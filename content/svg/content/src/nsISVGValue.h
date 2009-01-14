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
 * The Original Code is the Mozilla SVG project.
 *
 * The Initial Developer of the Original Code is
 * Crocodile Clips Ltd..
 * Portions created by the Initial Developer are Copyright (C) 2001
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Alex Fritze <alex.fritze@crocodile-clips.com> (original author)
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


#ifndef __NS_ISVGVALUE_H__
#define __NS_ISVGVALUE_H__

#include "nsISupports.h"
#include "nsQueryFrame.h"
#include "nsString.h"

class nsISVGValueObserver;

////////////////////////////////////////////////////////////////////////
// nsISVGValue: private interface for svg values

/* This interface is implemented by all value-types (e.g. coords,
  pointlists, matrices) that can be parsed from/to strings. This is
  used for element-properties that are also XML attributes. E.g. the
  'polyline'-element has a 'points'-attribute and a property
  'animatedPoints' in the DOM.

  XXX Observers
*/

// {d8299a5e-af9a-4bad-9845-fb1b6e2eed19}
#define NS_ISVGVALUE_IID \
{ 0xd8299a5e, 0xaf9a, 0x4bad, { 0x98, 0x45, 0xfb, 0x1b, 0x6e, 0x2e, 0xed, 0x19 } }


class nsISVGValue : public nsISupports
{
public:
  enum modificationType {
    mod_other = 0,
    mod_context,
    mod_die
  };

  NS_DECLARE_STATIC_IID_ACCESSOR(NS_ISVGVALUE_IID)
  NS_DECLARE_FRAME_ACCESSOR(nsISVGValue)

  NS_IMETHOD SetValueString(const nsAString& aValue)=0;
  NS_IMETHOD GetValueString(nsAString& aValue)=0;

  NS_IMETHOD AddObserver(nsISVGValueObserver* observer)=0;
  NS_IMETHOD RemoveObserver(nsISVGValueObserver* observer)=0;

  NS_IMETHOD BeginBatchUpdate()=0;
  NS_IMETHOD EndBatchUpdate()=0;
};

NS_DEFINE_STATIC_IID_ACCESSOR(nsISVGValue, NS_ISVGVALUE_IID)

nsresult
NS_CreateSVGGenericStringValue(const nsAString& aValue, nsISVGValue** aResult);

nsresult
NS_CreateSVGStringProxyValue(nsISVGValue* proxiedValue, nsISVGValue** aResult);

#endif // __NS_ISVGVALUE_H__

