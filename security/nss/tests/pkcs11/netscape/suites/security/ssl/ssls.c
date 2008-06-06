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
 * The Original Code is the Netscape security libraries.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1994-2000
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

#include "ssls.h"

#include <ssl.h>
#include <sslproto.h>

/* 21 lines x 8 chars = 168 bytes */


#if 1
unsigned char data[] = {
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x07, 0x08,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x07, 0x08,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x07, 0x08,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x07, 0x08,

  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x07, 0x08,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x07, 0x08,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x07, 0x08,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x07, 0x08,

  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x07, 0x08,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x07, 0x08,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x07, 0x08,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x07, 0x08,

  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x07, 0x08,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x07, 0x08,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x07, 0x08,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x07, 0x08,

  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x07, 0x08,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x07, 0x08,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x07, 0x08,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x07, 0x08,

  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x07, 0x08
};

#else

unsigned char data[] = {
  0x2e, 0x86, 0x53, 0x10, 0x4f, 0x38, 0x34, 0xea,
  0x4b, 0xd3, 0x88, 0xff, 0x6c, 0xd8, 0x1d, 0x4f,
  0x20, 0xb9, 0xe7, 0x67, 0xb2, 0xfb, 0x14, 0x56,
  0x55, 0x57, 0x93, 0x80, 0xd7, 0x71, 0x38, 0xef,

  0x6c, 0xc5, 0xde, 0xfa, 0xaf, 0x04, 0x51, 0x2f,
  0x0d, 0x9f, 0x27, 0x9b, 0xa5, 0xd8, 0x72, 0x60,
  0xd9, 0x03, 0x1b, 0x02, 0x71, 0xbd, 0x5a, 0x0a,
  0x42, 0x42, 0x50, 0xb3, 0x7c, 0x3d, 0xd9, 0x51,

  0xb8, 0x06, 0x1b, 0x7e, 0xcd, 0x9a, 0x21, 0xe5,
  0xf1, 0x5d, 0x0f, 0x28, 0x6b, 0x65, 0xbd, 0x28,
  0xad, 0xd0, 0xcc, 0x8d, 0x6e, 0x5d, 0xeb, 0xa1,
  0xe6, 0xd5, 0xf8, 0x27, 0x52, 0xad, 0x63, 0xd1,

  0xec, 0xbf, 0xe3, 0xbd, 0x3f, 0x59, 0x1a, 0x5e,
  0xf3, 0x56, 0x83, 0x43, 0x79, 0xd1, 0x65, 0xcd,
  0x2b, 0x9f, 0x98, 0x2f, 0x20, 0x03, 0x7f, 0xa9,
  0x88, 0x9d, 0xe0, 0x68, 0xa1, 0x6f, 0x0b, 0xe6,

  0xe1, 0x9e, 0x27, 0x5d, 0x84, 0x6a, 0x12, 0x98,
  0x32, 0x9a, 0x8e, 0xd5, 0x23, 0xd7, 0x1a, 0xec,
  0xe7, 0xfc, 0xe2, 0x25, 0x57, 0xd2, 0x3c, 0x97,
  0x12, 0xa9, 0xf5, 0x81, 0x7f, 0xf2, 0xd6, 0x5d,

  0xa4, 0x84, 0xc3, 0xad, 0x38, 0xdc, 0x9c, 0x19
};
#endif



