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
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998-1999
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Pierre Phaneuf <pp@ludusdesign.com>
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

#include "EditTxn.h"

NS_IMPL_CYCLE_COLLECTION_0(EditTxn)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(EditTxn)
  NS_INTERFACE_MAP_ENTRY(nsITransaction)
  NS_INTERFACE_MAP_ENTRY(nsPIEditorTransaction)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsITransaction)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF_AMBIGUOUS(EditTxn, nsITransaction)
NS_IMPL_CYCLE_COLLECTING_RELEASE_AMBIGUOUS(EditTxn, nsITransaction)

EditTxn::~EditTxn()
{
}

NS_IMETHODIMP
EditTxn::RedoTransaction(void)
{
  return DoTransaction();
}

NS_IMETHODIMP
EditTxn::GetIsTransient(PRBool *aIsTransient)
{
  *aIsTransient = PR_FALSE;

  return NS_OK;
}

NS_IMETHODIMP
EditTxn::Merge(nsITransaction *aTransaction, PRBool *aDidMerge)
{
  *aDidMerge = PR_FALSE;

  return NS_OK;
}
