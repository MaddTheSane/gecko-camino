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
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Daniel Glazman <glazman@netscape.com>
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

/*
 * representation of a declaration block (or style attribute) in a CSS
 * stylesheet
 */

#ifndef nsCSSDeclaration_h___
#define nsCSSDeclaration_h___

#include "nsISupports.h"
#include "nsColor.h"
#include <stdio.h>
#include "nsString.h"
#include "nsCoord.h"
#include "nsCSSValue.h"
#include "nsCSSProps.h"
#include "nsTArray.h"
#include "nsCSSDataBlock.h"
#include "nsCSSStruct.h"

class nsCSSDeclaration {
public:
  /**
   * Construct an |nsCSSDeclaration| that is in an invalid state (null
   * |mData|) and cannot be used until its |CompressFrom| method or
   * |InitializeEmpty| method is called.
   */
  nsCSSDeclaration();

  nsCSSDeclaration(const nsCSSDeclaration& aCopy);

  /**
   * |ValueAppended| must be called to maintain this declaration's
   * |mOrder| whenever a property is parsed into an expanded data block
   * for this declaration.  aProperty must not be a shorthand.
   */
  nsresult ValueAppended(nsCSSProperty aProperty);

  nsresult AppendComment(const nsAString& aComment);
  nsresult RemoveProperty(nsCSSProperty aProperty);

  nsresult GetValue(nsCSSProperty aProperty, nsAString& aValue) const;

  PRBool HasImportantData() const { return mImportantData != nsnull; }
  PRBool GetValueIsImportant(nsCSSProperty aProperty) const;
  PRBool GetValueIsImportant(const nsAString& aProperty) const;

  PRUint32 Count() const {
    return mOrder.Length(); 
  }
  nsresult GetNthProperty(PRUint32 aIndex, nsAString& aReturn) const;

  nsresult ToString(nsAString& aString) const;

  nsCSSDeclaration* Clone() const;

  nsresult MapRuleInfoInto(nsRuleData *aRuleData) const {
    return mData->MapRuleInfoInto(aRuleData);
  }

  nsresult MapImportantRuleInfoInto(nsRuleData *aRuleData) const {
    return mImportantData->MapRuleInfoInto(aRuleData);
  }

  /**
   * Initialize this declaration as holding no data.  Return false on
   * out-of-memory.
   */
  PRBool InitializeEmpty();

  /**
   * Transfer all of the state from |aExpandedData| into this declaration.
   * After calling, |aExpandedData| should be in its initial state.
   */
  void CompressFrom(nsCSSExpandedDataBlock *aExpandedData) {
    NS_ASSERTION(!mData, "oops");
    NS_ASSERTION(!mImportantData, "oops");
    aExpandedData->Compress(&mData, &mImportantData);
    aExpandedData->AssertInitialState();
  }

  /**
   * Transfer all of the state from this declaration into
   * |aExpandedData| and put this declaration temporarily into an
   * invalid state (ended by |CompressFrom| or |InitializeEmpty|) that
   * should last only during parsing.  During this time only
   * |ValueAppended| should be called.
   */
  void ExpandTo(nsCSSExpandedDataBlock *aExpandedData) {
    aExpandedData->AssertInitialState();

    NS_ASSERTION(mData, "oops");
    aExpandedData->Expand(&mData, &mImportantData);
    NS_ASSERTION(!mData && !mImportantData,
                 "Expand didn't null things out");
  }

  /**
   * Return a pointer to our current value for this property.  This only
   * returns non-null if the property is set and it not !important.  This
   * should only be called when not expanded.  Always returns null for
   * shorthand properties.
   */
  void* SlotForValue(nsCSSProperty aProperty) {
    NS_PRECONDITION(mData, "How did that happen?");
    if (nsCSSProps::IsShorthand(aProperty)) {
      return nsnull;
    }

    void* slot = mData->SlotForValue(aProperty);

    NS_ASSERTION(!slot || !mImportantData ||
                 !mImportantData->SlotForValue(aProperty),
                 "Property both important and not?");
    return slot;
  }

  /**
   * Clear the data, in preparation for its replacement with entirely
   * new data by a call to |CompressFrom|.
   */
  void ClearData() {
    mData->Destroy();
    mData = nsnull;
    if (mImportantData) {
      mImportantData->Destroy();
      mImportantData = nsnull;
    }
    mOrder.Clear();
  }

#ifdef DEBUG
  void List(FILE* out = stdout, PRInt32 aIndent = 0) const;
#endif
  
  // return whether there was a value in |aValue| (i.e., it had a non-null unit)
  static PRBool AppendCSSValueToString(nsCSSProperty aProperty,
                                       const nsCSSValue& aValue,
                                       nsAString& aResult);

private:
  // Not implemented, and not supported.
  nsCSSDeclaration& operator=(const nsCSSDeclaration& aCopy);
  PRBool operator==(const nsCSSDeclaration& aCopy) const;

  static void AppendImportanceToString(PRBool aIsImportant, nsAString& aString);
  // return whether there was a value in |aValue| (i.e., it had a non-null unit)
  PRBool   AppendValueToString(nsCSSProperty aProperty, nsAString& aResult) const;
  // Helper for ToString with strange semantics regarding aValue.
  void     AppendPropertyAndValueToString(nsCSSProperty aProperty,
                                          nsAutoString& aValue,
                                          nsAString& aResult) const;

private:
    //
    // Specialized ref counting.
    // We do not want everyone to ref count us, only the rules which hold
    //  onto us (our well defined lifetime is when the last rule releases
    //  us).
    // It's worth a comment here that the main nsCSSDeclaration is refcounted,
    //  but it's |mImportant| is not refcounted, but just owned by the
    //  non-important declaration.
    //
    friend class CSSStyleRuleImpl;
    void AddRef(void) {
      if (mRefCnt == PR_UINT32_MAX) {
        NS_WARNING("refcount overflow, leaking object");
        return;
      }
      ++mRefCnt;
    }
    void Release(void) {
      if (mRefCnt == PR_UINT32_MAX) {
        NS_WARNING("refcount overflow, leaking object");
        return;
      }
      NS_ASSERTION(0 < mRefCnt, "bad Release");
      if (0 == --mRefCnt) {
        delete this;
      }
    }
public:
    void RuleAbort(void) {
      NS_ASSERTION(0 == mRefCnt, "bad RuleAbort");
      delete this;
    }
private:
  // Block everyone, except us or a derivative, from deleting us.
  ~nsCSSDeclaration(void);
    
  nsCSSProperty OrderValueAt(PRUint32 aValue) const {
    return nsCSSProperty(mOrder.ElementAt(aValue));
  }

private:
    nsAutoTArray<PRUint8, 8> mOrder;
    nsAutoRefCnt mRefCnt;
    nsCSSCompressedDataBlock *mData; // never null, except while expanded
    nsCSSCompressedDataBlock *mImportantData; // may be null
};

#endif /* nsCSSDeclaration_h___ */
