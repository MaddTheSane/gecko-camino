/* vim: set shiftwidth=2 tabstop=8 autoindent cindent expandtab: */
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
 * The Original Code is nsStyleStructInlines.h.
 *
 * The Initial Developer of the Original Code is the Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2008
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   L. David Baron <dbaron@dbaron.org>, Mozilla Corporation (original author)
 *   Rob Arnold <robarnold@mozilla.com>
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

/*
 * Inline methods that belong in nsStyleStruct.h, except that they
 * require more headers.
 */

#ifndef nsStyleStructInlines_h_
#define nsStyleStructInlines_h_

#include "nsStyleStruct.h"
#include "imgIRequest.h"

inline void
nsStyleBorder::SetBorderImage(imgIRequest* aImage)
{
  mBorderImage = aImage;
  RebuildActualBorder();
}

inline imgIRequest*
nsStyleBorder::GetBorderImage() const
{
  return mBorderImage;
}

inline PRBool nsStyleBorder::HasVisibleStyle(PRUint8 aSide)
{
  PRUint8 style = GetBorderStyle(aSide);
  return (style != NS_STYLE_BORDER_STYLE_NONE &&
          style != NS_STYLE_BORDER_STYLE_HIDDEN);
}

inline void nsStyleBorder::SetBorderWidth(PRUint8 aSide, nscoord aBorderWidth)
{
  nscoord roundedWidth =
    NS_ROUND_BORDER_TO_PIXELS(aBorderWidth, mTwipsPerPixel);
  mBorder.side(aSide) = roundedWidth;
  if (HasVisibleStyle(aSide))
    mComputedBorder.side(aSide) = roundedWidth;
}

inline void nsStyleBorder::SetBorderImageWidthOverride(PRUint8 aSide,
                                                       nscoord aBorderWidth)
{
  mBorderImageWidth.side(aSide) =
    NS_ROUND_BORDER_TO_PIXELS(aBorderWidth, mTwipsPerPixel);
}

inline void nsStyleBorder::RebuildActualBorderSide(PRUint8 aSide)
{
  mComputedBorder.side(aSide) =
    (HasVisibleStyle(aSide) ? mBorder.side(aSide) : 0);
}

inline void nsStyleBorder::SetBorderStyle(PRUint8 aSide, PRUint8 aStyle)
{
  NS_ASSERTION(aSide <= NS_SIDE_LEFT, "bad side"); 
  mBorderStyle[aSide] &= ~BORDER_STYLE_MASK; 
  mBorderStyle[aSide] |= (aStyle & BORDER_STYLE_MASK);
  RebuildActualBorderSide(aSide);
}

inline void nsStyleBorder::RebuildActualBorder()
{
  NS_FOR_CSS_SIDES(side) {
    RebuildActualBorderSide(side);
  }
}

inline PRBool nsStyleBorder::IsBorderImageLoaded() const
{
  PRUint32 status;
  return mBorderImage &&
         NS_SUCCEEDED(mBorderImage->GetImageStatus(&status)) &&
         (status & imgIRequest::STATUS_FRAME_COMPLETE);
}

#endif /* !defined(nsStyleStructInlines_h_) */
