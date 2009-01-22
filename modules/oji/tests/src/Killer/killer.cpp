/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
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
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Sun Microsystems, Inc.
 * Portions created by the Initial Developer are Copyright (C) 1999
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
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


/**************************************************************/
/* This program is designed for Win32 platforms only          */
/* to close automatically                                     */
/* the dialog window appeared after Mozilla is crashed.       */
/* Otherwise application remains in memory until              */
/* this dialog is closed.                                     */
/* Though built version exist in the script directory one     */
/* may want to build it again. Then the following command     */
/* line should be used:                                       */
/* 	cl killer.cpp /link user32.lib                        */
/**************************************************************/

#include <windows.h>
#include <stdio.h>


#define ERROR_DIALOG_TITLE "Mozilla: mozilla.exe - Application Error"
//better to find in title some keywords than compare with the entire phrase
#define ERROR_DIALOG_KW_1 "mozilla"
#define ERROR_DIALOG_KW_2 "Error"

#define OK_BUTTON_TITLE "OK"

BOOL CALLBACK EnumChildProc(HWND hwnd, LPARAM out) {
	char title[1024];
	GetWindowText(hwnd, title, 1024);
	//printf("Child Window: %x -> %s\n", hwnd, title);
	if (!strcmp(title, OK_BUTTON_TITLE)) {
		*((HWND*)out) = hwnd;
		return FALSE;
	}	
	return TRUE;
}

BOOL CALLBACK EnumWindowsProc(  HWND hwnd, LPARAM lParam) {
	char title[1024];
	GetWindowText(hwnd, title, 1024);
	//printf("Window: %x -> %s\n", hwnd, title);
	if (strstr(title, ERROR_DIALOG_KW_1) && strstr(title, ERROR_DIALOG_KW_2)) {
		DWORD lp = 0, wp = 0;
		HWND ok;
		//really we can ommit this step but ...
		EnumChildWindows(hwnd, EnumChildProc, (LPARAM)(&ok));
		if (!ok) {
			printf("OK button not found !\n");
			return FALSE;
		}
		/*POINT p;
		p.x = 285; //experimental data -> but there are a lot of fonts in the world ...
		p.y = 150;
		HWND ok = ChildWindowFromPoint(hwnd, p);
		if (ok)
			printf("Child window: %x\n", ok);
		else 
			printf("Child window not found !\n");*/
		lp = (unsigned long)ok;
		wp = 1;
		wp = wp | (BN_CLICKED << 16);
		//printf("COMMAND: %d (hwnd), %d (code), %d(id)\n", lp, HIWORD(wp), LOWORD(wp));
		SendMessage(hwnd, WM_COMMAND, wp, lp);
		return FALSE;
	}
	return TRUE;
}

void main() {
	EnumWindows(EnumWindowsProc, 0);
}
