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
 * Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2007
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Alexander Surkov <surkov.alexander@gmail.com> (original author)
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

#include "nsAccessibilityUtils.h"

#include "nsPIAccessible.h"
#include "nsAccessibleEventData.h"

#include "nsIDOMXULSelectCntrlEl.h"
#include "nsIDOMXULSelectCntrlItemEl.h"
#include "nsIEventListenerManager.h"

void
nsAccUtils::GetAccAttr(nsIPersistentProperties *aAttributes, nsIAtom *aAttrName,
                       nsAString& aAttrValue)
{
  nsCAutoString attrName;
  aAttrName->ToUTF8String(attrName);
  aAttributes->GetStringProperty(attrName, aAttrValue);
}

void
nsAccUtils::SetAccAttr(nsIPersistentProperties *aAttributes, nsIAtom *aAttrName,
                       const nsAString& aAttrValue)
{
  nsAutoString oldValue;
  nsCAutoString attrName;

  aAttrName->ToUTF8String(attrName);
  aAttributes->SetStringProperty(attrName, aAttrValue, oldValue);
}

void
nsAccUtils::GetAccGroupAttrs(nsIPersistentProperties *aAttributes,
                             PRInt32 *aLevel, PRInt32 *aPosInSet,
                             PRInt32 *aSetSize)
{
  *aLevel = 0;
  *aPosInSet = 0;
  *aSetSize = 0;

  nsAutoString value;
  PRInt32 error = NS_OK;

  GetAccAttr(aAttributes, nsAccessibilityAtoms::level, value);
  if (!value.IsEmpty()) {
    PRInt32 level = value.ToInteger(&error);
    if (NS_SUCCEEDED(error))
      *aLevel = level;
  }

  GetAccAttr(aAttributes, nsAccessibilityAtoms::posinset, value);
  if (!value.IsEmpty()) {
    PRInt32 posInSet = value.ToInteger(&error);
    if (NS_SUCCEEDED(error))
      *aPosInSet = posInSet;
  }

  GetAccAttr(aAttributes, nsAccessibilityAtoms::setsize, value);
  if (!value.IsEmpty()) {
    PRInt32 sizeSet = value.ToInteger(&error);
    if (NS_SUCCEEDED(error))
      *aSetSize = sizeSet;
  }
}

PRBool
nsAccUtils::HasAccGroupAttrs(nsIPersistentProperties *aAttributes)
{
  nsAutoString value;

  GetAccAttr(aAttributes, nsAccessibilityAtoms::setsize, value);
  if (!value.IsEmpty()) {
    GetAccAttr(aAttributes, nsAccessibilityAtoms::posinset, value);
    return !value.IsEmpty();
  }

  return PR_FALSE;
}

void
nsAccUtils::SetAccGroupAttrs(nsIPersistentProperties *aAttributes,
                             PRInt32 aLevel, PRInt32 aPosInSet,
                             PRInt32 aSetSize)
{
  nsAutoString value;

  if (aLevel) {
    value.AppendInt(aLevel);
    SetAccAttr(aAttributes, nsAccessibilityAtoms::level, value);
  }

  if (aSetSize && aPosInSet) {
    value.Truncate();
    value.AppendInt(aPosInSet);
    SetAccAttr(aAttributes, nsAccessibilityAtoms::posinset, value);

    value.Truncate();
    value.AppendInt(aSetSize);
    SetAccAttr(aAttributes, nsAccessibilityAtoms::setsize, value);
  }
}

void
nsAccUtils::SetAccAttrsForXULSelectControlItem(nsIDOMNode *aNode,
                                               nsIPersistentProperties *aAttributes)
{
  nsCOMPtr<nsIDOMXULSelectControlItemElement> item(do_QueryInterface(aNode));
  if (!item)
    return;

  nsCOMPtr<nsIDOMXULSelectControlElement> control;
  item->GetControl(getter_AddRefs(control));
  if (!control)
    return;

  PRUint32 itemsCount;
  control->GetItemCount(&itemsCount);
  PRInt32 indexOf;
  control->GetIndexOfItem(item, &indexOf);

  SetAccGroupAttrs(aAttributes, 0, indexOf + 1, itemsCount);
}

PRBool
nsAccUtils::HasListener(nsIContent *aContent, const nsAString& aEventType)
{
  NS_ENSURE_ARG_POINTER(aContent);
  nsCOMPtr<nsIEventListenerManager> listenerManager;
  aContent->GetListenerManager(PR_FALSE, getter_AddRefs(listenerManager));

  return listenerManager && listenerManager->HasListenersFor(aEventType);  
}

nsresult
nsAccUtils::FireAccEvent(PRUint32 aEventType, nsIAccessible *aAccessible,
                         PRBool aIsAsynch)
{
  NS_ENSURE_ARG(aAccessible);

  nsCOMPtr<nsPIAccessible> pAccessible(do_QueryInterface(aAccessible));
  NS_ASSERTION(pAccessible, "Accessible doesn't implement nsPIAccessible");

  nsCOMPtr<nsIAccessibleEvent> event =
    new nsAccEvent(aEventType, aAccessible, nsnull, aIsAsynch);
  NS_ENSURE_TRUE(event, NS_ERROR_OUT_OF_MEMORY);

  return pAccessible->FireAccessibleEvent(event);
}

PRBool
nsAccUtils::IsAncestorOf(nsIDOMNode *aPossibleAncestorNode,
                         nsIDOMNode *aPossibleDescendantNode)
{
  NS_ENSURE_ARG_POINTER(aPossibleAncestorNode);
  NS_ENSURE_ARG_POINTER(aPossibleDescendantNode);

  nsCOMPtr<nsIDOMNode> loopNode = aPossibleDescendantNode;
  nsCOMPtr<nsIDOMNode> parentNode;
  while (NS_SUCCEEDED(loopNode->GetParentNode(getter_AddRefs(parentNode))) &&
         parentNode) {
    if (parentNode == aPossibleAncestorNode) {
      return PR_TRUE;
    }
    loopNode.swap(parentNode);
  }
  return PR_FALSE;
}

