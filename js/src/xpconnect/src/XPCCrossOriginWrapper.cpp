/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 sw=2 et tw=78: */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * The Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2006
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Blake Kaplan <mrbkap@gmail.com> (original author)
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include "xpcprivate.h"
#include "nsDOMError.h"
#include "jsdbgapi.h"
#include "jsobj.h"    // For OBJ_GET_PROPERTY.
#include "XPCWrapper.h"
#include "nsIDOMWindow.h"
#include "nsIDOMWindowCollection.h"

// This file implements a wrapper around objects that allows them to be
// accessed safely from across origins.

JS_STATIC_DLL_CALLBACK(JSBool)
XPC_XOW_AddProperty(JSContext *cx, JSObject *obj, jsval id, jsval *vp);

JS_STATIC_DLL_CALLBACK(JSBool)
XPC_XOW_DelProperty(JSContext *cx, JSObject *obj, jsval id, jsval *vp);

JS_STATIC_DLL_CALLBACK(JSBool)
XPC_XOW_GetProperty(JSContext *cx, JSObject *obj, jsval id, jsval *vp);

JS_STATIC_DLL_CALLBACK(JSBool)
XPC_XOW_SetProperty(JSContext *cx, JSObject *obj, jsval id, jsval *vp);

JS_STATIC_DLL_CALLBACK(JSBool)
XPC_XOW_Enumerate(JSContext *cx, JSObject *obj);

JS_STATIC_DLL_CALLBACK(JSBool)
XPC_XOW_NewResolve(JSContext *cx, JSObject *obj, jsval id, uintN flags,
                   JSObject **objp);

JS_STATIC_DLL_CALLBACK(JSBool)
XPC_XOW_Convert(JSContext *cx, JSObject *obj, JSType type, jsval *vp);

JS_STATIC_DLL_CALLBACK(void)
XPC_XOW_Finalize(JSContext *cx, JSObject *obj);

JS_STATIC_DLL_CALLBACK(JSBool)
XPC_XOW_CheckAccess(JSContext *cx, JSObject *obj, jsval id, JSAccessMode mode,
                    jsval *vp);

JS_STATIC_DLL_CALLBACK(JSBool)
XPC_XOW_Call(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval);

JS_STATIC_DLL_CALLBACK(JSBool)
XPC_XOW_Construct(JSContext *cx, JSObject *obj, uintN argc, jsval *argv,
                  jsval *rval);

JS_STATIC_DLL_CALLBACK(JSBool)
XPC_XOW_Equality(JSContext *cx, JSObject *obj, jsval v, JSBool *bp);

JSExtendedClass sXPC_XOW_JSClass = {
  // JSClass (JSExtendedClass.base) initialization
  { "XPCCrossOriginWrapper",
    JSCLASS_NEW_RESOLVE | JSCLASS_IS_EXTENDED |
    JSCLASS_HAS_RESERVED_SLOTS(XPCWrapper::sNumSlots + 1),
    XPC_XOW_AddProperty, XPC_XOW_DelProperty,
    XPC_XOW_GetProperty, XPC_XOW_SetProperty,
    XPC_XOW_Enumerate,   (JSResolveOp)XPC_XOW_NewResolve,
    XPC_XOW_Convert,     XPC_XOW_Finalize,
    nsnull,              XPC_XOW_CheckAccess,
    XPC_XOW_Call,        XPC_XOW_Construct,
    nsnull,              nsnull,
    nsnull,              nsnull
  },
  // JSExtendedClass initialization
  XPC_XOW_Equality
};

// The slot that we stick our scope into.
// This is used in the finalizer to see if we actually need to remove
// ourselves from our scope's map. Because we cannot outlive our scope
// (the parent link ensures this), we know that, when we're being
// finalized, either our scope is still alive (i.e. we became garbage
// due to no more references) or it is being garbage collected right now.
// Therefore, we can look in gDyingScopes, and if our scope is there,
// then the map is about to be destroyed anyway, so we don't need to
// do anything.
static const int XPC_XOW_ScopeSlot = XPCWrapper::sNumSlots;

JS_STATIC_DLL_CALLBACK(JSBool)
XPC_XOW_toString(JSContext *cx, JSObject *obj, uintN argc, jsval *argv,
                 jsval *rval);

// Throws an exception on context |cx|.
static inline
JSBool
ThrowException(nsresult ex, JSContext *cx)
{
  XPCThrower::Throw(ex, cx);

  return JS_FALSE;
}

// Get the (possibly non-existant) XOW off of an object
static inline
JSObject *
GetWrapper(JSContext *cx, JSObject *obj)
{
  while (JS_GET_CLASS(cx, obj) != &sXPC_XOW_JSClass.base) {
    obj = JS_GetParent(cx, obj);
    if (!obj) {
      break;
    }
  }

  return obj;
}

static inline
JSObject *
GetWrappedObject(JSContext *cx, JSObject *wrapper)
{
  if (JS_GET_CLASS(cx, wrapper) != &sXPC_XOW_JSClass.base) {
    return nsnull;
  }

  jsval v;
  if (!JS_GetReservedSlot(cx, wrapper, XPCWrapper::sWrappedObjSlot, &v)) {
    JS_ClearPendingException(cx);
    return nsnull;
  }

  if (!JSVAL_IS_OBJECT(v)) {
    return nsnull;
  }

  return JSVAL_TO_OBJECT(v);
}

static inline
nsIScriptSecurityManager *
GetSecurityManager(JSContext *cx)
{
  XPCCallContext ccx(JS_CALLER, cx);
  NS_ENSURE_TRUE(ccx.IsValid(), nsnull);

  // XXX HOOK_CALL_METHOD seems wrong.
  nsCOMPtr<nsIXPCSecurityManager> sm = ccx.GetXPCContext()->
    GetAppropriateSecurityManager(nsIXPCSecurityManager::HOOK_CALL_METHOD);

  nsCOMPtr<nsIScriptSecurityManager> ssm(do_QueryInterface(sm));

  // This Releases, but that's OK, since XPConnect holds a reference to it.
  return ssm;
}

static JSBool
IsValFrame(JSContext *cx, JSObject *obj, jsval v, XPCWrappedNative *wn)
{
  // Fast path for the common case.
  if (JS_GET_CLASS(cx, obj)->name[0] != 'W') {
    return JS_FALSE;
  }

  nsCOMPtr<nsIDOMWindow> domwin(do_QueryWrappedNative(wn));
  if (!domwin) {
    return JS_FALSE;
  }

  nsCOMPtr<nsIDOMWindowCollection> col;
  domwin->GetFrames(getter_AddRefs(col));
  if (!col) {
    return JS_FALSE;
  }

  if (JSVAL_IS_INT(v)) {
    col->Item(JSVAL_TO_INT(v), getter_AddRefs(domwin));
  } else {
    nsAutoString str(JS_GetStringChars(JSVAL_TO_STRING(v)));
    col->NamedItem(str, getter_AddRefs(domwin));
  }

  return domwin != nsnull;
}

// Returns whether the currently executing code has the same origin as the
// wrapper. Uses nsIScriptSecurityManager::CheckSameOriginPrincipal.
// |cx| must be the top context on the context stack.
// If the two principals have the same origin, returns NS_OK. If they differ,
// returns NS_ERROR_DOM_PROP_ACCESS_DENIED, returns another error code on
// failure.
nsresult
IsWrapperSameOrigin(JSContext *cx, JSObject *wrappedObj)
{
  nsCOMPtr<nsIPrincipal> subjectPrin, objectPrin;

  // Get the subject principal from the execution stack.
  nsIScriptSecurityManager *ssm = GetSecurityManager(cx);
  if (!ssm) {
    ThrowException(NS_ERROR_NOT_INITIALIZED, cx);
    return NS_ERROR_NOT_INITIALIZED;
  }

  nsresult rv = ssm->GetSubjectPrincipal(getter_AddRefs(subjectPrin));
  NS_ENSURE_SUCCESS(rv, rv);

  if (!subjectPrin) {
    ThrowException(NS_ERROR_FAILURE, cx);
    return NS_ERROR_FAILURE;
  }

  PRBool isSystem = PR_FALSE;
  rv = ssm->IsSystemPrincipal(subjectPrin, &isSystem);
  NS_ENSURE_SUCCESS(rv, rv);

  // If we somehow end up being called from chrome, just allow full access.
  // This can happen from components with xpcnativewrappers=no.
  if (isSystem) {
    return NS_OK;
  }

  rv = ssm->GetObjectPrincipal(cx, wrappedObj, getter_AddRefs(objectPrin));
  if (NS_FAILED(rv)) {
    return rv;
  }
  NS_ASSERTION(objectPrin, "Object didn't have principals?");

  // Micro-optimization: don't call into caps if we know the answer.
  if (subjectPrin == objectPrin) {
    return NS_OK;
  }

  // Now, we have our two principals, compare them!
  return ssm->CheckSameOriginPrincipal(subjectPrin, objectPrin);
}

static JSBool
XPC_XOW_FunctionWrapper(JSContext *cx, JSObject *obj, uintN argc, jsval *argv,
                        jsval *rval)
{
  JSObject *wrappedObj;

  obj = GetWrapper(cx, obj);
  if (!obj || (wrappedObj = GetWrappedObject(cx, obj)) == nsnull) {
    return ThrowException(NS_ERROR_ILLEGAL_VALUE, cx);
  }

  JSObject *funObj = JSVAL_TO_OBJECT(argv[-2]);
  jsval funToCall;
  if (!JS_GetReservedSlot(cx, funObj, 0, &funToCall)) {
    return JS_FALSE;
  }

  JSFunction *fun = JS_ValueToFunction(cx, funToCall);
  if (!fun) {
    return ThrowException(NS_ERROR_ILLEGAL_VALUE, cx);
  }

  JSNative native = JS_GetFunctionNative(cx, fun);
  NS_ASSERTION(native, "How'd we get here with a scripted function?");

  // A trick! Calling the native directly doesn't push the native onto the
  // JS stack, so interested onlookers will only see us, meaning that they
  // will compute *our* subject principal.

  argv[-2] = funToCall;
  argv[-1] = OBJECT_TO_JSVAL(wrappedObj);
  if (!native(cx, wrappedObj, argc, argv, rval)) {
    return JS_FALSE;
  }

  return XPC_XOW_RewrapIfNeeded(cx, obj, rval);
}

static JSObject *
GetGlobalObject(JSContext *cx, JSObject *start)
{
  JSObject *next;
  while ((next = JS_GetParent(cx, start)) != nsnull) {
    start = next;
  }

  return start;
}

JSBool
XPC_XOW_WrapFunction(JSContext *cx, JSObject *outerObj, JSObject *funobj,
                     jsval *rval)
{
  jsval funobjVal = OBJECT_TO_JSVAL(funobj);
  JSNative native = JS_GetFunctionNative(cx, JS_ValueToFunction(cx, funobjVal));
  if (!native || native == XPC_XOW_FunctionWrapper) {
    *rval = funobjVal;
    return JS_TRUE;
  }

  JSFunction *wrappedFun = JS_ValueToFunction(cx, OBJECT_TO_JSVAL(funobj));
  NS_ASSERTION(wrappedFun, "We were told this was a function");

  JSFunction *funWrapper =
    JS_NewFunction(cx, XPC_XOW_FunctionWrapper,
                   JS_GetFunctionArity(wrappedFun), 0,
                   GetGlobalObject(cx, outerObj),
                   "Wrapped function");
                   // XXX JS_GetFunctionName(wrappedFun));
  if (!funWrapper) {
    return JS_FALSE;
  }

  JSObject *funWrapperObj = JS_GetFunctionObject(funWrapper);
  if (!JS_SetReservedSlot(cx, funWrapperObj, 0, funobjVal)) {
    return JS_FALSE;
  }

  *rval = OBJECT_TO_JSVAL(funWrapperObj);
  return JS_TRUE;
}

JSBool
XPC_XOW_RewrapIfNeeded(JSContext *cx, JSObject *outerObj, jsval *vp)
{
  // Don't need to wrap primitive values.
  if (JSVAL_IS_PRIMITIVE(*vp)) {
    return JS_TRUE;
  }

  JSObject *obj = JSVAL_TO_OBJECT(*vp);

  if (JS_ObjectIsFunction(cx, obj)) {
    return XPC_XOW_WrapFunction(cx, outerObj, obj, vp);
  }

  // Don't need to wrap non-C++-implemented objects.
  // Note: This catches attempts to double-wrap cross origin wrappers.
  if (!XPCWrappedNative::GetWrappedNativeOfJSObject(cx, obj)) {
    return JS_TRUE;
  }

  return XPC_XOW_WrapObject(cx, GetGlobalObject(cx, outerObj), vp);
}

JSBool
XPC_XOW_WrapObject(JSContext *cx, JSObject *parent, jsval *vp)
{
  // Our argument should be a wrapped native object.
  JSObject *wrappedObj;
  XPCWrappedNative *wn;
  if (!JSVAL_IS_OBJECT(*vp) ||
      !(wrappedObj = JSVAL_TO_OBJECT(*vp)) ||
      !(wn = XPCWrappedNative::GetWrappedNativeOfJSObject(cx, wrappedObj))) {
    return JS_TRUE;
  }

  XPCJSRuntime *rt = nsXPConnect::GetRuntime();
  XPCCallContext ccx(NATIVE_CALLER, cx);
  NS_ENSURE_TRUE(ccx.IsValid(), JS_FALSE);

  XPCWrappedNativeScope *parentScope =
    XPCWrappedNativeScope::FindInJSObjectScope(ccx, parent);
  XPCWrappedNativeScope *wrapperScope = wn->GetScope();

#ifdef DEBUG_mrbkap
  printf("Wrapping object at %p (%s) [%p %p]\n",
         (void *)wrappedObj, JS_GET_CLASS(cx, wrappedObj)->name,
         (void *)parentScope, (void *)wrapperScope);
#endif

  JSObject *outerObj = nsnull;
  JSBool sameOrigin = (parentScope == wrapperScope);
  WrappedNative2WrapperMap *map =
    sameOrigin ? wrapperScope->GetWrapperMap() : parentScope->GetWrapperMap();

  if (sameOrigin) {
    outerObj = wn->GetWrapper();
    if (outerObj && JS_GET_CLASS(cx, outerObj) == &sXPC_XOW_JSClass.base) {
#ifdef DEBUG_mrbkap
      printf("But found a wrapper already there %p!\n", (void *)outerObj);
#endif
      *vp = OBJECT_TO_JSVAL(outerObj);
      return JS_TRUE;
    }
  }

  { // Scoped lock
    XPCAutoLock al(rt->GetMapLock());

    if (outerObj) {
      outerObj = map->Add(wrappedObj, outerObj);
      if (sameOrigin) {
        wn->SetWrapper(nsnull);
      }
    } else {
      outerObj = map->Find(wrappedObj);
    }
  }

  if (outerObj) {
    NS_ASSERTION(JS_GET_CLASS(cx, outerObj) == &sXPC_XOW_JSClass.base,
                              "What crazy object are we getting here?");
#ifdef DEBUG_mrbkap
    printf("But found a wrapper in the map %p!\n", (void *)outerObj);
#endif
    if (sameOrigin) {
      wn->SetWrapper(outerObj);
    }
    *vp = OBJECT_TO_JSVAL(outerObj);
    return JS_TRUE;
  }

  outerObj = JS_NewObject(cx, &sXPC_XOW_JSClass.base, nsnull, parent);
  if (!outerObj) {
    return JS_FALSE;
  }

  if (!JS_SetReservedSlot(cx, outerObj, XPCWrapper::sWrappedObjSlot, *vp) ||
      !JS_SetReservedSlot(cx, outerObj, XPCWrapper::sResolvingSlot,
                          BOOLEAN_TO_JSVAL(JS_FALSE)) ||
      !JS_SetReservedSlot(cx, outerObj, XPC_XOW_ScopeSlot,
                          PRIVATE_TO_JSVAL(parentScope))) {
    return JS_FALSE;
  }

  *vp = OBJECT_TO_JSVAL(outerObj);
  if (!sameOrigin) {
    XPCAutoLock al(rt->GetMapLock());
    map->Add(wrappedObj, outerObj);
  } else {
#ifdef DEBUG_mrbkap
    printf("Setting wrapper to %p\n", (void *)outerObj);
#endif
    wn->SetWrapper(outerObj);
  }

  return JS_TRUE;
}

JS_STATIC_DLL_CALLBACK(JSBool)
XPC_XOW_AddProperty(JSContext *cx, JSObject *obj, jsval id, jsval *vp)
{
  // All AddProperty needs to do is pass on addProperty requests to
  // same-origin objects, and throw for all else.

  obj = GetWrapper(cx, obj);
  jsval resolving;
  if (!JS_GetReservedSlot(cx, obj, XPCWrapper::sResolvingSlot, &resolving)) {
    return JS_FALSE;
  }

  if (JSVAL_TO_BOOLEAN(resolving)) {
    // Allow us to define a property on ourselves.
    return JS_TRUE;
  }

  JSObject *wrappedObj = GetWrappedObject(cx, obj);
  if (!wrappedObj) {
    return ThrowException(NS_ERROR_ILLEGAL_VALUE, cx);
  }
  nsresult rv = IsWrapperSameOrigin(cx, wrappedObj);
  if (NS_FAILED(rv)) {
    if (rv == NS_ERROR_DOM_PROP_ACCESS_DENIED) {
      // Can't override properties on foreign objects.
      return ThrowException(rv, cx);
    }
    return JS_FALSE;
  }

  // Same origin, pass this request along.
  return XPCWrapper::AddProperty(cx, wrappedObj, id, vp);
}

JS_STATIC_DLL_CALLBACK(JSBool)
XPC_XOW_DelProperty(JSContext *cx, JSObject *obj, jsval id, jsval *vp)
{
  JSObject *wrappedObj = GetWrappedObject(cx, obj);
  if (!wrappedObj) {
    return ThrowException(NS_ERROR_ILLEGAL_VALUE, cx);
  }
  nsresult rv = IsWrapperSameOrigin(cx, wrappedObj);
  if (NS_FAILED(rv)) {
    if (rv == NS_ERROR_DOM_PROP_ACCESS_DENIED) {
      // Can't delete properties on foreign objects.
      return ThrowException(rv, cx);
    }
    return JS_FALSE;
  }

  // Same origin, pass this request along.
  return XPCWrapper::DelProperty(cx, wrappedObj, id, vp);
}

static JSBool
XPC_XOW_GetOrSetProperty(JSContext *cx, JSObject *obj, jsval id, jsval *vp,
                         JSBool isSet)
{
  if (id == GetRTStringByIndex(cx, XPCJSRuntime::IDX_TO_STRING)) {
    return JS_TRUE;
  }

  XPCCallContext ccx(JS_CALLER, cx);
  if (!ccx.IsValid()) {
    return ThrowException(NS_ERROR_FAILURE, cx);
  }

  AUTO_MARK_JSVAL(ccx, vp);

  JSObject *wrappedObj = GetWrappedObject(cx, obj);
  if (!wrappedObj) {
    return ThrowException(NS_ERROR_ILLEGAL_VALUE, cx);
  }
  nsresult rv = IsWrapperSameOrigin(cx, wrappedObj);
  if (NS_FAILED(rv)) {
    if (rv != NS_ERROR_DOM_PROP_ACCESS_DENIED) {
      return JS_FALSE;
    }

    // This is a request to get a property across origins. We need to
    // determine if this property is allAccess. If it is, then we need to
    // actually get the property. If not, we simply need to throw an
    // exception.

    XPCWrappedNative *wn =
      XPCWrappedNative::GetWrappedNativeOfJSObject(cx, wrappedObj);
    NS_ASSERTION(wn, "How did we wrap a non-WrappedNative?");
    if (!IsValFrame(cx, wrappedObj, id, wn)) {
      nsIScriptSecurityManager *ssm = GetSecurityManager(cx);
      if (!ssm) {
        return ThrowException(NS_ERROR_NOT_INITIALIZED, cx);
      }

      PRUint32 check = isSet
                       ? (PRUint32)nsIXPCSecurityManager::ACCESS_SET_PROPERTY
                       : (PRUint32)nsIXPCSecurityManager::ACCESS_GET_PROPERTY;
      rv = ssm->CheckPropertyAccess(cx, wrappedObj,
                                    JS_GET_CLASS(cx, wrappedObj)->name,
                                    id, check);
      if (NS_FAILED(rv)) {
        // The security manager threw an exception for us.
        return JS_FALSE;
      }
    }

    if (!XPCWrapper::GetOrSetNativeProperty(cx, obj, wn, id, vp, isSet,
                                            JS_FALSE)) {
      return JS_FALSE;
    }

    return XPC_XOW_RewrapIfNeeded(cx, obj, vp);
  }

  // Same origin, pass this request along as though nothing interesting
  // happened.
  jsid asId;

  if (!JS_ValueToId(cx, id, &asId)) {
    return JS_FALSE;
  }

  JSBool ok = isSet
              ? OBJ_SET_PROPERTY(cx, wrappedObj, asId, vp)
              : OBJ_GET_PROPERTY(cx, wrappedObj, asId, vp);
  if (!ok) {
    return JS_FALSE;
  }

  // Don't call XPC_XOW_RewrapIfNeeded for same origin properties. We only
  // need to wrap window, document and location.
  if (JSVAL_IS_PRIMITIVE(*vp)) {
    return JS_TRUE;
  }

  wrappedObj = JSVAL_TO_OBJECT(*vp);
  const char *name = JS_GET_CLASS(cx, wrappedObj)->name;
  if (XPC_XOW_ClassNeedsXOW(name)) {
    return XPC_XOW_WrapObject(cx, GetGlobalObject(cx, obj), vp);
  }

  return JS_TRUE;
}

JS_STATIC_DLL_CALLBACK(JSBool)
XPC_XOW_GetProperty(JSContext *cx, JSObject *obj, jsval id, jsval *vp)
{
  return XPC_XOW_GetOrSetProperty(cx, obj, id, vp, JS_FALSE);
}

JS_STATIC_DLL_CALLBACK(JSBool)
XPC_XOW_SetProperty(JSContext *cx, JSObject *obj, jsval id, jsval *vp)
{
  return XPC_XOW_GetOrSetProperty(cx, obj, id, vp, JS_TRUE);
}

JS_STATIC_DLL_CALLBACK(JSBool)
XPC_XOW_Enumerate(JSContext *cx, JSObject *obj)
{
  obj = GetWrapper(cx, obj);
  JSObject *wrappedObj = GetWrappedObject(cx, obj);
  if (!wrappedObj) {
    // Nothing to enumerate.
    return JS_TRUE;
  }
  nsresult rv = IsWrapperSameOrigin(cx, wrappedObj);
  if (NS_FAILED(rv)) {
    if (rv == NS_ERROR_DOM_PROP_ACCESS_DENIED) {
      // Can't enumerate on foreign objects.
      return ThrowException(rv, cx);
    }

    return JS_FALSE;
  }

  return XPCWrapper::Enumerate(cx, obj, wrappedObj);
}

JS_STATIC_DLL_CALLBACK(JSBool)
XPC_XOW_NewResolve(JSContext *cx, JSObject *obj, jsval id, uintN flags,
                   JSObject **objp)
{
  obj = GetWrapper(cx, obj);

  if (id == GetRTStringByIndex(cx, XPCJSRuntime::IDX_TO_STRING)) {
    *objp = obj;
    return JS_DefineFunction(cx, obj, "toString",
                             XPC_XOW_toString, 0, 0) != nsnull;
  }

  JSObject *wrappedObj = GetWrappedObject(cx, obj);
  if (!wrappedObj) {
    // No wrappedObj means that this is probably the prototype.
    *objp = nsnull;
    return JS_TRUE;
  }

  nsresult rv = IsWrapperSameOrigin(cx, wrappedObj);
  if (NS_FAILED(rv)) {
    if (rv != NS_ERROR_DOM_PROP_ACCESS_DENIED) {
      return JS_FALSE;
    }

    // We're dealing with a cross-origin lookup. Ensure that we're allowed to
    // resolve this property and resolve it if so. Otherwise, we deny access
    // and throw a security error. Note that this code does not actually check
    // to see if the property exists, that's dealt with below.

    XPCWrappedNative *wn =
      XPCWrappedNative::GetWrappedNativeOfJSObject(cx, wrappedObj);
    NS_ASSERTION(wn, "How did we wrap a non-WrappedNative?");
    if (!IsValFrame(cx, wrappedObj, id, wn)) {
      nsIScriptSecurityManager *ssm = GetSecurityManager(cx);
      if (!ssm) {
        return ThrowException(NS_ERROR_NOT_INITIALIZED, cx);
      }
      PRUint32 action = (flags & JSRESOLVE_ASSIGNING)
                        ? (PRUint32)nsIXPCSecurityManager::ACCESS_SET_PROPERTY
                        : (PRUint32)nsIXPCSecurityManager::ACCESS_GET_PROPERTY;
      rv = ssm->CheckPropertyAccess(cx, wrappedObj,
                                    JS_GET_CLASS(cx, wrappedObj)->name,
                                    id, action);
      if (NS_FAILED(rv)) {
        // The security manager threw an exception for us.
        return JS_FALSE;
      }
    }

    // We're out! We're allowed to resolve this property.
    return XPCWrapper::ResolveNativeProperty(cx, obj, wrappedObj, wn, id,
                                             flags, objp, JS_FALSE);

  }

  return XPCWrapper::NewResolve(cx, obj, wrappedObj, id, flags, objp);
}

JS_STATIC_DLL_CALLBACK(JSBool)
XPC_XOW_Convert(JSContext *cx, JSObject *obj, JSType type, jsval *vp)
{
  JSObject *wrappedObj = GetWrappedObject(cx, obj);
  if (!wrappedObj) {
    // Converting the prototype to something.

    if (type == JSTYPE_STRING) {
      return XPC_XOW_toString(cx, obj, 0, nsnull, vp);
    }

    *vp = OBJECT_TO_JSVAL(obj);
    return JS_TRUE;
  }

  nsresult rv = IsWrapperSameOrigin(cx, wrappedObj);
  if (NS_FAILED(rv) &&
      (rv != NS_ERROR_DOM_PROP_ACCESS_DENIED || type != JSTYPE_STRING)) {
    return JS_FALSE;
  }

  // TODO wrap return value?
  return JS_GET_CLASS(cx, wrappedObj)->convert(cx, wrappedObj, type, vp);
}

JS_STATIC_DLL_CALLBACK(void)
XPC_XOW_Finalize(JSContext *cx, JSObject *obj)
{
  JSObject *wrappedObj = GetWrappedObject(cx, obj);
  if (!wrappedObj) {
    return;
  }

  // Get our scope.
  jsval scopeVal;
  if (!JS_GetReservedSlot(cx, obj, XPC_XOW_ScopeSlot, &scopeVal)) {
    return;
  }

  // Now that we have our scope, see if it's going away. If it is,
  // then our work here is going to be done when we destroy the scope
  // entirely.
  XPCWrappedNativeScope *scope = reinterpret_cast<XPCWrappedNativeScope *>
                                                 (JSVAL_TO_PRIVATE(scopeVal));
  if (XPCWrappedNativeScope::IsDyingScope(scope)) {
    return;
  }

  // Remove ourselves from the map.
  scope->GetWrapperMap()->Remove(wrappedObj);
}

JS_STATIC_DLL_CALLBACK(JSBool)
XPC_XOW_CheckAccess(JSContext *cx, JSObject *obj, jsval prop, JSAccessMode mode,
                    jsval *vp)
{
  // Simply forward checkAccess to our wrapped object. It's already expecting
  // untrusted things to ask it about accesses.

  uintN junk;
  jsid id;
  return JS_ValueToId(cx, prop, &id) &&
         JS_CheckAccess(cx, GetWrappedObject(cx, obj), id, mode, vp, &junk);
}

JS_STATIC_DLL_CALLBACK(JSBool)
XPC_XOW_Call(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
  JSObject *wrappedObj = GetWrappedObject(cx, obj);
  if (!wrappedObj) {
    // Nothing to call.
    return JS_TRUE;
  }
  nsresult rv = IsWrapperSameOrigin(cx, wrappedObj);
  if (NS_FAILED(rv)) {
    if (rv == NS_ERROR_DOM_PROP_ACCESS_DENIED) {
      // Can't call.
      return ThrowException(rv, cx);
    }

    return JS_FALSE;
  }

  JSObject *callee = JSVAL_TO_OBJECT(argv[-2]);
  NS_ASSERTION(GetWrappedObject(cx, callee), "How'd we get here?");
  callee = GetWrappedObject(cx, callee);
  if (!JS_CallFunctionValue(cx, obj, OBJECT_TO_JSVAL(callee), argc, argv,
                            rval)) {
    return JS_FALSE;
  }

  return XPC_XOW_RewrapIfNeeded(cx, callee, rval);
}

JS_STATIC_DLL_CALLBACK(JSBool)
XPC_XOW_Construct(JSContext *cx, JSObject *obj, uintN argc, jsval *argv,
                  jsval *rval)
{
  JSObject *realObj = GetWrapper(cx, JSVAL_TO_OBJECT(argv[-2]));
  JSObject *wrappedObj = GetWrappedObject(cx, realObj);
  if (!wrappedObj) {
    // Nothing to construct.
    return JS_TRUE;
  }
  nsresult rv = IsWrapperSameOrigin(cx, wrappedObj);
  if (NS_FAILED(rv)) {
    if (rv == NS_ERROR_DOM_PROP_ACCESS_DENIED) {
      // Can't construct.
      return ThrowException(rv, cx);
    }
    return JS_FALSE;
  }

  JSObject *callee = JSVAL_TO_OBJECT(argv[-2]);
  NS_ASSERTION(GetWrappedObject(cx, callee), "How'd we get here?");
  callee = GetWrappedObject(cx, callee);
  if (!JS_CallFunctionValue(cx, obj, OBJECT_TO_JSVAL(callee), argc, argv,
                            rval)) {
    return JS_FALSE;
  }

  return XPC_XOW_RewrapIfNeeded(cx, callee, rval);
}

JS_STATIC_DLL_CALLBACK(JSBool)
XPC_XOW_Equality(JSContext *cx, JSObject *obj, jsval v, JSBool *bp)
{
  // Convert both sides to XPCWrappedNative and see if they match.
  if (JSVAL_IS_PRIMITIVE(v)) {
    *bp = JS_FALSE;
    return JS_TRUE;
  }

  JSObject *test = JSVAL_TO_OBJECT(v);
  if (JS_GET_CLASS(cx, test) == &sXPC_XOW_JSClass.base) {
    if (!JS_GetReservedSlot(cx, test, XPCWrapper::sWrappedObjSlot, &v)) {
      return JS_FALSE;
    }

    if (JSVAL_IS_PRIMITIVE(v)) {
      *bp = JS_FALSE;
      return JS_TRUE;
    }

    test = JSVAL_TO_OBJECT(v);
  }

  obj = GetWrappedObject(cx, obj);
  if (!obj) {
    return ThrowException(NS_ERROR_ILLEGAL_VALUE, cx);
  }
  XPCWrappedNative *other =
    XPCWrappedNative::GetWrappedNativeOfJSObject(cx, test);
  if (!other) {
    *bp = JS_FALSE;
    return JS_TRUE;
  }

  XPCWrappedNative *me = XPCWrappedNative::GetWrappedNativeOfJSObject(cx, obj);
  obj = me->GetFlatJSObject();
  test = other->GetFlatJSObject();
  return ((JSExtendedClass *)JS_GET_CLASS(cx, obj))->
    equality(cx, obj, OBJECT_TO_JSVAL(test), bp);
}

JS_STATIC_DLL_CALLBACK(JSBool)
XPC_XOW_toString(JSContext *cx, JSObject *obj, uintN argc, jsval *argv,
                 jsval *rval)
{
  obj = GetWrapper(cx, obj);
  if (!obj) {
    return ThrowException(NS_ERROR_UNEXPECTED, cx);
  }

  JSObject *wrappedObj = GetWrappedObject(cx, obj);
  if (!wrappedObj) {
    // Someone's calling toString on our prototype.
    NS_NAMED_LITERAL_CSTRING(protoString, "[object XPCCrossOriginWrapper]");
    JSString *str =
      JS_NewStringCopyN(cx, protoString.get(), protoString.Length());
    if (!str) {
      return JS_FALSE;
    }
    *rval = STRING_TO_JSVAL(str);
    return JS_TRUE;
  }

  nsresult rv = IsWrapperSameOrigin(cx, wrappedObj);
  if (rv == NS_ERROR_DOM_PROP_ACCESS_DENIED) {
    nsIScriptSecurityManager *ssm = GetSecurityManager(cx);
    if (!ssm) {
      return ThrowException(NS_ERROR_NOT_INITIALIZED, cx);
    }
    rv = ssm->CheckPropertyAccess(cx, wrappedObj,
                                  JS_GET_CLASS(cx, wrappedObj)->name,
                                  GetRTStringByIndex(cx, XPCJSRuntime::IDX_TO_STRING),
                                  nsIXPCSecurityManager::ACCESS_GET_PROPERTY);
  }
  if (NS_FAILED(rv)) {
    return JS_FALSE;
  }

  XPCWrappedNative *wn =
    XPCWrappedNative::GetWrappedNativeOfJSObject(cx, wrappedObj);
  return XPCWrapper::NativeToString(cx, wn, argc, argv, rval, JS_FALSE);
}
