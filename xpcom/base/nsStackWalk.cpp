/* vim: set shiftwidth=4 tabstop=8 autoindent cindent expandtab: */
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
 * The Original Code is Mozilla stack walking code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2000
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Michael Judge, 20-December-2000
 *   L. David Baron <dbaron@dbaron.org>, Mozilla Corporation
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

/* API for getting a stack trace of the C/C++ stack on the current thread */

#include "nsStackWalk.h"

#if defined(_WIN32) && (defined(_M_IX86) || defined(_M_AMD64) || defined(_M_IA64)) && !defined(WINCE) // WIN32 x86 stack walking code

#include "nscore.h"
#include <windows.h>
#include <stdio.h>
#include "plstr.h"

#include "nspr.h"
#ifdef _M_IX86
#include <imagehlp.h>
// We need a way to know if we are building for WXP (or later), as if we are, we
// need to use the newer 64-bit APIs. API_VERSION_NUMBER seems to fit the bill.
// A value of 9 indicates we want to use the new APIs.
#if API_VERSION_NUMBER >= 9
#define USING_WXP_VERSION 1
#endif
#endif

// Define these as static pointers so that we can load the DLL on the
// fly (and not introduce a link-time dependency on it). Tip o' the
// hat to Matt Pietrick for this idea. See:
//
//   http://msdn.microsoft.com/library/periodic/period97/F1/D3/S245C6.htm
//
PR_BEGIN_EXTERN_C

typedef DWORD (__stdcall *SYMSETOPTIONSPROC)(DWORD);
extern SYMSETOPTIONSPROC _SymSetOptions;

typedef BOOL (__stdcall *SYMINITIALIZEPROC)(HANDLE, LPSTR, BOOL);
extern SYMINITIALIZEPROC _SymInitialize;

typedef BOOL (__stdcall *SYMCLEANUPPROC)(HANDLE);
extern SYMCLEANUPPROC _SymCleanup;

typedef BOOL (__stdcall *STACKWALKPROC)(DWORD,
                                        HANDLE,
                                        HANDLE,
                                        LPSTACKFRAME,
                                        LPVOID,
                                        PREAD_PROCESS_MEMORY_ROUTINE,
                                        PFUNCTION_TABLE_ACCESS_ROUTINE,
                                        PGET_MODULE_BASE_ROUTINE,
                                        PTRANSLATE_ADDRESS_ROUTINE);
extern  STACKWALKPROC _StackWalk;

#ifdef USING_WXP_VERSION
typedef BOOL (__stdcall *STACKWALKPROC64)(DWORD,
                                          HANDLE,
                                          HANDLE,
                                          LPSTACKFRAME64,
                                          PVOID,
                                          PREAD_PROCESS_MEMORY_ROUTINE64,
                                          PFUNCTION_TABLE_ACCESS_ROUTINE64,
                                          PGET_MODULE_BASE_ROUTINE64,
                                          PTRANSLATE_ADDRESS_ROUTINE64);
extern  STACKWALKPROC64 _StackWalk64;
#endif

typedef LPVOID (__stdcall *SYMFUNCTIONTABLEACCESSPROC)(HANDLE, DWORD);
extern  SYMFUNCTIONTABLEACCESSPROC _SymFunctionTableAccess;

#ifdef USING_WXP_VERSION
typedef LPVOID (__stdcall *SYMFUNCTIONTABLEACCESSPROC64)(HANDLE, DWORD64);
extern  SYMFUNCTIONTABLEACCESSPROC64 _SymFunctionTableAccess64;
#endif

typedef DWORD (__stdcall *SYMGETMODULEBASEPROC)(HANDLE, DWORD);
extern  SYMGETMODULEBASEPROC _SymGetModuleBase;

#ifdef USING_WXP_VERSION
typedef DWORD64 (__stdcall *SYMGETMODULEBASEPROC64)(HANDLE, DWORD64);
extern  SYMGETMODULEBASEPROC64 _SymGetModuleBase64;
#endif

typedef BOOL (__stdcall *SYMGETSYMFROMADDRPROC)(HANDLE, DWORD, PDWORD, PIMAGEHLP_SYMBOL);
extern  SYMGETSYMFROMADDRPROC _SymGetSymFromAddr;

#ifdef USING_WXP_VERSION
typedef BOOL (__stdcall *SYMFROMADDRPROC)(HANDLE, DWORD64, PDWORD64, PSYMBOL_INFO);
extern  SYMFROMADDRPROC _SymFromAddr;
#endif

typedef DWORD ( __stdcall *SYMLOADMODULE)(HANDLE, HANDLE, PSTR, PSTR, DWORD, DWORD);
extern  SYMLOADMODULE _SymLoadModule;

#ifdef USING_WXP_VERSION
typedef DWORD ( __stdcall *SYMLOADMODULE64)(HANDLE, HANDLE, PCSTR, PCSTR, DWORD64, DWORD);
extern  SYMLOADMODULE64 _SymLoadModule64;
#endif

typedef DWORD ( __stdcall *SYMUNDNAME)(PIMAGEHLP_SYMBOL, PSTR, DWORD);
extern  SYMUNDNAME _SymUnDName;

typedef DWORD ( __stdcall *SYMGETMODULEINFO)( HANDLE, DWORD, PIMAGEHLP_MODULE);
extern  SYMGETMODULEINFO _SymGetModuleInfo;

#ifdef USING_WXP_VERSION
typedef BOOL ( __stdcall *SYMGETMODULEINFO64)( HANDLE, DWORD64, PIMAGEHLP_MODULE64);
extern  SYMGETMODULEINFO64 _SymGetModuleInfo64;
#endif

typedef BOOL ( __stdcall *ENUMLOADEDMODULES)( HANDLE, PENUMLOADED_MODULES_CALLBACK, PVOID);
extern  ENUMLOADEDMODULES _EnumerateLoadedModules;

#ifdef USING_WXP_VERSION
typedef BOOL ( __stdcall *ENUMLOADEDMODULES64)( HANDLE, PENUMLOADED_MODULES_CALLBACK64, PVOID);
extern  ENUMLOADEDMODULES64 _EnumerateLoadedModules64;
#endif

typedef BOOL (__stdcall *SYMGETLINEFROMADDRPROC)(HANDLE, DWORD, PDWORD, PIMAGEHLP_LINE);
extern  SYMGETLINEFROMADDRPROC _SymGetLineFromAddr;

#ifdef USING_WXP_VERSION
typedef BOOL (__stdcall *SYMGETLINEFROMADDRPROC64)(HANDLE, DWORD64, PDWORD, PIMAGEHLP_LINE64);
extern  SYMGETLINEFROMADDRPROC64 _SymGetLineFromAddr64;
#endif

extern HANDLE hStackWalkMutex; 

HANDLE GetCurrentPIDorHandle();

PRBool EnsureSymInitialized();

PRBool EnsureImageHlpInitialized();

/*
 * SymGetModuleInfoEspecial
 *
 * Attempt to determine the module information.
 * Bug 112196 says this DLL may not have been loaded at the time
 *  SymInitialize was called, and thus the module information
 *  and symbol information is not available.
 * This code rectifies that problem.
 * Line information is optional.
 */
BOOL SymGetModuleInfoEspecial(HANDLE aProcess, DWORD aAddr, PIMAGEHLP_MODULE aModuleInfo, PIMAGEHLP_LINE aLineInfo);

struct WalkStackData {
  NS_WalkStackCallback callback;
  PRUint32 skipFrames;
  void *closure;
  HANDLE thread;
  HANDLE process;
};

void PrintError(char *prefix, WalkStackData* data);
DWORD WINAPI  WalkStackThread(LPVOID data);
void WalkStackMain64(struct WalkStackData* data);
void WalkStackMain(struct WalkStackData* data);


// Define these as static pointers so that we can load the DLL on the
// fly (and not introduce a link-time dependency on it). Tip o' the
// hat to Matt Pietrick for this idea. See:
//
//   http://msdn.microsoft.com/library/periodic/period97/F1/D3/S245C6.htm
//

SYMSETOPTIONSPROC _SymSetOptions;

SYMINITIALIZEPROC _SymInitialize;

SYMCLEANUPPROC _SymCleanup;

STACKWALKPROC _StackWalk;
#ifdef USING_WXP_VERSION
STACKWALKPROC64 _StackWalk64;
#else
#define _StackWalk64 0
#endif

SYMFUNCTIONTABLEACCESSPROC _SymFunctionTableAccess;
#ifdef USING_WXP_VERSION
SYMFUNCTIONTABLEACCESSPROC64 _SymFunctionTableAccess64;
#else
#define _SymFunctionTableAccess64 0
#endif

SYMGETMODULEBASEPROC _SymGetModuleBase;
#ifdef USING_WXP_VERSION
SYMGETMODULEBASEPROC64 _SymGetModuleBase64;
#else
#define _SymGetModuleBase64 0
#endif

SYMGETSYMFROMADDRPROC _SymGetSymFromAddr;
#ifdef USING_WXP_VERSION
SYMFROMADDRPROC _SymFromAddr;
#else
#define _SymFromAddr 0
#endif

SYMLOADMODULE _SymLoadModule;
#ifdef USING_WXP_VERSION
SYMLOADMODULE64 _SymLoadModule64;
#else
#define _SymLoadModule64 0
#endif

SYMUNDNAME _SymUnDName;

SYMGETMODULEINFO _SymGetModuleInfo;
#ifdef USING_WXP_VERSION
SYMGETMODULEINFO64 _SymGetModuleInfo64;
#else
#define _SymGetModuleInfo64 0
#endif

ENUMLOADEDMODULES _EnumerateLoadedModules;
#ifdef USING_WXP_VERSION
ENUMLOADEDMODULES64 _EnumerateLoadedModules64;
#else
#define _EnumerateLoadedModules64 0
#endif

SYMGETLINEFROMADDRPROC _SymGetLineFromAddr;
#ifdef USING_WXP_VERSION
SYMGETLINEFROMADDRPROC64 _SymGetLineFromAddr64;
#else
#define _SymGetLineFromAddr64 0
#endif

HANDLE hStackWalkMutex;

PR_END_EXTERN_C

// Routine to print an error message to standard error.
// Will also call callback with error, if data supplied.
void PrintError(char *prefix)
{
    LPVOID lpMsgBuf;
    DWORD lastErr = GetLastError();
    FormatMessage(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      NULL,
      lastErr,
      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
      (LPTSTR) &lpMsgBuf,
      0,
      NULL
    );
    fprintf(stderr, "### ERROR: %s: %s", prefix, lpMsgBuf);
    fflush(stderr);
    LocalFree( lpMsgBuf );
}

PRBool
EnsureImageHlpInitialized()
{
    static PRBool gInitialized = PR_FALSE;

    if (gInitialized)
        return gInitialized;

    // Create a mutex with no initial owner.
    hStackWalkMutex = CreateMutex(
      NULL,                       // default security attributes
      FALSE,                      // initially not owned
      NULL);                      // unnamed mutex

    if (hStackWalkMutex == NULL) {
        PrintError("CreateMutex");
        return PR_FALSE;
    }

    HMODULE module = ::LoadLibrary("DBGHELP.DLL");
    if (!module) {
        module = ::LoadLibrary("IMAGEHLP.DLL");
        if (!module) return PR_FALSE;
    }

    _SymSetOptions = (SYMSETOPTIONSPROC) ::GetProcAddress(module, "SymSetOptions");
    if (!_SymSetOptions) return PR_FALSE;

    _SymInitialize = (SYMINITIALIZEPROC) ::GetProcAddress(module, "SymInitialize");
    if (!_SymInitialize) return PR_FALSE;

    _SymCleanup = (SYMCLEANUPPROC)GetProcAddress(module, "SymCleanup");
    if (!_SymCleanup) return PR_FALSE;

#ifdef USING_WXP_VERSION
    _StackWalk64 = (STACKWALKPROC64)GetProcAddress(module, "StackWalk64");
#endif
    _StackWalk = (STACKWALKPROC)GetProcAddress(module, "StackWalk");
    if (!_StackWalk64  && !_StackWalk) return PR_FALSE;

#ifdef USING_WXP_VERSION
    _SymFunctionTableAccess64 = (SYMFUNCTIONTABLEACCESSPROC64) GetProcAddress(module, "SymFunctionTableAccess64");
#endif
    _SymFunctionTableAccess = (SYMFUNCTIONTABLEACCESSPROC) GetProcAddress(module, "SymFunctionTableAccess");
    if (!_SymFunctionTableAccess64 && !_SymFunctionTableAccess) return PR_FALSE;

#ifdef USING_WXP_VERSION
    _SymGetModuleBase64 = (SYMGETMODULEBASEPROC64)GetProcAddress(module, "SymGetModuleBase64");
#endif
    _SymGetModuleBase = (SYMGETMODULEBASEPROC)GetProcAddress(module, "SymGetModuleBase");
    if (!_SymGetModuleBase64 && !_SymGetModuleBase) return PR_FALSE;

    _SymGetSymFromAddr = (SYMGETSYMFROMADDRPROC)GetProcAddress(module, "SymGetSymFromAddr");
#ifdef USING_WXP_VERSION
    _SymFromAddr = (SYMFROMADDRPROC)GetProcAddress(module, "SymFromAddr");
#endif
    if (!_SymFromAddr && !_SymGetSymFromAddr) return PR_FALSE;

#ifdef USING_WXP_VERSION
    _SymLoadModule64 = (SYMLOADMODULE64)GetProcAddress(module, "SymLoadModule64");
#endif
    _SymLoadModule = (SYMLOADMODULE)GetProcAddress(module, "SymLoadModule");
    if (!_SymLoadModule64 && !_SymLoadModule) return PR_FALSE;

    _SymUnDName = (SYMUNDNAME)GetProcAddress(module, "SymUnDName");
    if (!_SymUnDName) return PR_FALSE;

#ifdef USING_WXP_VERSION
    _SymGetModuleInfo64 = (SYMGETMODULEINFO64)GetProcAddress(module, "SymGetModuleInfo64");
#endif
    _SymGetModuleInfo = (SYMGETMODULEINFO)GetProcAddress(module, "SymGetModuleInfo");
    if (!_SymGetModuleInfo64 && !_SymGetModuleInfo) return PR_FALSE;

#ifdef USING_WXP_VERSION
    _EnumerateLoadedModules64 = (ENUMLOADEDMODULES64)GetProcAddress(module, "EnumerateLoadedModules64");
#endif
    _EnumerateLoadedModules = (ENUMLOADEDMODULES)GetProcAddress(module, "EnumerateLoadedModules");
    if (!_EnumerateLoadedModules64 && !_EnumerateLoadedModules) return PR_FALSE;

#ifdef USING_WXP_VERSION
    _SymGetLineFromAddr64 = (SYMGETLINEFROMADDRPROC64)GetProcAddress(module, "SymGetLineFromAddr64");
#endif
    _SymGetLineFromAddr = (SYMGETLINEFROMADDRPROC)GetProcAddress(module, "SymGetLineFromAddr");
    if (!_SymGetLineFromAddr64 && !_SymGetLineFromAddr) return PR_FALSE;

    return gInitialized = PR_TRUE;
}

void
WalkStackMain64(struct WalkStackData* data)
{
#ifdef USING_WXP_VERSION
    // Get the context information for the thread. That way we will
    // know where our sp, fp, pc, etc. are and can fill in the
    // STACKFRAME64 with the initial values.
    CONTEXT context;
    HANDLE myProcess = data->process;
    HANDLE myThread = data->thread;
    DWORD64 addr;
    STACKFRAME64 frame64;
    int skip = 3 + data->skipFrames; // skip our own stack walking frames
    BOOL ok;

    // Get a context for the specified thread.
    memset(&context, 0, sizeof(CONTEXT));
    context.ContextFlags = CONTEXT_FULL;
    if (!GetThreadContext(myThread, &context)) {
        PrintError("GetThreadContext");
        return;
    }

    // Setup initial stack frame to walk from
    memset(&frame64, 0, sizeof(frame64));
#ifdef _M_IX86
    frame64.AddrPC.Offset    = context.Eip;
    frame64.AddrStack.Offset = context.Esp;
    frame64.AddrFrame.Offset = context.Ebp;
#elif defined _M_AMD64
    frame64.AddrPC.Offset    = context.Rip;
    frame64.AddrStack.Offset = context.Rsp;
    frame64.AddrFrame.Offset = context.Rbp;
#elif defined _M_IA64
    frame64.AddrPC.Offset    = context.StIIP;
    frame64.AddrStack.Offset = context.SP;
    frame64.AddrFrame.Offset = context.RsBSP;
#else
    PrintError("Unknown platform. No stack walking.");
    return;
#endif
    frame64.AddrPC.Mode      = AddrModeFlat;
    frame64.AddrStack.Mode   = AddrModeFlat;
    frame64.AddrFrame.Mode   = AddrModeFlat;
    frame64.AddrReturn.Mode  = AddrModeFlat;

    // Now walk the stack and map the pc's to symbol names
    while (1) {

        ok = 0;

        // stackwalk is not threadsafe, so grab the lock.
        DWORD dwWaitResult;
        dwWaitResult = WaitForSingleObject(hStackWalkMutex, INFINITE);
        if (dwWaitResult == WAIT_OBJECT_0) {

            ok = _StackWalk64(
#ifdef _M_AMD64
              IMAGE_FILE_MACHINE_AMD64,
#elif defined _M_IA64
              IMAGE_FILE_MACHINE_IA64,
#elif defined _M_IX86
              IMAGE_FILE_MACHINE_I386,
#else
              0,
#endif
              myProcess,
              myThread,
              &frame64,
              &context,
              NULL,
              _SymFunctionTableAccess64, // function table access routine
              _SymGetModuleBase64,       // module base routine
              0
            );

            ReleaseMutex(hStackWalkMutex);  // release our lock

            if (ok)
                addr = frame64.AddrPC.Offset;
            else {
                addr = 0;
                PrintError("WalkStack64");
            }

            if (!ok || (addr == 0)) {
                break;
            }

            if (skip-- > 0) {
                continue;
            }

            (*data->callback)((void*)addr, data->closure);

#if 0
            // Stop walking when we get to kernel32.
            if (strcmp(modInfo.ModuleName, "kernel32") == 0)
                break;
#else
            if (frame64.AddrReturn.Offset == 0)
                break;
#endif
        }
        else {
            PrintError("LockError64");
        } 
    }
    return;
#endif
}


void
WalkStackMain(struct WalkStackData* data)
{
    // Get the context information for the thread. That way we will
    // know where our sp, fp, pc, etc. are and can fill in the
    // STACKFRAME with the initial values.
    CONTEXT context;
    HANDLE myProcess = data->process;
    HANDLE myThread = data->thread;
    DWORD addr;
    STACKFRAME frame;
    int skip = data->skipFrames; // skip our own stack walking frames
    BOOL ok;

    // Get a context for the specified thread.
    memset(&context, 0, sizeof(CONTEXT));
    context.ContextFlags = CONTEXT_FULL;
    if (!GetThreadContext(myThread, &context)) {
        PrintError("GetThreadContext");
        return;
    }

    // Setup initial stack frame to walk from
#if defined _M_IX86
    memset(&frame, 0, sizeof(frame));
    frame.AddrPC.Offset    = context.Eip;
    frame.AddrPC.Mode      = AddrModeFlat;
    frame.AddrStack.Offset = context.Esp;
    frame.AddrStack.Mode   = AddrModeFlat;
    frame.AddrFrame.Offset = context.Ebp;
    frame.AddrFrame.Mode   = AddrModeFlat;
#else
    PrintError("Unknown platform. No stack walking.");
    return;
#endif

    // Now walk the stack and map the pc's to symbol names
    while (1) {

        ok = 0;

        // debug routines are not threadsafe, so grab the lock.
        DWORD dwWaitResult;
        dwWaitResult = WaitForSingleObject(hStackWalkMutex, INFINITE);
        if (dwWaitResult == WAIT_OBJECT_0) {

            ok = _StackWalk(
                IMAGE_FILE_MACHINE_I386,
                myProcess,
                myThread,
                &frame,
                &context,
                0,                        // read process memory routine
                _SymFunctionTableAccess,  // function table access routine
                _SymGetModuleBase,        // module base routine
                0                         // translate address routine
              );

            ReleaseMutex(hStackWalkMutex);  // release our lock

            if (ok)
                addr = frame.AddrPC.Offset;
            else {
                addr = 0;
                PrintError("WalkStack");
            }

            if (!ok || (addr == 0)) {
                break;
            }

            if (skip-- > 0) {
                continue;
            }

            (*data->callback)((void*)addr, data->closure);

#if 0
            // Stop walking when we get to kernel32.dll.
            if (strcmp(modInfo.ImageName, "kernel32.dll") == 0)
                break;
#else
            if (frame.AddrReturn.Offset == 0)
                break;
#endif
        }
        else {
            PrintError("LockError");
        }
        
    }

    return;

}

DWORD WINAPI
WalkStackThread(LPVOID lpdata)
{
    struct WalkStackData *data = (WalkStackData *)lpdata;
    DWORD ret ;

    // Suspend the calling thread, dump his stack, and then resume him.
    // He's currently waiting for us to finish so now should be a good time.
    ret = ::SuspendThread( data->thread );
    if (ret == -1) {
        PrintError("ThreadSuspend");
    }
    else {
        if (_StackWalk64)
            WalkStackMain64(data);
        else
            WalkStackMain(data);
        ret = ::ResumeThread(data->thread);
        if (ret == -1) {
            PrintError("ThreadResume");
        }
    }

    return 0;
}

/**
 * Walk the stack, translating PC's found into strings and recording the
 * chain in aBuffer. For this to work properly, the DLLs must be rebased
 * so that the address in the file agrees with the address in memory.
 * Otherwise StackWalk will return FALSE when it hits a frame in a DLL
 * whose in memory address doesn't match its in-file address.
 */

EXPORT_XPCOM_API(nsresult)
NS_StackWalk(NS_WalkStackCallback aCallback, PRUint32 aSkipFrames,
             void *aClosure)
{
    HANDLE myProcess, myThread, walkerThread;
    DWORD walkerReturn;
    struct WalkStackData data;

    if (!EnsureImageHlpInitialized())
        return PR_FALSE;

    // Have to duplicate handle to get a real handle.
    if (!::DuplicateHandle(::GetCurrentProcess(),
                           ::GetCurrentProcess(),
                           ::GetCurrentProcess(),
                           &myProcess,
                           THREAD_ALL_ACCESS, FALSE, 0)) {
        PrintError("DuplicateHandle (process)");
        return NS_ERROR_FAILURE;
    }
    if (!::DuplicateHandle(::GetCurrentProcess(),
                           ::GetCurrentThread(),
                           ::GetCurrentProcess(),
                           &myThread,
                           THREAD_ALL_ACCESS, FALSE, 0)) {
        PrintError("DuplicateHandle (thread)");
        ::CloseHandle(myProcess);
        return NS_ERROR_FAILURE;
    }

    data.callback = aCallback;
    data.skipFrames = aSkipFrames;
    data.closure = aClosure;
    data.thread = myThread;
    data.process = myProcess;
    walkerThread = ::CreateThread( NULL, 0, WalkStackThread, (LPVOID) &data, 0, NULL ) ;
    if (walkerThread) {
        walkerReturn = ::WaitForSingleObject(walkerThread, 2000); // no timeout is never a good idea
        if (walkerReturn != WAIT_OBJECT_0) {
            PrintError("ThreadWait");
        }
        ::CloseHandle(walkerThread);
    }
    else {
        PrintError("ThreadCreate");
    }
    ::CloseHandle(myThread);
    ::CloseHandle(myProcess);
    return NS_OK;
}


static BOOL CALLBACK callbackEspecial(
  LPSTR aModuleName,
  DWORD aModuleBase,
  ULONG aModuleSize,
  PVOID aUserContext)
{
    BOOL retval = TRUE;
    DWORD addr = *(DWORD*)aUserContext;

    /*
     * You'll want to control this if we are running on an
     *  architecture where the addresses go the other direction.
     * Not sure this is even a realistic consideration.
     */
    const BOOL addressIncreases = TRUE;

    /*
     * If it falls inside the known range, load the symbols.
     */
    if (addressIncreases
       ? (addr >= aModuleBase && addr <= (aModuleBase + aModuleSize))
       : (addr <= aModuleBase && addr >= (aModuleBase - aModuleSize))
        ) {
        retval = _SymLoadModule(GetCurrentProcess(), NULL, aModuleName, NULL, aModuleBase, aModuleSize);
    }

    return retval;
}

static BOOL CALLBACK callbackEspecial64(
  PTSTR aModuleName,
  DWORD64 aModuleBase,
  ULONG aModuleSize,
  PVOID aUserContext)
{
#ifdef USING_WXP_VERSION
    BOOL retval = TRUE;
    DWORD64 addr = *(DWORD64*)aUserContext;

    /*
     * You'll want to control this if we are running on an
     *  architecture where the addresses go the other direction.
     * Not sure this is even a realistic consideration.
     */
    const BOOL addressIncreases = TRUE;

    /*
     * If it falls in side the known range, load the symbols.
     */
    if (addressIncreases
       ? (addr >= aModuleBase && addr <= (aModuleBase + aModuleSize))
       : (addr <= aModuleBase && addr >= (aModuleBase - aModuleSize))
        ) {
        retval = _SymLoadModule64(GetCurrentProcess(), NULL, aModuleName, NULL, aModuleBase, aModuleSize);
    }

    return retval;
#else
    return FALSE;
#endif
}

/*
 * SymGetModuleInfoEspecial
 *
 * Attempt to determine the module information.
 * Bug 112196 says this DLL may not have been loaded at the time
 *  SymInitialize was called, and thus the module information
 *  and symbol information is not available.
 * This code rectifies that problem.
 */
BOOL SymGetModuleInfoEspecial(HANDLE aProcess, DWORD aAddr, PIMAGEHLP_MODULE aModuleInfo, PIMAGEHLP_LINE aLineInfo)
{
    BOOL retval = FALSE;

    /*
     * Init the vars if we have em.
     */
    aModuleInfo->SizeOfStruct = sizeof(IMAGEHLP_MODULE);
    if (nsnull != aLineInfo) {
      aLineInfo->SizeOfStruct = sizeof(IMAGEHLP_LINE);
    }

    /*
     * Give it a go.
     * It may already be loaded.
     */
    retval = _SymGetModuleInfo(aProcess, aAddr, aModuleInfo);

    if (FALSE == retval) {
        BOOL enumRes = FALSE;

        /*
         * Not loaded, here's the magic.
         * Go through all the modules.
         */
        enumRes = _EnumerateLoadedModules(aProcess, (PENUMLOADED_MODULES_CALLBACK)callbackEspecial, (PVOID)&aAddr);
        if (FALSE != enumRes)
        {
            /*
             * One final go.
             * If it fails, then well, we have other problems.
             */
            retval = _SymGetModuleInfo(aProcess, aAddr, aModuleInfo);
        }
    }

    /*
     * If we got module info, we may attempt line info as well.
     * We will not report failure if this does not work.
     */
    if (FALSE != retval && nsnull != aLineInfo && nsnull != _SymGetLineFromAddr) {
        DWORD displacement = 0;
        BOOL lineRes = FALSE;
        lineRes = _SymGetLineFromAddr(aProcess, aAddr, &displacement, aLineInfo);
    }

    return retval;
}

#ifdef USING_WXP_VERSION
BOOL SymGetModuleInfoEspecial64(HANDLE aProcess, DWORD64 aAddr, PIMAGEHLP_MODULE64 aModuleInfo, PIMAGEHLP_LINE64 aLineInfo)
{
    BOOL retval = FALSE;

    /*
     * Init the vars if we have em.
     */
    aModuleInfo->SizeOfStruct = sizeof(IMAGEHLP_MODULE64);
    if (nsnull != aLineInfo) {
        aLineInfo->SizeOfStruct = sizeof(IMAGEHLP_LINE64);
    }

    /*
     * Give it a go.
     * It may already be loaded.
     */
    retval = _SymGetModuleInfo64(aProcess, aAddr, aModuleInfo);

    if (FALSE == retval) {
        BOOL enumRes = FALSE;

        /*
         * Not loaded, here's the magic.
         * Go through all the modules.
         */
        enumRes = _EnumerateLoadedModules64(aProcess, (PENUMLOADED_MODULES_CALLBACK64)callbackEspecial64, (PVOID)&aAddr);
        if (FALSE != enumRes)
        {
            /*
             * One final go.
             * If it fails, then well, we have other problems.
             */
            retval = _SymGetModuleInfo64(aProcess, aAddr, aModuleInfo);
        }
    }

    /*
     * If we got module info, we may attempt line info as well.
     * We will not report failure if this does not work.
     */
    if (FALSE != retval && nsnull != aLineInfo && nsnull != _SymGetLineFromAddr64) {
        DWORD displacement = 0;
        BOOL lineRes = FALSE;
        lineRes = _SymGetLineFromAddr64(aProcess, aAddr, &displacement, aLineInfo);
    }

    return retval;
}
#endif

HANDLE
GetCurrentPIDorHandle()
{
    if (_SymGetModuleBase64)
        return GetCurrentProcess();  // winxp and friends use process handle

    return (HANDLE) GetCurrentProcessId(); // winme win98 win95 etc use process identifier
}

PRBool
EnsureSymInitialized()
{
    static PRBool gInitialized = PR_FALSE;
    PRBool retStat;

    if (gInitialized)
        return gInitialized;

    if (!EnsureImageHlpInitialized())
        return PR_FALSE;

    _SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
    retStat = _SymInitialize(GetCurrentPIDorHandle(), NULL, TRUE);
    if (!retStat)
        PrintError("SymInitialize");

    gInitialized = retStat;
    /* XXX At some point we need to arrange to call _SymCleanup */

    return retStat;
}


EXPORT_XPCOM_API(nsresult)
NS_DescribeCodeAddress(void *aPC, nsCodeAddressDetails *aDetails)
{
    aDetails->library[0] = '\0';
    aDetails->loffset = 0;
    aDetails->filename[0] = '\0';
    aDetails->lineno = 0;
    aDetails->function[0] = '\0';
    aDetails->foffset = 0;

    if (!EnsureSymInitialized())
        return NS_ERROR_FAILURE;

    HANDLE myProcess = ::GetCurrentProcess();
    BOOL ok;

    // debug routines are not threadsafe, so grab the lock.
    DWORD dwWaitResult;
    dwWaitResult = WaitForSingleObject(hStackWalkMutex, INFINITE);
    if (dwWaitResult != WAIT_OBJECT_0)
        return NS_ERROR_UNEXPECTED;

#ifdef USING_WXP_VERSION
    if (_StackWalk64) {
        //
        // Attempt to load module info before we attempt to resolve the symbol.
        // This just makes sure we get good info if available.
        //

        DWORD64 addr = (DWORD64)aPC;
        IMAGEHLP_MODULE64 modInfo;
        modInfo.SizeOfStruct = sizeof(modInfo);
        BOOL modInfoRes;
        modInfoRes = SymGetModuleInfoEspecial64(myProcess, addr, &modInfo, nsnull);

        if (modInfoRes) {
            PL_strncpyz(aDetails->library, modInfo.ModuleName,
                        sizeof(aDetails->library));
            aDetails->loffset = (char*) aPC - (char*) modInfo.BaseOfImage;
            // XXX We could get filename/lineno information from
            // SymGetModuleInfoEspecial64
        }

        ULONG64 buffer[(sizeof(SYMBOL_INFO) +
          MAX_SYM_NAME*sizeof(TCHAR) + sizeof(ULONG64) - 1) / sizeof(ULONG64)];
        PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)buffer;
        pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        pSymbol->MaxNameLen = MAX_SYM_NAME;

        DWORD64 displacement;
        ok = _SymFromAddr && _SymFromAddr(myProcess, addr, &displacement, pSymbol);

        if (ok) {
            PL_strncpyz(aDetails->function, pSymbol->Name,
                        sizeof(aDetails->function));
            aDetails->foffset = displacement;
        }
    } else
#endif
    {
        //
        // Attempt to load module info before we attempt to resolve the symbol.
        // This just makes sure we get good info if available.
        //

        DWORD addr = (DWORD)aPC;
        IMAGEHLP_MODULE modInfo;
        modInfo.SizeOfStruct = sizeof(modInfo);
        BOOL modInfoRes;
        modInfoRes = SymGetModuleInfoEspecial(myProcess, addr, &modInfo, nsnull);

        if (modInfoRes) {
            PL_strncpyz(aDetails->library, modInfo.ModuleName,
                        sizeof(aDetails->library));
            aDetails->loffset = (char*) aPC - (char*) modInfo.BaseOfImage;
            // XXX We could get filename/lineno information from
            // SymGetModuleInfoEspecial
        }

#ifdef USING_WXP_VERSION
        ULONG64 buffer[(sizeof(SYMBOL_INFO) +
          MAX_SYM_NAME*sizeof(TCHAR) + sizeof(ULONG64) - 1) / sizeof(ULONG64)];
        PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)buffer;
        pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        pSymbol->MaxNameLen = MAX_SYM_NAME;

        DWORD64 displacement;

        ok = _SymFromAddr && _SymFromAddr(myProcess, addr, &displacement, pSymbol);
#else
        char buf[sizeof(IMAGEHLP_SYMBOL) + 512];
        PIMAGEHLP_SYMBOL pSymbol = (PIMAGEHLP_SYMBOL) buf;
        pSymbol->SizeOfStruct = sizeof(buf);
        pSymbol->MaxNameLength = 512;

        DWORD displacement;

        ok = _SymGetSymFromAddr(myProcess,
                    frame.AddrPC.Offset,
                    &displacement,
                    pSymbol);
#endif

        if (ok) {
            PL_strncpyz(aDetails->function, pSymbol->Name,
                        sizeof(aDetails->function));
            aDetails->foffset = displacement;
        }
    }

    ReleaseMutex(hStackWalkMutex);  // release our lock
    return NS_OK;
}

EXPORT_XPCOM_API(nsresult)
NS_FormatCodeAddressDetails(void *aPC, const nsCodeAddressDetails *aDetails,
                            char *aBuffer, PRUint32 aBufferSize)
{
#ifdef USING_WXP_VERSION
    if (_StackWalk64) {
        if (aDetails->function[0])
            _snprintf(aBuffer, aBufferSize, "%s!%s+0x%016lX\n",
                      aDetails->library, aDetails->function, aDetails->foffset);
        else
            _snprintf(aBuffer, aBufferSize, "0x%016lX\n", aPC);
    } else {
#endif
        if (aDetails->function[0])
            _snprintf(aBuffer, aBufferSize, "%s!%s+0x%08lX\n",
                      aDetails->library, aDetails->function, aDetails->foffset);
        else
            _snprintf(aBuffer, aBufferSize, "0x%08lX\n", aPC);
#ifdef USING_WXP_VERSION
    }
#endif
    return NS_OK;
}

// WIN32 x86 stack walking code
// i386 or PPC Linux stackwalking code or Solaris
#elif (defined(linux) && defined(__GNUC__) && (defined(__i386) || defined(PPC) || defined(__x86_64__))) || (defined(__sun) && (defined(__sparc) || defined(sparc) || defined(__i386) || defined(i386)))

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "nscore.h"
#include <stdio.h>
#include "plstr.h"

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
      (*aCallback)(pc, aClosure);
    }
  }
  return NS_OK;
}

EXPORT_XPCOM_API(nsresult)
NS_DescribeCodeAddress(void *aPC, nsCodeAddressDetails *aDetails)
{
  aDetails->library[0] = '\0';
  aDetails->loffset = 0;
  aDetails->filename[0] = '\0';
  aDetails->lineno = 0;
  aDetails->function[0] = '\0';
  aDetails->foffset = 0;

  Dl_info info;
  int ok = dladdr(aPC, &info);
  if (!ok) {
    return NS_OK;
  }

  PL_strncpyz(aDetails->library, info.dli_fname, sizeof(aDetails->library));
  aDetails->loffset = (char*)aPC - (char*)info.dli_fbase;

  const char * symbol = info.dli_sname;
  int len;
  if (!symbol || !(len = strlen(symbol))) {
    return NS_OK;
  }

  char demangled[4096] = "\0";

  DemangleSymbol(symbol, demangled, sizeof(demangled));

  if (strlen(demangled)) {
    symbol = demangled;
    len = strlen(symbol);
  }

  PL_strncpyz(aDetails->function, symbol, sizeof(aDetails->function));
  aDetails->foffset = (char*)aPC - (char*)info.dli_saddr;
  return NS_OK;
}

EXPORT_XPCOM_API(nsresult)
NS_FormatCodeAddressDetails(void *aPC, const nsCodeAddressDetails *aDetails,
                            char *aBuffer, PRUint32 aBufferSize)
{
  if (!aDetails->library[0]) {
    snprintf(aBuffer, aBufferSize, "UNKNOWN %p\n", aPC);
  } else if (!aDetails->function[0]) {
    snprintf(aBuffer, aBufferSize, "UNKNOWN [%s +0x%08lX]\n",
                                   aDetails->library, aDetails->loffset);
  } else {
    snprintf(aBuffer, aBufferSize, "%s+0x%08lX [%s +0x%08lX]\n",
                                   aDetails->function, aDetails->foffset,
                                   aDetails->library, aDetails->loffset);
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
        (args->callback)(pc, args->closure);

        ptr->next = newbucket(pc);
        mutex_unlock(&lock);
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
cswalkstack(struct frame *fp, int (*operate_func)(void *, void *),
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
cs_operate(int (*operate_func)(void *, void *), void * usrarg)
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

EXPORT_XPCOM_API(nsresult)
NS_DescribeCodeAddress(void *aPC, nsCodeAddressDetails *aDetails)
{
    aDetails->library[0] = '\0';
    aDetails->loffset = 0;
    aDetails->filename[0] = '\0';
    aDetails->lineno = 0;
    aDetails->function[0] = '\0';
    aDetails->foffset = 0;

    char dembuff[4096];
    Dl_info info;

    if (dladdr(aPC, & info)) {
        if (info.dli_fname) {
            PL_strncpyz(aDetails->library, info.dli_fname,
                        sizeof(aDetails->library));
            aDetails->loffset = (char*)aPC - (char*)info.dli_fbase;
        }
        if (info.dli_sname) {
            aDetails->foffset = (char*)aPC - (char*)info.dli_saddr;
#ifdef __GNUC__
            DemangleSymbol(info.dli_sname, dembuff, sizeof(dembuff));
#else
            if (!demf || demf(info.dli_sname, dembuff, sizeof (dembuff)))
                dembuff[0] = 0;
#endif /*__GNUC__*/
            PL_strncpyz(aDetails->function,
                        (dembuff[0] != '\0') ? dembuff : info.dli_sname,
                        sizeof(aDetails->function));
        }
    }

    return NS_OK;
}

EXPORT_XPCOM_API(nsresult)
NS_FormatCodeAddressDetails(void *aPC, const nsCodeAddressDetails *aDetails,
                            char *aBuffer, PRUint32 aBufferSize)
{
    snprintf(aBuffer, aBufferSize, "%p %s:%s+0x%lx\n",
             aPC,
             aDetails->library[0] ? aDetails->library : "??",
             aDetails->function[0] ? aDetails->function : "??",
             aDetails->foffset);
    return NS_OK;
}

#endif

#else // unsupported platform.

EXPORT_XPCOM_API(nsresult)
NS_StackWalk(NS_WalkStackCallback aCallback, PRUint32 aSkipFrames,
             void *aClosure)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

EXPORT_XPCOM_API(nsresult)
NS_DescribeCodeAddress(void *aPC, nsCodeAddressDetails *aDetails)
{
    aDetails->library[0] = '\0';
    aDetails->loffset = 0;
    aDetails->filename[0] = '\0';
    aDetails->lineno = 0;
    aDetails->function[0] = '\0';
    aDetails->foffset = 0;
    return NS_ERROR_NOT_IMPLEMENTED;
}

EXPORT_XPCOM_API(nsresult)
NS_FormatCodeAddressDetails(void *aPC, const nsCodeAddressDetails *aDetails,
                            char *aBuffer, PRUint32 aBufferSize)
{
    aBuffer[0] = '\0';
    return NS_ERROR_NOT_IMPLEMENTED;
}

#endif
