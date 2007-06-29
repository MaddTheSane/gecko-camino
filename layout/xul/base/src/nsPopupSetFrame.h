/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
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
 * The Original Code is mozilla.org Code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
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

//
// nsPopupSetFrame
//

#ifndef nsPopupSetFrame_h__
#define nsPopupSetFrame_h__

#include "prtypes.h"
#include "nsIAtom.h"
#include "nsCOMPtr.h"
#include "nsGkAtoms.h"

#include "nsBoxFrame.h"
#include "nsFrameList.h"
#include "nsMenuPopupFrame.h"
#include "nsIMenuParent.h"
#include "nsITimer.h"
#include "nsISupportsArray.h"

nsIFrame* NS_NewPopupSetFrame(nsIPresShell* aPresShell, nsStyleContext* aContext);

struct nsPopupFrameList {
  nsPopupFrameList* mNextPopup;  // The next popup in the list.
  nsMenuPopupFrame* mPopupFrame; // Our popup.
  nsIContent* mPopupContent;     // The content element for the <popup> itself.

public:
  nsPopupFrameList(nsIContent* aPopupContent, nsPopupFrameList* aNext);
};

class nsPopupSetFrame : public nsBoxFrame
{
public:
  nsPopupSetFrame(nsIPresShell* aShell, nsStyleContext* aContext):
    nsBoxFrame(aShell, aContext) {}

  ~nsPopupSetFrame() {}
  
  NS_IMETHOD Init(nsIContent*      aContent,
                  nsIFrame*        aParent,
                  nsIFrame*        aPrevInFlow);
  NS_IMETHOD AppendFrames(nsIAtom*        aListName,
                          nsIFrame*       aFrameList);
  NS_IMETHOD RemoveFrame(nsIAtom*        aListName,
                         nsIFrame*       aOldFrame);
  NS_IMETHOD InsertFrames(nsIAtom*        aListName,
                          nsIFrame*       aPrevFrame,
                          nsIFrame*       aFrameList);
  NS_IMETHOD  SetInitialChildList(nsIAtom*        aListName,
                                  nsIFrame*       aChildList);

    // nsIBox
  NS_IMETHOD DoLayout(nsBoxLayoutState& aBoxLayoutState);

  // Used to destroy our popup frames.
  virtual void Destroy();

  virtual nsIAtom* GetType() const { return nsGkAtoms::popupSetFrame; }

#ifdef DEBUG
  NS_IMETHOD GetFrameName(nsAString& aResult) const
  {
      return MakeFrameName(NS_LITERAL_STRING("PopupSet"), aResult);
  }
#endif

protected:

  nsresult AddPopupFrameList(nsIFrame* aPopupFrameList);
  nsresult AddPopupFrame(nsIFrame* aPopup);
  nsresult RemovePopupFrame(nsIFrame* aPopup);
  
  nsPopupFrameList* mPopupList;

}; // class nsPopupSetFrame

#endif
