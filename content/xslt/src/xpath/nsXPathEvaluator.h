/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
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
 * The Original Code is TransforMiiX XSLT processor code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2001
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Peter Van der Beken <peterv@propagandism.org>
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

#ifndef nsXPathEvaluator_h__
#define nsXPathEvaluator_h__

#include "nsIDOMXPathEvaluator.h"
#include "nsIXPathEvaluatorInternal.h"
#include "nsIWeakReference.h"
#include "nsAutoPtr.h"
#include "nsString.h"
#include "txResultRecycler.h"
#include "nsAgg.h"
#include "nsTArray.h"

/**
 * A class for evaluating an XPath expression string
 */
class nsXPathEvaluator : public nsIDOMXPathEvaluator,
                         public nsIXPathEvaluatorInternal
{
public:
    nsXPathEvaluator(nsISupports *aOuter);

    nsresult Init();

    // nsISupports interface (support aggregation)
    NS_DECL_AGGREGATED

    // nsIDOMXPathEvaluator interface
    NS_DECL_NSIDOMXPATHEVALUATOR

    // nsIXPathEvaluatorInternal interface
    NS_IMETHOD SetDocument(nsIDOMDocument* aDocument);
    NS_IMETHOD CreateExpression(const nsAString &aExpression, 
                                nsIDOMXPathNSResolver *aResolver,
                                nsTArray<nsString> *aNamespaceURIs,
                                nsTArray<nsCString> *aContractIDs,
                                nsCOMArray<nsISupports> *aState,
                                nsIDOMXPathExpression **aResult);

private:
    nsresult CreateExpression(const nsAString & aExpression,
                              nsIDOMXPathNSResolver *aResolver,
                              nsTArray<PRInt32> *aNamespaceIDs,
                              nsTArray<nsCString> *aContractIDs,
                              nsCOMArray<nsISupports> *aState,
                              nsIDOMXPathExpression **aResult);

    nsWeakPtr mDocument;
    nsRefPtr<txResultRecycler> mRecycler;
};

/* d0a75e02-b5e7-11d5-a7f2-df109fb8a1fc */
#define TRANSFORMIIX_XPATH_EVALUATOR_CID   \
{ 0xd0a75e02, 0xb5e7, 0x11d5, { 0xa7, 0xf2, 0xdf, 0x10, 0x9f, 0xb8, 0xa1, 0xfc } }

#endif
