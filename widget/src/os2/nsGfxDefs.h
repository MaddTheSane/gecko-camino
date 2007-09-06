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
 * The Original Code is the Mozilla OS/2 libraries.
 *
 * The Initial Developer of the Original Code is
 * John Fairhurst, <john_fairhurst@iname.com>.
 * Portions created by the Initial Developer are Copyright (C) 1999
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

#ifndef _nsgfxdefs_h
#define _nsgfxdefs_h

// nsGfxDefs.h - common includes etc. for gfx library

#include "nscore.h"

#define INCL_PM
#define INCL_DOS
#define INCL_DOSERRORS
#define INCL_DEV
#include <os2.h>
#include "prlog.h"
#include "nsHashtable.h"

#include <uconv.h> // XXX hack XXX

#define COLOR_CUBE_SIZE 216

void PMERROR(const char *str);

// Wrapper code for all OS/2 system calls to check the return code for error condition in debug build.
// Could be used like this:
//
//    HDC hdc = GFX (::GpiQueryDevice (ps), HDC_ERROR);
//    GFX (::GpiAssociate (mPrintPS, 0), FALSE);
//    return GFX (::GpiDestroyPS (mPrintPS), FALSE);

#ifdef DEBUG
  extern void DEBUG_LogErr(long ReturnCode, const char* ErrorExpression,
                           const char* FileName, const char* FunctionName,
                           long LineNum);

  inline long CheckSuccess(long ReturnCode, long SuccessCode,
                           const char* ErrorExpression, const char* FileName,
                           const char* FunctionName, long LineNum)
  {
    if (ReturnCode != SuccessCode) {
      DEBUG_LogErr(ReturnCode, ErrorExpression, FileName, FunctionName, LineNum);
    }
    return ReturnCode;
  }

  #define CHK_SUCCESS(ReturnCode, SuccessCode)                          \
          CheckSuccess(ReturnCode, SuccessCode, #ReturnCode, __FILE__,  \
                       __FUNCTION__, __LINE__)

  inline long CheckFailure(long ReturnCode, long ErrorCode,
                           const char* ErrorExpression, const char* FileName,
                           const char* FunctionName, long LineNum)
  {
    if (ReturnCode == ErrorCode) {
      DEBUG_LogErr(ReturnCode, ErrorExpression, FileName, FunctionName, LineNum);
    }
    return ReturnCode;
  }

/*  #define CHK_FAIL(ReturnCode, ErrorCode)                             \ */
  #define GFX(ReturnCode, ErrorCode)                                  \
          CheckFailure(ReturnCode, ErrorCode, #ReturnCode, __FILE__,  \
                       __FUNCTION__, __LINE__)

#else	// Retail build
  #define CHK_SUCCESS(ReturnCode, SuccessCode) ReturnCode
/*  #define CHK_FAIL(ReturnCode, ErrorCode) ReturnCode */
  #define GFX(ReturnCode, ErrorCode) ReturnCode
#endif

class nsString;
class nsIDeviceContext;


BOOL GetTextExtentPoint32(HPS aPS, const char* aString, int aLength, PSIZEL aSizeL);
BOOL ExtTextOut(HPS aPS, int X, int Y, UINT fuOptions, const RECTL* lprc,
                const char* aString, unsigned int aLength, const int* pDx);

BOOL IsDBCS();

#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif

#define MK_RGB(r,g,b) ((r) * 65536) + ((g) * 256) + (b)

#ifdef DEBUG
extern PRLogModuleInfo *gGFXOS2LogModule;
#endif

#endif
