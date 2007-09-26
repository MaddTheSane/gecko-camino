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

#include "nsXPathResult.h"
#include "txExprResult.h"
#include "txNodeSet.h"
#include "nsDOMError.h"
#include "nsIContent.h"
#include "nsIAttribute.h"
#include "nsIDOMClassInfo.h"
#include "nsIDOMNode.h"
#include "nsIDOMDocument.h"
#include "nsDOMString.h"
#include "txXPathTreeWalker.h"
#include "nsCycleCollectionParticipant.h"

nsXPathResult::nsXPathResult() : mDocument(nsnull),
                                 mCurrentPos(0),
                                 mResultType(ANY_TYPE),
                                 mInvalidIteratorState(PR_TRUE)
{
}

nsXPathResult::~nsXPathResult()
{
    RemoveObserver();
}

NS_IMPL_CYCLE_COLLECTION_CLASS(nsXPathResult)
NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(nsXPathResult)
    {
        tmp->RemoveObserver();
    }
    NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mDocument)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(nsXPathResult)
    NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mDocument)
    {
        txAExprResult *result = tmp->mResult.get();
        if (result && result->getResultType() == txAExprResult::NODESET) {
            txNodeSet *nodeSet = static_cast<txNodeSet*>(result);
            nsCOMPtr<nsIDOMNode> node;
            PRInt32 count = nodeSet->size();
            PRInt32 i;
            for (i = 0; i < count; ++i) {
                txXPathNativeNode::getNode(nodeSet->get(i),
                                           getter_AddRefs(node));
                cb.NoteXPCOMChild(node);
            }
        }
    }
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTING_ADDREF_AMBIGUOUS(nsXPathResult, nsIDOMXPathResult)
NS_IMPL_CYCLE_COLLECTING_RELEASE_AMBIGUOUS(nsXPathResult, nsIDOMXPathResult)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsXPathResult)
    NS_INTERFACE_MAP_ENTRY(nsIDOMXPathResult)
    NS_INTERFACE_MAP_ENTRY(nsIMutationObserver)
    NS_INTERFACE_MAP_ENTRY(nsIXPathResult)
    NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIDOMXPathResult)
    NS_INTERFACE_MAP_ENTRY_CONTENT_CLASSINFO(XPathResult)
NS_INTERFACE_MAP_END

void
nsXPathResult::RemoveObserver()
{
    if (mDocument) {
        mDocument->RemoveMutationObserver(this);
    }
}

NS_IMETHODIMP
nsXPathResult::GetResultType(PRUint16 *aResultType)
{
    *aResultType = mResultType;

    return NS_OK;
}

NS_IMETHODIMP
nsXPathResult::GetNumberValue(double *aNumberValue)
{
    if (mResultType != NUMBER_TYPE) {
        return NS_ERROR_DOM_TYPE_ERR;
    }

    *aNumberValue = mResult.get()->numberValue();

    return NS_OK;
}

NS_IMETHODIMP
nsXPathResult::GetStringValue(nsAString &aStringValue)
{
    if (mResultType != STRING_TYPE) {
        return NS_ERROR_DOM_TYPE_ERR;
    }

    nsAutoString stringValue;
    mResult.get()->stringValue(stringValue);

    aStringValue.Assign(stringValue);

    return NS_OK;
}

NS_IMETHODIMP
nsXPathResult::GetBooleanValue(PRBool *aBooleanValue)
{
    if (mResultType != BOOLEAN_TYPE) {
        return NS_ERROR_DOM_TYPE_ERR;
    }

    *aBooleanValue = mResult.get()->booleanValue();

    return NS_OK;
}

NS_IMETHODIMP
nsXPathResult::GetSingleNodeValue(nsIDOMNode **aSingleNodeValue)
{
    if (!isNode()) {
        return NS_ERROR_DOM_TYPE_ERR;
    }

    txNodeSet *nodeSet = static_cast<txNodeSet*>(mResult.get());
    if (nodeSet->size() > 0) {
        return txXPathNativeNode::getNode(nodeSet->get(0), aSingleNodeValue);
    }

    *aSingleNodeValue = nsnull;

    return NS_OK;
}

NS_IMETHODIMP
nsXPathResult::GetInvalidIteratorState(PRBool *aInvalidIteratorState)
{
    *aInvalidIteratorState = isIterator() && mInvalidIteratorState;

    return NS_OK;
}

NS_IMETHODIMP
nsXPathResult::GetSnapshotLength(PRUint32 *aSnapshotLength)
{
    if (!isSnapshot()) {
        return NS_ERROR_DOM_TYPE_ERR;
    }

    txNodeSet *nodeSet = static_cast<txNodeSet*>(mResult.get());
    *aSnapshotLength = (PRUint32)nodeSet->size();

    return NS_OK;
}

NS_IMETHODIMP
nsXPathResult::IterateNext(nsIDOMNode **aResult)
{
    if (!isIterator()) {
        return NS_ERROR_DOM_TYPE_ERR;
    }

    if (mDocument) {
        mDocument->FlushPendingNotifications(Flush_Content);
    }

    if (mInvalidIteratorState) {
        return NS_ERROR_DOM_INVALID_STATE_ERR;
    }

    txNodeSet *nodeSet = static_cast<txNodeSet*>(mResult.get());
    if (mCurrentPos < (PRUint32)nodeSet->size()) {
        return txXPathNativeNode::getNode(nodeSet->get(mCurrentPos++),
                                          aResult);
    }

    *aResult = nsnull;

    return NS_OK;
}

NS_IMETHODIMP
nsXPathResult::SnapshotItem(PRUint32 aIndex, nsIDOMNode **aResult)
{
    if (!isSnapshot()) {
        return NS_ERROR_DOM_TYPE_ERR;
    }

    txNodeSet *nodeSet = static_cast<txNodeSet*>(mResult.get());
    if (aIndex < (PRUint32)nodeSet->size()) {
        return txXPathNativeNode::getNode(nodeSet->get(aIndex), aResult);
    }

    *aResult = nsnull;

    return NS_OK;
}

void
nsXPathResult::NodeWillBeDestroyed(const nsINode* aNode)
{
    // Set to null to avoid unregistring unnecessarily
    mDocument = nsnull;
    Invalidate(aNode->IsNodeOfType(nsINode::eCONTENT) ?
               static_cast<const nsIContent*>(aNode) : nsnull);
}

void
nsXPathResult::CharacterDataChanged(nsIDocument* aDocument,
                                    nsIContent *aContent,
                                    CharacterDataChangeInfo* aInfo)
{
    Invalidate(aContent);
}

void
nsXPathResult::AttributeChanged(nsIDocument* aDocument,
                                nsIContent* aContent,
                                PRInt32 aNameSpaceID,
                                nsIAtom* aAttribute,
                                PRInt32 aModType,
                                PRUint32 aStateMask)
{
    Invalidate(aContent);
}

void
nsXPathResult::ContentAppended(nsIDocument* aDocument,
                               nsIContent* aContainer,
                               PRInt32 aNewIndexInContainer)
{
    Invalidate(aContainer);
}

void
nsXPathResult::ContentInserted(nsIDocument* aDocument,
                               nsIContent* aContainer,
                               nsIContent* aChild,
                               PRInt32 aIndexInContainer)
{
    Invalidate(aContainer);
}

void
nsXPathResult::ContentRemoved(nsIDocument* aDocument,
                              nsIContent* aContainer,
                              nsIContent* aChild,
                              PRInt32 aIndexInContainer)
{
    Invalidate(aContainer);
}

nsresult
nsXPathResult::SetExprResult(txAExprResult* aExprResult, PRUint16 aResultType,
                             nsINode* aContextNode)
{
    mResultType = aResultType;
    mContextNode = do_GetWeakReference(aContextNode);

    if ((isSnapshot() || isIterator() || isNode()) &&
        aExprResult->getResultType() != txAExprResult::NODESET) {
        return NS_ERROR_DOM_TYPE_ERR;
    }

    if (mDocument) {
        mDocument->RemoveMutationObserver(this);
        mDocument = nsnull;
    }
 
    mResult.set(aExprResult);

    if (!isIterator()) {
        return NS_OK;
    }

    mInvalidIteratorState = PR_FALSE;

    txNodeSet* nodeSet = static_cast<txNodeSet*>(aExprResult);
    nsCOMPtr<nsIDOMNode> node;
    if (nodeSet->size() > 0) {
        nsresult rv = txXPathNativeNode::getNode(nodeSet->get(0),
                                                 getter_AddRefs(node));
        NS_ENSURE_SUCCESS(rv, rv);

        // If we support the document() function in DOM-XPath we need to
        // observe all documents that we have resultnodes in.
        nsCOMPtr<nsIDOMDocument> document;
        node->GetOwnerDocument(getter_AddRefs(document));
        if (document) {
            mDocument = do_QueryInterface(document);
        }
        else {
            mDocument = do_QueryInterface(node);
        }

        NS_ASSERTION(mDocument, "We need a document!");
        if (mDocument) {
            mDocument->AddMutationObserver(this);
        }
    }

    return NS_OK;
}

void
nsXPathResult::Invalidate(const nsIContent* aChangeRoot)
{
    nsCOMPtr<nsINode> contextNode = do_QueryReferent(mContextNode);
    if (contextNode && aChangeRoot && aChangeRoot->GetBindingParent()) {
        // If context node is in anonymous content, changes to
        // non-anonymous content need to invalidate the XPathResult. If
        // the changes are happening in a different anonymous trees, no
        // invalidation should happen.
        nsIContent* ctxBindingParent = nsnull;
        if (contextNode->IsNodeOfType(nsINode::eCONTENT)) {
            ctxBindingParent =
                static_cast<nsIContent*>(contextNode.get())
                    ->GetBindingParent();
        } else if (contextNode->IsNodeOfType(nsINode::eATTRIBUTE)) {
            nsIContent* parent =
              static_cast<nsIAttribute*>(contextNode.get())->GetContent();
            if (parent) {
                ctxBindingParent = parent->GetBindingParent();
            }
        }
        if (ctxBindingParent != aChangeRoot->GetBindingParent()) {
          return;
        }
    }

    if (mDocument) {
        mDocument->RemoveMutationObserver(this);
        mDocument = nsnull;
    }
    mInvalidIteratorState = PR_TRUE;
}

nsresult
nsXPathResult::GetExprResult(txAExprResult** aExprResult)
{
    if (isIterator() && mInvalidIteratorState) {
        return NS_ERROR_DOM_INVALID_STATE_ERR;
    }

    *aExprResult = mResult.get();
    if (!*aExprResult) {
        return NS_ERROR_DOM_INVALID_STATE_ERR;
    }

    NS_ADDREF(*aExprResult);

    return NS_OK;
}

nsresult
nsXPathResult::Clone(nsIXPathResult **aResult)
{
    *aResult = nsnull;

    if (isIterator() && mInvalidIteratorState) {
        return NS_ERROR_DOM_INVALID_STATE_ERR;
    }

    nsCOMPtr<nsIXPathResult> result = new nsXPathResult();
    if (!result) {
        return NS_ERROR_OUT_OF_MEMORY;
    }

    nsCOMPtr<nsINode> contextNode = do_QueryReferent(mContextNode);
    nsresult rv = result->SetExprResult(mResult.get(), mResultType,
                                        contextNode);
    NS_ENSURE_SUCCESS(rv, rv);

    result.swap(*aResult);

    return NS_OK;
}

void
txResultHolder::set(txAExprResult *aResult)
{
    releaseNodeSet();

    // XXX This will keep the recycler alive, should we clear it?
    mResult = aResult;

    if (mResult && mResult->getResultType() == txAExprResult::NODESET) {
        txNodeSet *nodeSet =
            static_cast<txNodeSet*>
                       (static_cast<txAExprResult*>(mResult));
        PRInt32 i, count = nodeSet->size();
        for (i = 0; i < count; ++i) {
            txXPathNativeNode::addRef(nodeSet->get(i));
        }
    }
}

void
txResultHolder::releaseNodeSet()
{
    if (mResult && mResult->getResultType() == txAExprResult::NODESET) {
        txNodeSet *nodeSet =
            static_cast<txNodeSet*>
                       (static_cast<txAExprResult*>(mResult));
        PRInt32 i, count = nodeSet->size();
        for (i = 0; i < count; ++i) {
            txXPathNativeNode::release(nodeSet->get(i));
        }
    }
}
