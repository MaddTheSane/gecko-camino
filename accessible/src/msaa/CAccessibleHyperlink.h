/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:expandtab:shiftwidth=2:tabstop=2:
 */
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
 * Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2007
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Alexander Surkov <surkov.alexander@gmail.com> (original author)
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

#ifndef _ACCESSIBLE_HYPERLINK_H
#define _ACCESSIBLE_HYPERLINK_H

#include "nsISupports.h"

#include "CAccessibleAction.h"
#include "AccessibleHyperlink.h"

class CAccessibleHyperlink: public CAccessibleAction,
                            public IAccessibleHyperlink
{
public:

  // IUnknown
  STDMETHODIMP QueryInterface(REFIID, void**);

  // IAccessibleAction
  FORWARD_IACCESSIBLEACTION(CAccessibleAction)

  virtual /* [propget] */ HRESULT STDMETHODCALLTYPE get_anchor(
      /* [in] */ long index,
      /* [retval][out] */ VARIANT *anchor);

  virtual /* [propget] */ HRESULT STDMETHODCALLTYPE get_anchorTarget(
      /* [in] */ long index,
      /* [retval][out] */ VARIANT *anchorTarget);

  virtual /* [propget] */ HRESULT STDMETHODCALLTYPE get_startIndex(
      /* [retval][out] */ long *index);

  virtual /* [propget] */ HRESULT STDMETHODCALLTYPE get_endIndex(
      /* [retval][out] */ long *index);

  virtual /* [propget] */ HRESULT STDMETHODCALLTYPE get_valid(
      /* [retval][out] */ boolean *valid);

};

#endif

