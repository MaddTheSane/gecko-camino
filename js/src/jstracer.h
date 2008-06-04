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

#ifndef jstracer_h___
#define jstracer_h___

#include "jsstddef.h"
#include "jslock.h"

/* 
 * Trace monitor. Every runtime is associated with a trace monitor that
 * keeps track of loop frequencies for all JavaScript code loaded into
 * that runtime. For this we use a loop table. Entries in the loop
 * table are requested by jsemit.c during compilation. By using atomic
 * pre-increment obtaining the next index is lock free, but to allocate
 * more table space the trace monitor lock has to be aquired first.
 * 
 * The loop table also doubles as tree pointer table once a loop 
 * achieves a certain number of iterations and we recorded a tree for
 * that loop.
 */
struct JSTraceMonitor {
    jsval               *loopTable;
    uint32              loopTableSize;
    JSScript            *recorderScript;
    JSObject            *recorderScriptObject;
};

#define TRACE_THRESHOLD 10

uint32 js_AllocateLoopTableSlot(JSRuntime *rt);
void   js_FreeLoopTableSlot(JSRuntime *rt, uint32 slot);
JSBool js_GrowLoopTable(JSContext *cx, uint32 index);

#endif /* jstracer_h___ */
