/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
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
 * The Original Code is Mozilla Communicator client code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Original Author: David W. Hyatt (hyatt@netscape.com)
 *   Alec Flett <alecf@netscape.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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

#include "nsCOMPtr.h"
#include "nsIXBLService.h"
#include "nsIInputStream.h"
#include "nsDoubleHashtable.h"
#include "nsIURI.h"
#include "nsIURL.h"
#include "nsIChannel.h"
#include "nsXPIDLString.h"
#include "nsIParser.h"
#include "nsParserCIID.h"
#include "nsNetUtil.h"
#include "plstr.h"
#include "nsIContent.h"
#include "nsIDOMElement.h"
#include "nsIDocument.h"
#include "nsContentUtils.h"
#include "nsIPresShell.h"
#include "nsIXMLContentSink.h"
#include "nsContentCID.h"
#include "nsXMLDocument.h"
#include "nsIStreamListener.h"
#include "nsGenericDOMNodeList.h"

#include "nsXBLBinding.h"
#include "nsXBLPrototypeBinding.h"
#include "nsIXBLDocumentInfo.h"
#include "nsXBLInsertionPoint.h"

#include "nsIStyleSheet.h"
#include "nsHTMLStyleSheet.h"
#include "nsIHTMLCSSStyleSheet.h"

#include "nsIStyleRuleProcessor.h"
#include "nsIWeakReference.h"

#include "jsapi.h"
#include "nsIXPConnect.h"
#include "nsDOMCID.h"
#include "nsIDOMScriptObjectFactory.h"
#include "nsIScriptGlobalObject.h"

#include "nsIScriptContext.h"
#include "nsBindingManager.h"

#include "nsThreadUtils.h"

// ==================================================================
// = nsAnonymousContentList 
// ==================================================================

class nsAnonymousContentList : public nsIDOMNodeList
{
public:
  nsAnonymousContentList(nsInsertionPointList* aElements);
  virtual ~nsAnonymousContentList();

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS(nsAnonymousContentList)
  // nsIDOMNodeList interface
  NS_DECL_NSIDOMNODELIST

  PRInt32 GetInsertionPointCount() { return mElements->Length(); }

  nsXBLInsertionPoint* GetInsertionPointAt(PRInt32 i) { return static_cast<nsXBLInsertionPoint*>(mElements->ElementAt(i)); }
  void RemoveInsertionPointAt(PRInt32 i) { mElements->RemoveElementAt(i); }

private:
  nsInsertionPointList* mElements;
};

nsAnonymousContentList::nsAnonymousContentList(nsInsertionPointList* aElements)
  : mElements(aElements)
{
  MOZ_COUNT_CTOR(nsAnonymousContentList);

  // We don't reference count our Anonymous reference (to avoid circular
  // references). We'll be told when the Anonymous goes away.
}

nsAnonymousContentList::~nsAnonymousContentList()
{
  MOZ_COUNT_DTOR(nsAnonymousContentList);
  delete mElements;
}

NS_IMPL_CYCLE_COLLECTION_CLASS(nsAnonymousContentList)

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsAnonymousContentList)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsAnonymousContentList)

NS_INTERFACE_MAP_BEGIN(nsAnonymousContentList)
  NS_INTERFACE_MAP_ENTRY(nsIDOMNodeList)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
  NS_INTERFACE_MAP_ENTRY_CONTENT_CLASSINFO(NodeList)
  NS_INTERFACE_MAP_ENTRIES_CYCLE_COLLECTION(nsAnonymousContentList)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_0(nsAnonymousContentList)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(nsAnonymousContentList)
  {
    PRInt32 i, count = tmp->mElements->Length();
    for (i = 0; i < count; ++i) {
      NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NATIVE_MEMBER(mElements->ElementAt(i),
                                                      nsXBLInsertionPoint);
    }
  }
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMETHODIMP
nsAnonymousContentList::GetLength(PRUint32* aLength)
{
  NS_ASSERTION(aLength != nsnull, "null ptr");
  if (! aLength)
      return NS_ERROR_NULL_POINTER;

  PRInt32 cnt = mElements->Length();

  *aLength = 0;
  for (PRInt32 i = 0; i < cnt; i++)
    *aLength += static_cast<nsXBLInsertionPoint*>(mElements->ElementAt(i))->ChildCount();

  return NS_OK;
}

NS_IMETHODIMP    
nsAnonymousContentList::Item(PRUint32 aIndex, nsIDOMNode** aReturn)
{
  PRInt32 cnt = mElements->Length();
  PRUint32 pointCount = 0;

  for (PRInt32 i = 0; i < cnt; i++) {
    aIndex -= pointCount;
    
    nsXBLInsertionPoint* point = static_cast<nsXBLInsertionPoint*>(mElements->ElementAt(i));
    pointCount = point->ChildCount();

    if (aIndex < pointCount) {
      nsCOMPtr<nsIContent> result = point->ChildAt(aIndex);
      if (result)
        return CallQueryInterface(result, aReturn);
      return NS_ERROR_FAILURE;
    }
  }

  return NS_ERROR_FAILURE;
}

//
// Generic pldhash table stuff for mapping one nsISupports to another
//
// These values are never null - a null value implies that this
// whole key should be removed (See SetOrRemoveObject)
class ObjectEntry : public PLDHashEntryHdr
{
public:

  // note that these are allocated within the PLDHashTable, but we
  // want to keep track of them anyway
  ObjectEntry() { MOZ_COUNT_CTOR(ObjectEntry); }
  ~ObjectEntry() { MOZ_COUNT_DTOR(ObjectEntry); }
  
  nsISupports* GetValue() { return mValue; }
  nsISupports* GetKey() { return mKey; }
  void SetValue(nsISupports* aValue) { mValue = aValue; }
  void SetKey(nsISupports* aKey) { mKey = aKey; }
  
private:
  nsCOMPtr<nsISupports> mKey;
  nsCOMPtr<nsISupports> mValue;
};

PR_STATIC_CALLBACK(void)
ClearObjectEntry(PLDHashTable* table, PLDHashEntryHdr *entry)
{
  ObjectEntry* objEntry = static_cast<ObjectEntry*>(entry);
  objEntry->~ObjectEntry();
}

PR_STATIC_CALLBACK(PRBool)
InitObjectEntry(PLDHashTable* table, PLDHashEntryHdr* entry, const void* key)
{
  new (entry) ObjectEntry;
  return PR_TRUE;
}
  


static PLDHashTableOps ObjectTableOps = {
  PL_DHashAllocTable,
  PL_DHashFreeTable,
  PL_DHashVoidPtrKeyStub,
  PL_DHashMatchEntryStub,
  PL_DHashMoveEntryStub,
  ClearObjectEntry,
  PL_DHashFinalizeStub,
  InitObjectEntry
};

// helper routine for adding a new entry
static nsresult
AddObjectEntry(PLDHashTable& table, nsISupports* aKey, nsISupports* aValue)
{
  NS_ASSERTION(aKey, "key must be non-null");
  if (!aKey) return NS_ERROR_INVALID_ARG;
  
  ObjectEntry *entry =
    static_cast<ObjectEntry*>
               (PL_DHashTableOperate(&table, aKey, PL_DHASH_ADD));

  if (!entry)
    return NS_ERROR_OUT_OF_MEMORY;

  // only add the key if the entry is new
  if (!entry->GetKey())
    entry->SetKey(aKey);

  // now attach the new entry - note that entry->mValue could possibly
  // have a value already, this will release that.
  entry->SetValue(aValue);
  
  return NS_OK;
}

// helper routine for looking up an existing entry. Note that the
// return result is NOT addreffed
static nsISupports*
LookupObject(PLDHashTable& table, nsISupports* aKey)
{
  ObjectEntry *entry =
    static_cast<ObjectEntry*>
               (PL_DHashTableOperate(&table, aKey, PL_DHASH_LOOKUP));

  if (PL_DHASH_ENTRY_IS_BUSY(entry))
    return entry->GetValue();

  return nsnull;
}

inline void
RemoveObjectEntry(PLDHashTable& table, nsISupports* aKey)
{
  PL_DHashTableOperate(&table, aKey, PL_DHASH_REMOVE);
}

static nsresult
SetOrRemoveObject(PLDHashTable& table, nsIContent* aKey, nsISupports* aValue)
{
  if (aValue) {
    // lazily create the table, but only when adding elements
    if (!table.ops &&
        !PL_DHashTableInit(&table, &ObjectTableOps, nsnull,
                           sizeof(ObjectEntry), 16)) {
      table.ops = nsnull;
      return NS_ERROR_OUT_OF_MEMORY;
    }
    aKey->SetFlags(NODE_MAY_BE_IN_BINDING_MNGR);
    return AddObjectEntry(table, aKey, aValue);
  }

  // no value, so remove the key from the table
  if (table.ops)
    RemoveObjectEntry(table, aKey);
  return NS_OK;
}

// Implementation /////////////////////////////////////////////////////////////////

// Static member variable initialization

// Implement our nsISupports methods

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(nsBindingManager)
  if (tmp->mBindingTable.IsInitialized())
    tmp->mBindingTable.Clear();

  if (tmp->mDocumentTable.IsInitialized())
    tmp->mDocumentTable.Clear();

  if (tmp->mLoadingDocTable.IsInitialized())
    tmp->mLoadingDocTable.Clear();

  if (tmp->mContentListTable.ops)
    PL_DHashTableFinish(&(tmp->mContentListTable));
  tmp->mContentListTable.ops = nsnull;

  if (tmp->mAnonymousNodesTable.ops)
    PL_DHashTableFinish(&(tmp->mAnonymousNodesTable));
  tmp->mAnonymousNodesTable.ops = nsnull;

  if (tmp->mInsertionParentTable.ops)
    PL_DHashTableFinish(&(tmp->mInsertionParentTable));
  tmp->mInsertionParentTable.ops = nsnull;

  if (tmp->mWrapperTable.ops)
    PL_DHashTableFinish(&(tmp->mWrapperTable));
  tmp->mWrapperTable.ops = nsnull;

  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSTARRAY(mAttachedStack)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END


static PLDHashOperator
DocumentInfoHashtableTraverser(nsIURI* key,
                               nsIXBLDocumentInfo* di,
                               void* userArg)
{
  nsCycleCollectionTraversalCallback *cb = 
    static_cast<nsCycleCollectionTraversalCallback*>(userArg);
  cb->NoteXPCOMChild(di);
  return PL_DHASH_NEXT;
}

static PLDHashOperator
LoadingDocHashtableTraverser(nsIURI* key,
                             nsIStreamListener* sl,
                             void* userArg)
{
  nsCycleCollectionTraversalCallback *cb = 
    static_cast<nsCycleCollectionTraversalCallback*>(userArg);
  cb->NoteXPCOMChild(sl);
  return PL_DHASH_NEXT;
}

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(nsBindingManager)
  // The hashes keyed on nsIContent are traversed from the nsIContent itself.
  if (tmp->mDocumentTable.IsInitialized())
      tmp->mDocumentTable.EnumerateRead(&DocumentInfoHashtableTraverser, &cb);
  if (tmp->mLoadingDocTable.IsInitialized())
      tmp->mLoadingDocTable.EnumerateRead(&LoadingDocHashtableTraverser, &cb);
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSTARRAY_MEMBER(mAttachedStack,
                                                    nsXBLBinding)
  // No need to traverse mProcessAttachedQueueEvent, since it'll just
  // fire at some point.
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_CLASS(nsBindingManager)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsBindingManager)
  NS_INTERFACE_MAP_ENTRY(nsIMutationObserver)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsBindingManager)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsBindingManager)

// Constructors/Destructors
nsBindingManager::nsBindingManager(void)
  : mProcessingAttachedStack(PR_FALSE),
    mProcessOnEndUpdate(PR_FALSE)
{
  mContentListTable.ops = nsnull;
  mAnonymousNodesTable.ops = nsnull;
  mInsertionParentTable.ops = nsnull;
  mWrapperTable.ops = nsnull;
}

nsBindingManager::~nsBindingManager(void)
{
  if (mContentListTable.ops)
    PL_DHashTableFinish(&mContentListTable);
  if (mAnonymousNodesTable.ops)
    PL_DHashTableFinish(&mAnonymousNodesTable);
  if (mInsertionParentTable.ops)
    PL_DHashTableFinish(&mInsertionParentTable);
  if (mWrapperTable.ops)
    PL_DHashTableFinish(&mWrapperTable);
}

nsXBLBinding*
nsBindingManager::GetBinding(nsIContent* aContent)
{
  if (aContent && aContent->HasFlag(NODE_MAY_BE_IN_BINDING_MNGR) &&
      mBindingTable.IsInitialized()) {
    return mBindingTable.GetWeak(aContent);
  }

  return nsnull;
}

nsresult
nsBindingManager::SetBinding(nsIContent* aContent, nsXBLBinding* aBinding)
{
  if (!mBindingTable.IsInitialized()) {
    if (!mBindingTable.Init())
      return NS_ERROR_OUT_OF_MEMORY;
  }

  // After this point, aBinding will be the most-derived binding for aContent.
  // If we already have a binding for aContent in our table, make sure to
  // remove it from the attached stack.  Otherwise we might end up firing its
  // constructor twice (if aBinding inherits from it) or firing its constructor
  // after aContent has been deleted (if aBinding is null and the content node
  // dies before we process mAttachedStack).
  nsXBLBinding* oldBinding = mBindingTable.GetWeak(aContent);
  if (oldBinding) {
    mAttachedStack.RemoveElement(oldBinding);
  }
  
  PRBool result = PR_TRUE;

  if (aBinding) {
    aContent->SetFlags(NODE_MAY_BE_IN_BINDING_MNGR);
    result = mBindingTable.Put(aContent, aBinding);
  } else {
    mBindingTable.Remove(aContent);

    // The death of the bindings means the death of the JS wrapper,
    // and the flushing of our explicit and anonymous insertion point
    // lists.
    SetWrappedJS(aContent, nsnull);
    SetContentListFor(aContent, nsnull);
    SetAnonymousNodesFor(aContent, nsnull);
  }

  return result ? NS_OK : NS_ERROR_FAILURE;
}

nsIContent*
nsBindingManager::GetInsertionParent(nsIContent* aContent)
{ 
  if (mInsertionParentTable.ops) {
    return static_cast<nsIContent*>
                      (LookupObject(mInsertionParentTable, aContent));
  }

  return nsnull;
}

nsresult
nsBindingManager::SetInsertionParent(nsIContent* aContent, nsIContent* aParent)
{
  return SetOrRemoveObject(mInsertionParentTable, aContent, aParent);
}

nsIXPConnectWrappedJS*
nsBindingManager::GetWrappedJS(nsIContent* aContent)
{ 
  if (mWrapperTable.ops) {
    return static_cast<nsIXPConnectWrappedJS*>(LookupObject(mWrapperTable, aContent));
  }

  return nsnull;
}

nsresult
nsBindingManager::SetWrappedJS(nsIContent* aContent, nsIXPConnectWrappedJS* aWrappedJS)
{
  return SetOrRemoveObject(mWrapperTable, aContent, aWrappedJS);
}

nsresult
nsBindingManager::ChangeDocumentFor(nsIContent* aContent, nsIDocument* aOldDocument,
                                    nsIDocument* aNewDocument)
{
  // XXXbz this code is pretty broken, since moving from one document
  // to another always passes through a null document!
  NS_PRECONDITION(aOldDocument != nsnull, "no old document");
  NS_PRECONDITION(!aNewDocument,
                  "Changing to a non-null new document not supported yet");
  if (! aOldDocument)
    return NS_ERROR_NULL_POINTER;

  // Hold a ref to the binding so it won't die when we remove it from our
  // table.
  nsRefPtr<nsXBLBinding> binding = GetBinding(aContent);
  if (binding) {
    binding->ChangeDocument(aOldDocument, aNewDocument);
    SetBinding(aContent, nsnull);
    if (aNewDocument)
      aNewDocument->BindingManager()->SetBinding(aContent, binding);
  }

  // Clear out insertion parents and content lists.
  SetInsertionParent(aContent, nsnull);
  SetContentListFor(aContent, nsnull);
  SetAnonymousNodesFor(aContent, nsnull);

  return NS_OK;
}

nsIAtom*
nsBindingManager::ResolveTag(nsIContent* aContent, PRInt32* aNameSpaceID)
{
  nsXBLBinding *binding = GetBinding(aContent);
  
  if (binding) {
    nsIAtom* base = binding->GetBaseTag(aNameSpaceID);

    if (base) {
      return base;
    }
  }

  *aNameSpaceID = aContent->GetNameSpaceID();
  return aContent->Tag();
}

nsresult
nsBindingManager::GetContentListFor(nsIContent* aContent, nsIDOMNodeList** aResult)
{ 
  // Locate the primary binding and get its node list of anonymous children.
  *aResult = nsnull;
  
  if (mContentListTable.ops) {
    *aResult = static_cast<nsIDOMNodeList*>
                          (LookupObject(mContentListTable, aContent));
    NS_IF_ADDREF(*aResult);
  }
  
  if (!*aResult) {
    nsCOMPtr<nsIDOMNode> node(do_QueryInterface(aContent));
    node->GetChildNodes(aResult);
  }

  return NS_OK;
}

nsresult
nsBindingManager::SetContentListFor(nsIContent* aContent,
                                    nsInsertionPointList* aList)
{
  nsIDOMNodeList* contentList = nsnull;
  if (aList) {
    contentList = new nsAnonymousContentList(aList);
    if (!contentList) {
      delete aList;
      return NS_ERROR_OUT_OF_MEMORY;
    }
  }

  return SetOrRemoveObject(mContentListTable, aContent, contentList);
}

PRBool
nsBindingManager::HasContentListFor(nsIContent* aContent)
{
  return mContentListTable.ops && LookupObject(mContentListTable, aContent);
}

nsresult
nsBindingManager::GetAnonymousNodesInternal(nsIContent* aContent,
                                            nsIDOMNodeList** aResult,
                                            PRBool* aIsAnonymousContentList)
{ 
  // Locate the primary binding and get its node list of anonymous children.
  *aResult = nsnull;
  if (mAnonymousNodesTable.ops) {
    *aResult = static_cast<nsIDOMNodeList*>
                          (LookupObject(mAnonymousNodesTable, aContent));
    NS_IF_ADDREF(*aResult);
  }

  if (!*aResult) {
    *aIsAnonymousContentList = PR_FALSE;
    nsXBLBinding *binding = GetBinding(aContent);
    if (binding) {
      *aResult = binding->GetAnonymousNodes().get();
      return NS_OK;
    }
  } else
    *aIsAnonymousContentList = PR_TRUE;

  return NS_OK;
}

nsresult
nsBindingManager::GetAnonymousNodesFor(nsIContent* aContent,
                                       nsIDOMNodeList** aResult)
{
  PRBool dummy;
  return GetAnonymousNodesInternal(aContent, aResult, &dummy);
}

nsresult
nsBindingManager::SetAnonymousNodesFor(nsIContent* aContent,
                                       nsInsertionPointList* aList)
{
  nsIDOMNodeList* contentList = nsnull;
  if (aList) {
    contentList = new nsAnonymousContentList(aList);
    if (!contentList) {
      delete aList;
      return NS_ERROR_OUT_OF_MEMORY;
    }
  }

  return SetOrRemoveObject(mAnonymousNodesTable, aContent, contentList);
}

nsresult
nsBindingManager::GetXBLChildNodesInternal(nsIContent* aContent,
                                           nsIDOMNodeList** aResult,
                                           PRBool* aIsAnonymousContentList)
{
  *aResult = nsnull;

  PRUint32 length;

  // Retrieve the anonymous content that we should build.
  nsCOMPtr<nsIDOMNodeList> result;
  GetAnonymousNodesInternal(aContent, getter_AddRefs(result),
                            aIsAnonymousContentList);
  if (result) {
    result->GetLength(&length);
    if (length == 0)
      result = nsnull;
  }
    
  // We may have an altered list of children from XBL insertion points.
  // If we don't have any anonymous kids, we next check to see if we have 
  // insertion points.
  if (!result) {
    if (mContentListTable.ops) {
      result = static_cast<nsIDOMNodeList*>
                          (LookupObject(mContentListTable, aContent));
      *aIsAnonymousContentList = PR_TRUE;
    }
  }

  result.swap(*aResult);

  return NS_OK;
}

nsresult
nsBindingManager::GetXBLChildNodesFor(nsIContent* aContent, nsIDOMNodeList** aResult)
{
  PRBool dummy;
  return GetXBLChildNodesInternal(aContent, aResult, &dummy);
}

nsIContent*
nsBindingManager::GetInsertionPoint(nsIContent* aParent, nsIContent* aChild,
                                    PRUint32* aIndex)
{
  nsXBLBinding *binding = GetBinding(aParent);
  return binding ? binding->GetInsertionPoint(aChild, aIndex) : nsnull;
}

nsIContent*
nsBindingManager::GetSingleInsertionPoint(nsIContent* aParent,
                                          PRUint32* aIndex,
                                          PRBool* aMultipleInsertionPoints)
{
  nsXBLBinding *binding = GetBinding(aParent);
  if (binding)
    return binding->GetSingleInsertionPoint(aIndex, aMultipleInsertionPoints);

  *aMultipleInsertionPoints = PR_FALSE;
  return nsnull;
}

nsresult
nsBindingManager::AddLayeredBinding(nsIContent* aContent, nsIURI* aURL,
                                    nsIPrincipal* aOriginPrincipal)
{
  // First we need to load our binding.
  nsresult rv;
  nsCOMPtr<nsIXBLService> xblService = 
           do_GetService("@mozilla.org/xbl;1", &rv);
  if (!xblService)
    return rv;

  // Load the bindings.
  nsRefPtr<nsXBLBinding> binding;
  PRBool dummy;
  xblService->LoadBindings(aContent, aURL, aOriginPrincipal, PR_TRUE,
                           getter_AddRefs(binding), &dummy);
  if (binding) {
    AddToAttachedQueue(binding);
    ProcessAttachedQueue();
  }

  return NS_OK;
}

nsresult
nsBindingManager::RemoveLayeredBinding(nsIContent* aContent, nsIURI* aURL)
{
  // Hold a ref to the binding so it won't die when we remove it from our table
  nsRefPtr<nsXBLBinding> binding = GetBinding(aContent);
  
  if (!binding) {
    return NS_OK;
  }

  // For now we can only handle removing a binding if it's the only one
  NS_ENSURE_FALSE(binding->GetBaseBinding(), NS_ERROR_FAILURE);

  // Make sure that the binding has the URI that is requested to be removed
  nsIURI* bindingUri = binding->PrototypeBinding()->BindingURI();
  
  PRBool equalUri;
  nsresult rv = aURL->Equals(bindingUri, &equalUri);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!equalUri) {
    return NS_OK;
  }

  // Make sure it isn't a style binding
  if (binding->IsStyleBinding()) {
    return NS_OK;
  }

  // Hold strong ref in case removing the binding tries to close the
  // window or something.
  // XXXbz should that be ownerdoc?  Wouldn't we need a ref to the
  // currentdoc too?  What's the one that should be passed to
  // ChangeDocument?
  nsCOMPtr<nsIDocument> doc = aContent->GetOwnerDoc();
  NS_ASSERTION(doc, "No owner document?");
  
  // Finally remove the binding...
  binding->UnhookEventHandlers();
  binding->ChangeDocument(doc, nsnull);
  SetBinding(aContent, nsnull);
  binding->MarkForDeath();
  
  // ...and recreate it's frames. We need to do this since the frames may have
  // been removed and style may have changed due to the removal of the
  // anonymous children.
  // XXXbz this should be using the current doc (if any), not the owner doc.
  nsIPresShell *presShell = doc->GetPrimaryShell();
  NS_ENSURE_TRUE(presShell, NS_ERROR_FAILURE);

  return presShell->RecreateFramesFor(aContent);;
}

nsresult
nsBindingManager::LoadBindingDocument(nsIDocument* aBoundDoc,
                                      nsIURI* aURL,
                                      nsIPrincipal* aOriginPrincipal)
{
  NS_PRECONDITION(aURL, "Must have a URI to load!");
  
  // First we need to load our binding.
  nsresult rv;
  nsCOMPtr<nsIXBLService> xblService = 
           do_GetService("@mozilla.org/xbl;1", &rv);
  if (!xblService)
    return rv;

  // Load the binding doc.
  nsCOMPtr<nsIXBLDocumentInfo> info;
  xblService->LoadBindingDocumentInfo(nsnull, aBoundDoc, aURL,
                                      aOriginPrincipal, PR_TRUE,
                                      getter_AddRefs(info));
  if (!info)
    return NS_ERROR_FAILURE;

  return NS_OK;
}

nsresult
nsBindingManager::AddToAttachedQueue(nsXBLBinding* aBinding)
{
  if (!mAttachedStack.AppendElement(aBinding))
    return NS_ERROR_OUT_OF_MEMORY;

  // If we're in the middle of processing our queue already, don't
  // bother posting the event.
  if (!mProcessingAttachedStack && !mProcessAttachedQueueEvent) {
    mProcessAttachedQueueEvent =
      new nsRunnableMethod<nsBindingManager>(
        this, &nsBindingManager::DoProcessAttachedQueue);
    NS_DispatchToCurrentThread(mProcessAttachedQueueEvent);
  }

  return NS_OK;
}

void
nsBindingManager::DoProcessAttachedQueue()
{
  ProcessAttachedQueue();

  NS_ASSERTION(mAttachedStack.Length() == 0,
               "Shouldn't have pending bindings!");
  
  mProcessAttachedQueueEvent = nsnull;
}

void
nsBindingManager::ProcessAttachedQueue()
{
  if (mProcessingAttachedStack || mAttachedStack.Length() == 0)
    return;

  mProcessingAttachedStack = PR_TRUE;

  PRInt32 lastItem;
  while ((lastItem = mAttachedStack.Length() - 1) >= 0) {
    nsRefPtr<nsXBLBinding> binding = mAttachedStack.ElementAt(lastItem);
    mAttachedStack.RemoveElementAt(lastItem);

    NS_ASSERTION(binding, "null item in attached stack?");
    binding->ExecuteAttachedHandler();
  }

  mProcessingAttachedStack = PR_FALSE;

  NS_ASSERTION(mAttachedStack.Length() == 0, "How did we get here?");
  
  mAttachedStack.Compact();
}

PR_STATIC_CALLBACK(PLDHashOperator)
AccumulateBindingsToDetach(nsISupports *aKey, nsXBLBinding *aBinding,
                           void* aVoidArray)
{
  nsVoidArray* arr = static_cast<nsVoidArray*>(aVoidArray);
  // Hold an owning reference to this binding, just in case
  if (arr->AppendElement(aBinding))
    NS_ADDREF(aBinding);
  return PL_DHASH_NEXT;
}

PR_STATIC_CALLBACK(PRBool)
ExecuteDetachedHandler(void* aBinding, void* aClosure)
{
  NS_PRECONDITION(aBinding, "Null binding in list?");
  nsXBLBinding* binding = static_cast<nsXBLBinding*>(aBinding);
  binding->ExecuteDetachedHandler();
  // Drop our ref to the binding now
  NS_RELEASE(binding);
  return PR_TRUE;
}

void
nsBindingManager::ExecuteDetachedHandlers()
{
  // Walk our hashtable of bindings.
  if (mBindingTable.IsInitialized()) {
    nsVoidArray bindingsToDetach;
    mBindingTable.EnumerateRead(AccumulateBindingsToDetach, &bindingsToDetach);
    bindingsToDetach.EnumerateForwards(ExecuteDetachedHandler, nsnull);
  }
}

nsresult
nsBindingManager::PutXBLDocumentInfo(nsIXBLDocumentInfo* aDocumentInfo)
{
  NS_PRECONDITION(aDocumentInfo, "Must have a non-null documentinfo!");
  
  NS_ENSURE_TRUE(mDocumentTable.IsInitialized() || mDocumentTable.Init(16),
                 NS_ERROR_OUT_OF_MEMORY);

  NS_ENSURE_TRUE(mDocumentTable.Put(aDocumentInfo->DocumentURI(),
                                    aDocumentInfo),
                 NS_ERROR_OUT_OF_MEMORY);

  return NS_OK;
}

void
nsBindingManager::RemoveXBLDocumentInfo(nsIXBLDocumentInfo* aDocumentInfo)
{
  if (mDocumentTable.IsInitialized()) {
    mDocumentTable.Remove(aDocumentInfo->DocumentURI());
  }
}

nsIXBLDocumentInfo*
nsBindingManager::GetXBLDocumentInfo(nsIURI* aURL)
{
  if (!mDocumentTable.IsInitialized())
    return nsnull;

  return mDocumentTable.GetWeak(aURL);
}

nsresult
nsBindingManager::PutLoadingDocListener(nsIURI* aURL, nsIStreamListener* aListener)
{
  NS_PRECONDITION(aListener, "Must have a non-null listener!");
  
  NS_ENSURE_TRUE(mLoadingDocTable.IsInitialized() || mLoadingDocTable.Init(16),
                 NS_ERROR_OUT_OF_MEMORY);
  
  NS_ENSURE_TRUE(mLoadingDocTable.Put(aURL, aListener),
                 NS_ERROR_OUT_OF_MEMORY);

  return NS_OK;
}

nsIStreamListener*
nsBindingManager::GetLoadingDocListener(nsIURI* aURL)
{
  if (!mLoadingDocTable.IsInitialized())
    return nsnull;

  return mLoadingDocTable.GetWeak(aURL);
}

void
nsBindingManager::RemoveLoadingDocListener(nsIURI* aURL)
{
  if (mLoadingDocTable.IsInitialized()) {
    mLoadingDocTable.Remove(aURL);
  }
}

PR_STATIC_CALLBACK(PLDHashOperator)
MarkForDeath(nsISupports *aKey, nsXBLBinding *aBinding, void* aClosure)
{
  if (aBinding->MarkedForDeath())
    return PL_DHASH_NEXT; // Already marked for death.

  nsCAutoString path;
  aBinding->PrototypeBinding()->DocURI()->GetPath(path);

  if (!strncmp(path.get(), "/skin", 5))
    aBinding->MarkForDeath();
  
  return PL_DHASH_NEXT;
}

void
nsBindingManager::FlushSkinBindings()
{
  if (mBindingTable.IsInitialized())
    mBindingTable.EnumerateRead(MarkForDeath, nsnull);
}

// Used below to protect from recurring in QI calls through XPConnect.
struct AntiRecursionData {
  nsIContent* element; 
  REFNSIID iid; 
  AntiRecursionData* next;

  AntiRecursionData(nsIContent* aElement, 
                    REFNSIID aIID, 
                    AntiRecursionData* aNext)
    : element(aElement), iid(aIID), next(aNext) {}
};

nsresult
nsBindingManager::GetBindingImplementation(nsIContent* aContent, REFNSIID aIID,
                                           void** aResult)
{
  *aResult = nsnull;
  nsXBLBinding *binding = GetBinding(aContent);
  if (binding) {
    // The binding should not be asked for nsISupports
    NS_ASSERTION(!aIID.Equals(NS_GET_IID(nsISupports)), "Asking a binding for nsISupports");
    if (binding->ImplementsInterface(aIID)) {
      nsCOMPtr<nsIXPConnectWrappedJS> wrappedJS = GetWrappedJS(aContent);

      if (wrappedJS) {
        // Protect from recurring in QI calls through XPConnect. 
        // This can happen when a second binding is being resolved.
        // At that point a wrappedJS exists, but it doesn't yet know about
        // the iid we are asking for. So, without this protection, 
        // AggregatedQueryInterface would end up recurring back into itself
        // through this code. 
        //
        // With this protection, when we detect the recursion we return 
        // NS_NOINTERFACE in the inner call. The outer call will then fall 
        // through (see below) and build a new chained wrappedJS for the iid.
        //
        // We're careful to not assume that only one direct nesting can occur
        // because there is a call into JS in the middle and we can't assume 
        // that this code won't be reached by some more complex nesting path.
        //
        // NOTE: We *assume* this is single threaded, so we can use a
        // static linked list to do the check.

        static AntiRecursionData* list = nsnull;

        for (AntiRecursionData* p = list; p; p = p->next) {
          if (p->element == aContent && p->iid.Equals(aIID)) {
            *aResult = nsnull;
            return NS_NOINTERFACE;
          }
        }

        AntiRecursionData item(aContent, aIID, list);
        list = &item;

        nsresult rv = wrappedJS->AggregatedQueryInterface(aIID, aResult);
        
        list = item.next;
        
        if (*aResult)
          return rv;
        
        // No result was found, so this must be another XBL interface.
        // Fall through to create a new wrapper.
      }

      // We have never made a wrapper for this implementation.
      // Create an XPC wrapper for the script object and hand it back.

      nsIDocument* doc = aContent->GetOwnerDoc();
      if (!doc)
        return NS_NOINTERFACE;

      nsIScriptGlobalObject *global = doc->GetScriptGlobalObject();
      if (!global)
        return NS_NOINTERFACE;

      nsIScriptContext *context = global->GetContext();
      if (!context)
        return NS_NOINTERFACE;

      JSContext* jscontext = (JSContext*)context->GetNativeContext();
      if (!jscontext)
        return NS_NOINTERFACE;

      nsIXPConnect *xpConnect = nsContentUtils::XPConnect();

      nsCOMPtr<nsIXPConnectWrappedNative> wrapper;
      xpConnect->GetWrappedNativeOfNativeObject(jscontext,
                                                global->GetGlobalJSObject(),
                                                aContent,
                                                NS_GET_IID(nsISupports),
                                                getter_AddRefs(wrapper));
      NS_ENSURE_TRUE(wrapper, NS_NOINTERFACE);

      JSObject* jsobj = nsnull;

      wrapper->GetJSObject(&jsobj);
      NS_ENSURE_TRUE(jsobj, NS_NOINTERFACE);

      nsresult rv = xpConnect->WrapJSAggregatedToNative(aContent, jscontext,
                                                        jsobj, aIID, aResult);
      if (NS_FAILED(rv))
        return rv;

      // We successfully created a wrapper.  We will own this wrapper for as long as the binding remains
      // alive.  At the time the binding is cleared out of the bindingManager, we will remove the wrapper
      // from the bindingManager as well.
      nsISupports* supp = static_cast<nsISupports*>(*aResult);
      wrappedJS = do_QueryInterface(supp);
      SetWrappedJS(aContent, wrappedJS);

      return rv;
    }
  }
  
  *aResult = nsnull;
  return NS_NOINTERFACE;
}

nsresult
nsBindingManager::WalkRules(nsStyleSet* aStyleSet,
                            nsIStyleRuleProcessor::EnumFunc aFunc,
                            RuleProcessorData* aData,
                            PRBool* aCutOffInheritance)
{
  *aCutOffInheritance = PR_FALSE;
  
  if (!aData->mContent)
    return NS_OK;

  // Walk the binding scope chain, starting with the binding attached to our
  // content, up till we run out of scopes or we get cut off.
  nsIContent *content = aData->mContent;
  
  do {
    nsXBLBinding *binding = GetBinding(content);
    if (binding) {
      aData->mScopedRoot = content;
      binding->WalkRules(aFunc, aData);
      // If we're not looking at our original content, allow the binding to cut
      // off style inheritance
      if (content != aData->mContent) {
        if (!binding->InheritsStyle()) {
          // Go no further; we're not inheriting style from anything above here
          break;
        }
      }
    }

    nsIContent* parent = content->GetBindingParent();
    if (parent == content) {
      NS_ASSERTION(content->IsNativeAnonymous() ||
                   content->IsNodeOfType(nsINode::eXUL),
                   "Unexpected binding parent");
                             
      break; // The anonymous content case is often deliberately hacked to
             // return itself to cut off style inheritance here.  Do that.
    }

    content = parent;
  } while (content);

  // If "content" is non-null that means we cut off inheritance at some point
  // in the loop.
  *aCutOffInheritance = (content != nsnull);

  // Null out the scoped root that we set repeatedly
  aData->mScopedRoot = nsnull;

  return NS_OK;
}

PRBool
nsBindingManager::ShouldBuildChildFrames(nsIContent* aContent)
{
  nsXBLBinding *binding = GetBinding(aContent);

  return !binding || binding->ShouldBuildChildFrames();
}

nsIContent*
nsBindingManager::GetNestedInsertionPoint(nsIContent* aParent, nsIContent* aChild)
{
  // Check to see if the content is anonymous.
  if (aChild->GetBindingParent() == aParent)
    return nsnull; // It is anonymous. Don't use the insertion point, since that's only
                   // for the explicit kids.

  PRUint32 index;
  nsIContent *insertionElement = GetInsertionPoint(aParent, aChild, &index);
  if (insertionElement != aParent) {
    // See if we nest even further in.
    nsIContent* nestedPoint = GetNestedInsertionPoint(insertionElement, aChild);
    if (nestedPoint)
      insertionElement = nestedPoint;
  }

  return insertionElement;
}

// Note: We don't hold a reference to the document observer; we assume
// that it has a live reference to the document.
void
nsBindingManager::AddObserver(nsIMutationObserver* aObserver)
{
  // The array makes sure the observer isn't already in the list
  mObservers.AppendObserver(aObserver);
}

PRBool
nsBindingManager::RemoveObserver(nsIMutationObserver* aObserver)
{
  return mObservers.RemoveObserver(aObserver);
}

void
nsBindingManager::CharacterDataChanged(nsIDocument* aDocument,
                                       nsIContent* aContent,
                                       CharacterDataChangeInfo* aInfo)
{
  NS_BINDINGMANAGER_NOTIFY_OBSERVERS(CharacterDataChanged,
                                     (aDocument, aContent, aInfo));
}

void
nsBindingManager::AttributeChanged(nsIDocument* aDocument,
                                   nsIContent* aContent,
                                   PRInt32 aNameSpaceID,
                                   nsIAtom* aAttribute,
                                   PRInt32 aModType,
                                   PRUint32 aStateMask)
{
  NS_BINDINGMANAGER_NOTIFY_OBSERVERS(AttributeChanged,
                                     (aDocument, aContent, aNameSpaceID,
                                      aAttribute, aModType, aStateMask));
}

void
nsBindingManager::ContentAppended(nsIDocument* aDocument,
                                  nsIContent* aContainer,
                                  PRInt32     aNewIndexInContainer)
{
  // XXX This is hacked and not quite correct. See below.
  if (aNewIndexInContainer != -1 &&
      (mContentListTable.ops || mAnonymousNodesTable.ops)) {
    // It's not anonymous.
    PRInt32 childCount = aContainer->GetChildCount();

    nsIContent *child = aContainer->GetChildAt(aNewIndexInContainer);

    nsCOMPtr<nsIContent> ins = GetNestedInsertionPoint(aContainer, child);

    if (ins) {
      nsCOMPtr<nsIDOMNodeList> nodeList;
      PRBool isAnonymousContentList;
      GetXBLChildNodesInternal(ins, getter_AddRefs(nodeList),
                               &isAnonymousContentList);

      if (nodeList && isAnonymousContentList) {
        // Find a non-pseudo-insertion point and just jam ourselves in.
        // This is not 100% correct.  Hack city, baby.
        nsAnonymousContentList* contentList =
          static_cast<nsAnonymousContentList*>(nodeList.get());

        PRInt32 count = contentList->GetInsertionPointCount();
        for (PRInt32 i = 0; i < count; i++) {
          nsXBLInsertionPoint* point = contentList->GetInsertionPointAt(i);
          PRInt32 index = point->GetInsertionIndex();
          if (index != -1) {
            // We're real. Jam all the kids in.
            // XXX Check the filters to find the correct points.
            for (PRInt32 j = aNewIndexInContainer; j < childCount; j++) {
              child = aContainer->GetChildAt(j);
              point->AddChild(child);
              SetInsertionParent(child, ins);
            }
            break;
          }
        }
      }
    }
  }

  NS_BINDINGMANAGER_NOTIFY_OBSERVERS(ContentAppended,
                                     (aDocument, aContainer,
                                      aNewIndexInContainer));
}

void
nsBindingManager::ContentInserted(nsIDocument* aDocument,
                                  nsIContent* aContainer,
                                  nsIContent* aChild,
                                  PRInt32 aIndexInContainer)
{
  // XXX This is hacked just to make menus work again.
  if (aIndexInContainer != -1 &&
      (mContentListTable.ops || mAnonymousNodesTable.ops)) {
    // It's not anonymous.
    nsCOMPtr<nsIContent> ins = GetNestedInsertionPoint(aContainer, aChild);

    if (ins) {
      nsCOMPtr<nsIDOMNodeList> nodeList;
      PRBool isAnonymousContentList;
      GetXBLChildNodesInternal(ins, getter_AddRefs(nodeList),
                               &isAnonymousContentList);

      if (nodeList && isAnonymousContentList) {
        // Find a non-pseudo-insertion point and just jam ourselves in.
        // This is not 100% correct.  Hack city, baby.
        nsAnonymousContentList* contentList =
          static_cast<nsAnonymousContentList*>(nodeList.get());

        PRInt32 count = contentList->GetInsertionPointCount();
        for (PRInt32 i = 0; i < count; i++) {
          nsXBLInsertionPoint* point = contentList->GetInsertionPointAt(i);
          if (point->GetInsertionIndex() != -1) {
            // We're real. Jam the kid in.
            // XXX Check the filters to find the correct points.

            // Find the right insertion spot.  Can't just insert in the insertion
            // point at aIndexInContainer since the point may contain anonymous
            // content, not all of aContainer's kids, etc.  So find the last
            // child of aContainer that comes before aIndexInContainer and is in
            // the insertion point and insert right after it.
            PRInt32 pointSize = point->ChildCount();
            PRBool inserted = PR_FALSE;
            for (PRInt32 parentIndex = aIndexInContainer - 1;
                 parentIndex >= 0 && !inserted; --parentIndex) {
              nsIContent* currentSibling = aContainer->GetChildAt(parentIndex);
              for (PRInt32 pointIndex = pointSize - 1; pointIndex >= 0;
                   --pointIndex) {
                nsCOMPtr<nsIContent> currContent = point->ChildAt(pointIndex);
                if (currContent == currentSibling) {
                  point->InsertChildAt(pointIndex + 1, aChild);
                  inserted = PR_TRUE;
                  break;
                }
              }
            }
            if (!inserted) {
              // None of our previous siblings are in here... just stick
              // ourselves in at the beginning of the insertion point.
              // XXXbz if we ever start doing the filter thing right, this may be
              // no good, since we may _still_ have anonymous kids in there and
              // may need to get the ordering with those right.
              point->InsertChildAt(0, aChild);
            }
            SetInsertionParent(aChild, ins);
            break;
          }
        }
      }
    }
  }

  NS_BINDINGMANAGER_NOTIFY_OBSERVERS(ContentInserted,
                                     (aDocument, aContainer, aChild,
                                      aIndexInContainer));  
}

void
nsBindingManager::ContentRemoved(nsIDocument* aDocument,
                                 nsIContent* aContainer,
                                 nsIContent* aChild,
                                 PRInt32 aIndexInContainer)
{
  if (aIndexInContainer != -1 &&
      (mContentListTable.ops || mAnonymousNodesTable.ops)) {
    // It's not anonymous
    nsCOMPtr<nsIContent> point = GetNestedInsertionPoint(aContainer, aChild);

    if (point) {
      nsCOMPtr<nsIDOMNodeList> nodeList;
      PRBool isAnonymousContentList;
      GetXBLChildNodesInternal(point, getter_AddRefs(nodeList),
                               &isAnonymousContentList);
      
      if (nodeList && isAnonymousContentList) {
        // Find a non-pseudo-insertion point and remove ourselves.
        nsAnonymousContentList* contentList = static_cast<nsAnonymousContentList*>(static_cast<nsIDOMNodeList*>(nodeList));
        PRInt32 count = contentList->GetInsertionPointCount();
        for (PRInt32 i =0; i < count; i++) {
          nsXBLInsertionPoint* point = contentList->GetInsertionPointAt(i);
          if (point->GetInsertionIndex() != -1) {
            point->RemoveChild(aChild);
          }
        }
      }
    }  
  }

  NS_BINDINGMANAGER_NOTIFY_OBSERVERS(ContentRemoved,
                                     (aDocument, aContainer, aChild,
                                      aIndexInContainer));  


}

void
nsBindingManager::NodeWillBeDestroyed(const nsINode *aNode)
{
  // Make sure to not run any more XBL constructors
  mProcessingAttachedStack = PR_TRUE;
  
  NS_BINDINGMANAGER_NOTIFY_OBSERVERS(NodeWillBeDestroyed, (aNode));
}

void
nsBindingManager::ParentChainChanged(nsIContent *aContent)
{
  NS_BINDINGMANAGER_NOTIFY_OBSERVERS(ParentChainChanged, (aContent));
}

void
nsBindingManager::Traverse(nsIContent *aContent,
                           nsCycleCollectionTraversalCallback &cb)
{
  if (!aContent->HasFlag(NODE_MAY_BE_IN_BINDING_MNGR)) {
    return;
  }

  nsXBLBinding *binding = GetBinding(aContent);
  if (binding) {
    cb.NoteXPCOMChild(aContent);
    NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NATIVE_PTR(binding, nsXBLBinding)
  }
  nsISupports *value;
  if (mContentListTable.ops &&
      (value = LookupObject(mContentListTable, aContent))) {
    cb.NoteXPCOMChild(aContent);
    cb.NoteXPCOMChild(value);
  }
  if (mAnonymousNodesTable.ops &&
      (value = LookupObject(mAnonymousNodesTable, aContent))) {
    cb.NoteXPCOMChild(aContent);
    cb.NoteXPCOMChild(value);
  }
  if (mInsertionParentTable.ops &&
      (value = LookupObject(mInsertionParentTable, aContent))) {
    cb.NoteXPCOMChild(aContent);
    cb.NoteXPCOMChild(value);
  }
  if (mWrapperTable.ops &&
      (value = LookupObject(mWrapperTable, aContent))) {
    cb.NoteXPCOMChild(aContent);
    cb.NoteXPCOMChild(value);
  }
}

void
nsBindingManager::BeginOutermostUpdate()
{
  mProcessOnEndUpdate = (mAttachedStack.Length() == 0);
}

void
nsBindingManager::EndOutermostUpdate()
{
  if (mProcessOnEndUpdate) {
    mProcessOnEndUpdate = PR_FALSE;
    ProcessAttachedQueue();
  }
}
