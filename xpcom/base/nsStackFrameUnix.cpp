/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
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
 * The Original Code is nsStackFrameWin.h code, released
 * December 20, 2000.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2003
 * the Initial Developer. All Rights Reserved.
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

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "nscore.h"
#include <stdio.h>

// On glibc 2.1, the Dl_info api defined in <dlfcn.h> is only exposed
// if __USE_GNU is defined.  I suppose its some kind of standards
// adherence thing.
//
#if (__GLIBC_MINOR__ >= 1) && !defined(__USE_GNU)
#define __USE_GNU
#endif

#ifdef HAVE_LIBDL
#include <dlfcn.h>
#endif



// This thing is exported by libstdc++
// Yes, this is a gcc only hack
#if defined(MOZ_DEMANGLE_SYMBOLS)
#include <cxxabi.h>
#include <stdlib.h> // for free()
#endif // MOZ_DEMANGLE_SYMBOLS

void DemangleSymbol(const char * aSymbol, 
                    char * aBuffer,
                    int aBufLen)
{
    aBuffer[0] = '\0';

#if defined(MOZ_DEMANGLE_SYMBOLS)
    /* See demangle.h in the gcc source for the voodoo */
    char * demangled = abi::__cxa_demangle(aSymbol,0,0,0);
    
    if (demangled)
    {
        strncpy(aBuffer,demangled,aBufLen);
        free(demangled);
    }
#endif // MOZ_DEMANGLE_SYMBOLS
}


#if defined(linux) && defined(__GNUC__) && (defined(__i386) || defined(PPC) || defined(__x86_64__)) // i386 or PPC Linux stackwalking code


EXPORT_XPCOM_API(nsresult)
NS_StackWalk(NS_WalkStackCallback aCallback, PRUint32 aSkipFrames,
             void *aClosure)
{
  // Stack walking code courtesy Kipp's "leaky".
  char buf[512];

  // Get the frame pointer
  void **bp;
#if defined(__i386) 
  __asm__( "movl %%ebp, %0" : "=g"(bp));
#elif defined(__x86_64__)
  __asm__( "movq %%rbp, %0" : "=g"(bp));
#else
  // It would be nice if this worked uniformly, but at least on i386 and
  // x86_64, it stopped working with gcc 4.1, because it points to the
  // end of the saved registers instead of the start.
  bp = (void**) __builtin_frame_address(0);
#endif

  int skip = aSkipFrames;
  for ( ; (void**)*bp > bp; bp = (void**)*bp) {
    void *pc = *(bp+1);
    if (--skip < 0) {
      Dl_info info;
      int ok = dladdr(pc, &info);
      if (!ok) {
        snprintf(buf, sizeof(buf), "UNKNOWN %p\n", pc);
        (*aCallback)(buf, aClosure);
        continue;
      }

      PRUint32 foff = (char*)pc - (char*)info.dli_fbase;

      const char * symbol = info.dli_sname;
      int len;
      if (!symbol || !(len = strlen(symbol))) {
        snprintf(buf, sizeof(buf), "UNKNOWN [%s +0x%08X]\n",
                                   info.dli_fname, foff);
        (*aCallback)(buf, aClosure);
        continue;
      }

      char demangled[4096] = "\0";

      DemangleSymbol(symbol, demangled, sizeof(demangled));

      if (strlen(demangled)) {
        symbol = demangled;
        len = strlen(symbol);
      }

      PRUint32 off = (char*)pc - (char*)info.dli_saddr;
      snprintf(buf, sizeof(buf), "%s+0x%08X [%s +0x%08X]\n",
                                 symbol, off, info.dli_fname, foff);
      (*aCallback)(buf, aClosure);
    }
  }
  return NS_OK;
}

#elif defined(__sun) && (defined(__sparc) || defined(sparc) || defined(__i386) || defined(i386))

/*
 * Stack walking code for Solaris courtesy of Bart Smaalder's "memtrak".
 */

#include <synch.h>
#include <ucontext.h>
#include <sys/frame.h>
#include <sys/regset.h>
#include <sys/stack.h>

static int    load_address ( void * pc, void * arg );
static struct bucket * newbucket ( void * pc );
static struct frame * cs_getmyframeptr ( void );
static void   cs_walk_stack ( void * (*read_func)(char * address),
                              struct frame * fp,
                              int (*operate_func)(void *, void *),
                              void * usrarg );
static void   cs_operate ( void (*operate_func)(void *, void *),
                           void * usrarg );

#ifndef STACK_BIAS
#define STACK_BIAS 0
#endif /*STACK_BIAS*/

#define LOGSIZE 4096

/* type of demangling function */
typedef int demf_t(const char *, char *, size_t);

static demf_t *demf;

static int initialized = 0;

#if defined(sparc) || defined(__sparc)
#define FRAME_PTR_REGISTER REG_SP
#endif

#if defined(i386) || defined(__i386)
#define FRAME_PTR_REGISTER EBP
#endif

struct bucket {
    void * pc;
    int index;
    struct bucket * next;
};

struct my_user_args {
    NS_WalkStackCallback callback;
    PRUint32 skipFrames;
    void *closure;
};


static void myinit();

#pragma init (myinit)

static void
myinit()
{

    if (! initialized) {
#ifndef __GNUC__
        void *handle;
        const char *libdem = "libdemangle.so.1";

        /* load libdemangle if we can and need to (only try this once) */
        if ((handle = dlopen(libdem, RTLD_LAZY)) != NULL) {
            demf = (demf_t *)dlsym(handle,
                           "cplus_demangle"); /*lint !e611 */
                /*
                 * lint override above is to prevent lint from
                 * complaining about "suspicious cast".
                 */
        }
#endif /*__GNUC__*/
    }    
    initialized = 1;
}


static int
load_address(void * pc, void * arg )
{
    static struct bucket table[2048];
    static mutex_t lock;
    struct bucket * ptr;
    struct my_user_args * args = (struct my_user_args *) arg;

    unsigned int val = NS_PTR_TO_INT32(pc);

    ptr = table + ((val >> 2)&2047);

    mutex_lock(&lock);
    while (ptr->next) {
        if (ptr->next->pc == pc)
            break;
        ptr = ptr->next;
    }

    if (ptr->next) {
        mutex_unlock(&lock);
    } else {
        char buffer[4096], dembuff[4096];
        Dl_info info;
        const char *func = "??", *lib = "??";

        ptr->next = newbucket(pc);
        mutex_unlock(&lock);
 
        if (dladdr(pc, & info)) {
            if (info.dli_fname)
                lib =  info.dli_fname;
            if (info.dli_sname)
                func = info.dli_sname;
        }
 
#ifdef __GNUC__
        DemangleSymbol(func, dembuff, sizeof(dembuff));
#else
        if (!demf || demf(func, dembuff, sizeof (dembuff)))
            dembuff[0] = 0;
#endif /*__GNUC__*/
        if (strlen(dembuff)) {
            func = dembuff;
        }
        snprintf(buffer, sizeof(buffer), "%u %s:%s+0x%x\n",
                 ptr->next->index, lib, func,
                 (char *)pc - (char*)info.dli_saddr);
        (*args.callback)(buffer, args.closure);
    }
    return 0;
}


static struct bucket *
newbucket(void * pc)
{
    struct bucket * ptr = (struct bucket *) malloc(sizeof (*ptr));
    static int index; /* protected by lock in caller */
                     
    ptr->index = index++;
    ptr->next = NULL;
    ptr->pc = pc;    
    return (ptr);    
}


static struct frame *
csgetframeptr()
{
    ucontext_t u;
    struct frame *fp;

    (void) getcontext(&u);

    fp = (struct frame *)
        ((char *)u.uc_mcontext.gregs[FRAME_PTR_REGISTER] +
        STACK_BIAS);

    /* make sure to return parents frame pointer.... */

    return ((struct frame *)((ulong_t)fp->fr_savfp + STACK_BIAS));
}


static void
cswalkstack(struct frame *fp, int (*operate_func)(void *, void *, FILE *),
    void *usrarg)
{

    while (fp != 0 && fp->fr_savpc != 0) {

        if (operate_func((void *)fp->fr_savpc, usrarg) != 0)
            break;
        /*
         * watch out - libthread stacks look funny at the top
         * so they may not have their STACK_BIAS set
         */

        fp = (struct frame *)((ulong_t)fp->fr_savfp +
            (fp->fr_savfp?(ulong_t)STACK_BIAS:0));
    }
}


static void
cs_operate(int (*operate_func)(void *, void *, FILE *), void * usrarg)
{
    cswalkstack(csgetframeptr(), operate_func, usrarg);
}

EXPORT_XPCOM_API(nsresult)
NS_StackWalk(NS_WalkStackCallback aCallback, PRUint32 aSkipFrames,
             void *aClosure)
{
    struct my_user_args args;

    if (!initialized)
        myinit();

    args.callback = aCallback;
    args.skipFrames = aSkipFrames; /* XXX Not handled! */
    args.closure = aClosure;
    cs_operate(load_address, &args);
    return NS_OK;
}
#endif
