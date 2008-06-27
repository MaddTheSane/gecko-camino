/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sw=4 et tw=78:
 *
 * ***** BEGIN LICENSE BLOCK *****
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
 * The Original Code is Mozilla SpiderMonkey JavaScript 1.9 code, released
 * May 28, 2008.
 *
 * The Initial Developer of the Original Code is
 *   Brendan Eich <brendan@mozilla.org
 *
 * Contributor(s):
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

#ifndef jstracerinlines_h___
#define jstracerinlines_h___

#define PRIMITIVE(x) interp_##x

#include "jsinterpinlines.h"

#undef PRIMITIVE

#define G(ok)           (ok ? LIR_xf : LIR_xt)

static inline TraceRecorder*
recorder(JSContext* cx) 
{
    return JS_TRACE_MONITOR(cx).recorder;
}

static inline SideExit*
snapshot(JSContext* cx, JSFrameRegs& regs, SideExit& exit)
{
    memset(&exit, 0, sizeof(exit));
    exit.from = recorder(cx)->fragment;
    return &exit;
}

static inline void
prim_copy(JSContext* cx, jsval& from, jsval& to)
{
    interp_prim_copy(cx, from, to);
    recorder(cx)->copy(&from, &to);
}

static inline void
prim_push_stack(JSContext* cx, JSFrameRegs& regs, jsval& v)
{
    recorder(cx)->set(regs.sp, recorder(cx)->get(&v));
    interp_prim_push_stack(cx, regs, v);
}

static inline void
prim_pop_stack(JSContext* cx, JSFrameRegs& regs, jsval& v)
{
    interp_prim_pop_stack(cx, regs, v);
    recorder(cx)->set(&v, recorder(cx)->get(regs.sp));
}

static inline void
prim_store_stack(JSContext* cx, JSFrameRegs& regs, int n, jsval& v)
{
    interp_prim_store_stack(cx, regs, n, v);
    recorder(cx)->set(&regs.sp[n], recorder(cx)->get(&v));
}

static inline void
prim_fetch_stack(JSContext* cx, JSFrameRegs& regs, int n, jsval& v)
{
    interp_prim_fetch_stack(cx, regs, n, v);
    recorder(cx)->set(&v, recorder(cx)->get(&regs.sp[n]));
}

static inline void
prim_adjust_stack(JSContext* cx, JSFrameRegs& regs, int n)
{
    interp_prim_adjust_stack(cx, regs, n);
}

static inline void
prim_generate_constant(JSContext* cx, jsval c, jsval& v)
{
    interp_prim_generate_constant(cx, c, v);
    if (JSVAL_IS_DOUBLE(c)) {
        jsdouble d = *JSVAL_TO_DOUBLE(c);
        recorder(cx)->set(&v, recorder(cx)->lir->insImmq(*(uint64_t*)&d));
    } else {
        int32_t x;
        if (JSVAL_IS_BOOLEAN(c)) {
            x = JSVAL_TO_BOOLEAN(c);
        } else if (JSVAL_IS_INT(c)) {
            x = JSVAL_TO_INT(c);
        } else if (JSVAL_IS_STRING(c)) {
            x = (int32_t)JSVAL_TO_STRING(c);
        } else {
            JS_ASSERT(JSVAL_IS_OBJECT(c));
            x = (int32_t)JSVAL_TO_OBJECT(c);
        }
        recorder(cx)->set(&v, recorder(cx)->lir->insImm(x));
    }
}

static inline void
prim_boolean_to_jsval(JSContext* cx, JSBool& b, jsval& v)
{
    interp_prim_boolean_to_jsval(cx, b, v);
    recorder(cx)->copy(&b, &v);
}

static inline void
prim_string_to_jsval(JSContext* cx, JSString*& str, jsval& v)
{
    interp_prim_string_to_jsval(cx, str, v);
    recorder(cx)->copy(&str, &v);
}

static inline void
prim_object_to_jsval(JSContext* cx, JSObject*& obj, jsval& v)
{
    interp_prim_object_to_jsval(cx, obj, v);
    recorder(cx)->copy(&obj, &v);
}

static inline void
prim_id_to_jsval(JSContext* cx, jsid& id, jsval& v)
{
    interp_prim_id_to_jsval(cx, id, v);
    recorder(cx)->copy(&id, &v);
}

static inline bool
guard_jsdouble_is_int_and_int_fits_in_jsval(JSContext* cx, JSFrameRegs& regs, jsdouble& d, jsint& i)
{
    bool ok = interp_guard_jsdouble_is_int_and_int_fits_in_jsval(cx, regs, d, i);
    /* We do not box in trace code, so ints always fit and we only check
       that it is actually an int. */
    recorder(cx)->call(F_DOUBLE_IS_INT, &d, &i);
    SideExit exit;
    recorder(cx)->lir->insGuard(G(ok),
            recorder(cx)->get(&i),
            snapshot(cx, regs, exit));
    return ok;
}

static inline void
prim_int_to_jsval(JSContext* cx, jsint& i, jsval& v)
{
    interp_prim_int_to_jsval(cx, i, v);
    recorder(cx)->copy(&i, &v);
}

static inline bool
call_NewDoubleInRootedValue(JSContext* cx, jsdouble& d, jsval& v)
{
    bool ok = interp_call_NewDoubleInRootedValue(cx, d, v);
    recorder(cx)->copy(&d, &v);
    return ok;
}

static inline bool
guard_int_fits_in_jsval(JSContext* cx, JSFrameRegs& regs, jsint& i)
{
    bool ok = interp_guard_int_fits_in_jsval(cx, regs, i);
    return ok;
}

static inline void
prim_int_to_double(JSContext* cx, jsint& i, jsdouble& d)
{
    interp_prim_int_to_double(cx, i, d);
    recorder(cx)->unary(LIR_i2f, &i, &d);
}

static inline bool
guard_uint_fits_in_jsval(JSContext* cx, JSFrameRegs& regs, uint32& u)
{
    bool ok = interp_guard_uint_fits_in_jsval(cx, regs, u);
    return ok;
}

static inline void
prim_uint_to_jsval(JSContext* cx, uint32& u, jsval& v)
{
    interp_prim_uint_to_jsval(cx, u, v);
    recorder(cx)->copy(&u, &v);
}

static inline void
prim_uint_to_double(JSContext* cx, uint32& u, jsdouble& d)
{
    interp_prim_uint_to_double(cx, u, d);
    recorder(cx)->unary(LIR_u2f, &u, &d);
}

static inline bool
guard_jsval_is_int(JSContext* cx, JSFrameRegs& regs, jsval& v)
{
    bool ok = interp_guard_jsval_is_int(cx, regs, v);
    return ok;
}

static inline void
prim_jsval_to_int(JSContext* cx, jsval& v, jsint& i)
{
    interp_prim_jsval_to_int(cx, v, i);
    recorder(cx)->copy(&v, &i);
}

static inline bool
guard_jsval_is_double(JSContext* cx, JSFrameRegs& regs, jsval& v)
{
    bool ok = interp_guard_jsval_is_double(cx, regs, v);
    return ok;
}

static inline void
prim_jsval_to_double(JSContext* cx, jsval& v, jsdouble& d)
{
    interp_prim_jsval_to_double(cx, v, d);
    recorder(cx)->copy(&v, &d);
}

static inline void
call_ValueToNumber(JSContext* cx, jsval& v, jsdouble& d)
{
    interp_call_ValueToNumber(cx, v, d);
    recorder(cx)->call(F_ValueToNumber, cx, &v, &d);
}

static inline bool
guard_jsval_is_null(JSContext* cx, JSFrameRegs& regs, jsval& v)
{
    bool ok = interp_guard_jsval_is_null(cx, regs, v);
    if (JSVAL_IS_OBJECT(v)) {
        SideExit exit;
        recorder(cx)->lir->insGuard(G(ok),
                recorder(cx)->lir->ins_eq0(recorder(cx)->get(&v)),
                snapshot(cx, regs, exit));
    }
    return ok;
}

static inline void
call_ValueToECMAInt32(JSContext* cx, jsval& v, jsint& i)
{
    interp_call_ValueToECMAInt32(cx, v, i);
    recorder(cx)->call(F_ValueToECMAInt32, cx, &v, &i);
}

static inline void
prim_int_to_uint(JSContext* cx, jsint& i, uint32& u)
{
    interp_prim_int_to_uint(cx, i, u);
    recorder(cx)->copy(&i, &u);
}

static inline void
call_ValueToECMAUint32(JSContext* cx, jsval& v, uint32& u)
{
    interp_call_ValueToECMAUint32(cx, v, u);
    recorder(cx)->call(F_ValueToECMAUint32, cx, &v, &u);
}

static inline void
prim_generate_boolean_constant(JSContext* cx, JSBool c, JSBool& b)
{
    interp_prim_generate_boolean_constant(cx, c, b);
    recorder(cx)->set(&b, recorder(cx)->lir->insImm(c));
}

static inline bool
guard_jsval_is_boolean(JSContext* cx, JSFrameRegs& regs, jsval& v)
{
    bool ok = interp_guard_jsval_is_boolean(cx, regs, v);
    return ok;
}

static inline void
prim_jsval_to_boolean(JSContext* cx, jsval& v, JSBool& b)
{
    interp_prim_jsval_to_boolean(cx, v, b);
    recorder(cx)->copy(&v, &b);
}

static inline void
call_ValueToBoolean(JSContext* cx, jsval& v, JSBool& b)
{
    interp_call_ValueToBoolean(cx, v, b);
    recorder(cx)->call(F_ValueToBoolean, cx, &v, &b);
}

static inline bool
guard_jsval_is_primitive(JSContext* cx, JSFrameRegs& regs, jsval& v)
{
    bool ok = interp_guard_jsval_is_primitive(cx, regs, v);
    return ok;
}

static inline void
prim_jsval_to_object(JSContext* cx, jsval& v, JSObject*& obj)
{
    interp_prim_jsval_to_object(cx, v, obj);
    recorder(cx)->copy(&v, &obj);
}

static inline bool
guard_obj_is_null(JSContext* cx, JSFrameRegs& regs, JSObject*& obj)
{
    bool ok = interp_guard_obj_is_null(cx, regs, obj);
    SideExit exit;
    recorder(cx)->lir->insGuard(G(ok),
            recorder(cx)->lir->ins_eq0(recorder(cx)->get(&obj)),
            snapshot(cx, regs, exit));
    return ok;
}

static inline void
call_ValueToNonNullObject(JSContext* cx, jsval& v, JSObject*& obj)
{
    interp_call_ValueToNonNullObject(cx, v, obj);
    recorder(cx)->call(F_ValueToNonNullObject, cx, &v, &obj);
}

static inline bool
call_obj_default_value(JSContext* cx, JSObject*& obj, JSType hint,
                       jsval& v)
{
    bool ok = interp_call_obj_default_value(cx, obj, hint, v);
    recorder(cx)->call(F_obj_default_value, cx, &obj, recorder(cx)->lir->insImm(hint), &v);
    return ok;
}

static inline void
prim_dadd(JSContext* cx, jsdouble& a, jsdouble& b, jsdouble& r)
{
    interp_prim_dadd(cx, a, b, r);
    recorder(cx)->binary(LIR_fadd, &a, &b, &r);
}

static inline void
prim_dsub(JSContext* cx, jsdouble& a, jsdouble& b, jsdouble& r)
{
    interp_prim_dsub(cx, a, b, r);
    recorder(cx)->binary(LIR_fsub, &a, &b, &r);
}

static inline void
prim_dmul(JSContext* cx, jsdouble& a, jsdouble& b, jsdouble& r)
{
    interp_prim_dmul(cx, a, b, r);
    recorder(cx)->binary(LIR_fmul, &a, &b, &r);
}

static inline bool
prim_ddiv(JSContext* cx, JSRuntime* rt, JSFrameRegs& regs, int n,
                     jsdouble& a, jsdouble& b)
{
    bool ok = interp_prim_ddiv(cx, rt, regs, n, a, b);
    recorder(cx)->binary(LIR_fdiv, &a, &b, &regs.sp[n]);
    return ok;
}

static inline bool
prim_dmod(JSContext* cx, JSRuntime* rt, JSFrameRegs& regs, int n,
                     jsdouble& a, jsdouble& b)
{
    bool ok = interp_prim_dmod(cx, rt, regs, n, a, b);
    recorder(cx)->call(F_dmod, &a, &b, &regs.sp[n]);
    return ok;
}

static inline void
prim_ior(JSContext* cx, jsint& a, jsint& b, jsint& r)
{
    interp_prim_ior(cx, a, b, r);
    recorder(cx)->binary(LIR_or, &a, &b, &r);
}

static inline void
prim_ixor(JSContext* cx, jsint& a, jsint& b, jsint& r)
{
    interp_prim_ixor(cx, a, b, r);
    recorder(cx)->binary(LIR_xor, &a, &b, &r);
}

static inline void
prim_iand(JSContext* cx, jsint& a, jsint& b, jsint& r)
{
    interp_prim_iand(cx, a, b, r);
    recorder(cx)->binary(LIR_and, &a, &b, &r);
}

static inline void
prim_ilsh(JSContext* cx, jsint& a, jsint& b, jsint& r)
{
    interp_prim_ilsh(cx, a, b, r);
    recorder(cx)->binary(LIR_lsh, &a, &b, &r);
}

static inline void
prim_irsh(JSContext* cx, jsint& a, jsint& b, jsint& r)
{
    interp_prim_irsh(cx, a, b, r);
    recorder(cx)->binary(LIR_rsh, &a, &b, &r);
}

static inline void
prim_ursh(JSContext* cx, uint32& a, jsint& b, uint32& r)
{
    interp_prim_ursh(cx, a, b, r);
    recorder(cx)->binary(LIR_ush, &a, &b, &r);
}

static inline bool
guard_boolean_is_true(JSContext* cx, JSFrameRegs& regs, JSBool& cond)
{
    bool ok = interp_guard_boolean_is_true(cx, regs, cond);
    SideExit exit;
    recorder(cx)->lir->insGuard(G(ok),
            recorder(cx)->lir->ins_eq0(recorder(cx)->get(&cond)),
            snapshot(cx, regs, exit));
    return ok;
}

static inline void
prim_icmp_lt(JSContext* cx, jsint& a, jsint& b, JSBool& r)
{
    interp_prim_icmp_lt(cx, a, b, r);
    recorder(cx)->binary(LIR_lt, &a, &b, &r);
}

static inline void
prim_icmp_le(JSContext* cx, jsint& a, jsint& b, JSBool& r)
{
    interp_prim_icmp_le(cx, a, b, r);
    recorder(cx)->binary(LIR_le, &a, &b, &r);
}

static inline void
prim_icmp_gt(JSContext* cx, jsint& a, jsint& b, JSBool& r)
{
    interp_prim_icmp_gt(cx, a, b, r);
    recorder(cx)->binary(LIR_gt, &a, &b, &r);
}

static inline void
prim_icmp_ge(JSContext* cx, jsint& a, jsint& b, JSBool& r)
{
    interp_prim_icmp_ge(cx, a, b, r);
    recorder(cx)->binary(LIR_ge, &a, &b, &r);
}

static inline void
prim_dcmp_lt(JSContext* cx, bool ifnan, jsdouble& a, jsdouble& b, JSBool& r)
{
    interp_prim_dcmp_lt(cx, ifnan, a, b, r);
    recorder(cx)->binary(LIR_lt, &a, &b, &r); // TODO: check ifnan handling
}

static inline void
prim_dcmp_le(JSContext* cx, bool ifnan, jsdouble& a, jsdouble& b, JSBool& r)
{
    interp_prim_dcmp_le(cx, ifnan, a, b, r);
    recorder(cx)->binary(LIR_le, &a, &b, &r); // TODO: check ifnan handling
}

static inline void
prim_dcmp_gt(JSContext* cx, bool ifnan, jsdouble& a, jsdouble& b, JSBool& r)
{
    interp_prim_dcmp_gt(cx, ifnan, a, b, r);
    recorder(cx)->binary(LIR_gt, &a, &b, &r); // TODO: check ifnan handling
}

static inline void
prim_dcmp_ge(JSContext* cx, bool ifnan, jsdouble& a, jsdouble& b, JSBool& r)
{
    interp_prim_dcmp_ge(cx, ifnan, a, b, r);
    recorder(cx)->binary(LIR_ge, &a, &b, &r); // TODO: check ifnan handling
}

static inline void
prim_generate_int_constant(JSContext* cx, jsint c, jsint& i)
{
    interp_prim_generate_int_constant(cx, c, i);
    recorder(cx)->set(&i, recorder(cx)->lir->insImm(c));
}

static inline void
prim_jsval_to_string(JSContext* cx, jsval& v, JSString*& str)
{
    interp_prim_jsval_to_string(cx, v, str);
    recorder(cx)->copy(&v, &str);
}

static inline void
call_CompareStrings(JSContext* cx, JSString*& a, JSString*& b, jsint& r)
{
    interp_call_CompareStrings(cx, a, b, r);
    recorder(cx)->call(F_CompareStrings, &a, &b, &r);
}

static inline bool
guard_both_jsvals_are_int(JSContext* cx, JSFrameRegs& regs, jsval& a, jsval& b)
{
    bool ok = interp_guard_both_jsvals_are_int(cx, regs, a, b);
    return ok;
}

static inline bool
guard_both_jsvals_are_string(JSContext* cx, JSFrameRegs& regs, jsval& a, jsval& b)
{
    bool ok = interp_guard_both_jsvals_are_string(cx, regs, a, b);
    return ok;
}

static inline bool
guard_can_do_fast_inc_dec(JSContext* cx, JSFrameRegs& regs, jsval& v)
{
    bool ok = interp_guard_can_do_fast_inc_dec(cx, regs, v);
    /* to do a fast inc/dec the topmost two bits must be both 0 or 1 */
    SideExit exit;
    TraceRecorder* r = recorder(cx);
    r->lir->insGuard(G(ok),
            r->lir->ins_eq0(
                    r->lir->ins2(LIR_and,
                            r->lir->ins2(LIR_xor,
                                    r->lir->ins2i(LIR_lsh, recorder(cx)->get(&v), 1),
                                    recorder(cx)->get(&v)),
                            r->lir->insImm(0x80000000))),
            snapshot(cx, regs, exit));
    return ok;
}

static inline void
prim_generate_double_constant(JSContext* cx, jsdouble c, jsdouble& d)
{
    interp_prim_generate_double_constant(cx, c, d);
    recorder(cx)->set(&d, recorder(cx)->lir->insImmq(*(uint64_t*)&c));
}

static inline void
prim_do_fast_inc_dec(JSContext* cx, jsval& a, jsval incr, jsval& r)
{
    interp_prim_do_fast_inc_dec(cx, a, incr, r);
    recorder(cx)->iinc(&a, incr/2, &r);
}

#undef G

#endif /* jstracerinlines_h___ */
