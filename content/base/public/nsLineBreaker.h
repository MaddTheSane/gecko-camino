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
 * The Original Code is Novell code.
 *
 * The Initial Developer of the Original Code is Novell.
 * Portions created by the Initial Developer are Copyright (C) 2006
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *         Robert O'Callahan <robert@ocallahan.org> (Original Author)
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

#ifndef NSLINEBREAKER_H_
#define NSLINEBREAKER_H_

#include "nsString.h"
#include "nsTArray.h"

class nsIAtom;

/**
 * A receiver of line break data.
 */
class nsILineBreakSink {
public:
  /**
   * Sets the break data for a substring of the associated text chunk.
   * One or more of these calls will be performed; the union of all substrings
   * will cover the entire text chunk. Substrings may overlap (i.e., we may
   * set the break-before state of a character more than once).
   * @param aBreakBefore the break-before states for the characters in the substring.
   */
  virtual void SetBreaks(PRUint32 aStart, PRUint32 aLength, PRPackedBool* aBreakBefore) = 0;
};

/**
 * A line-breaking state machine. You feed text into it via AppendText calls
 * and it computes the possible line breaks. Because break decisions can
 * require a lot of context, the breaks for a piece of text are sometimes not
 * known until later text has been seen (or all text ends). So breaks are
 * returned via a call to SetBreaks on the nsILineBreakSink object passed
 * with each text chunk, which might happen during the corresponding AppendText
 * call, or might happen during a later AppendText call or even a Reset()
 * call.
 * 
 * The linebreak results MUST NOT depend on how the text is broken up
 * into AppendText calls.
 * 
 * The current strategy is that we break the overall text into
 * whitespace-delimited "words". Then for words that contain a "complex" 
 * character (currently CJK or Thai), we break within the word using complex
 * rules (JISx4051 or Pango).
 */
class nsLineBreaker {
public:
  nsLineBreaker();
  ~nsLineBreaker();

  // Normally, break opportunities exist at the end of each run of whitespace
  // (Unicode ZWSP (U+200B) and ASCII space (U+0020)). Break opportunities can
  // also exist inside runs of non-whitespace, as determined by nsILineBreaker.
  // We provide flags to control on a per-chunk basis where breaks are allowed.
  // At any character boundary, exactly one text chunk governs whether a
  // break is allowed at that boundary.
  //
  // We operate on text after whitespace processing has been applied, so
  // other characters (e.g. tabs and newlines) may have been converted to
  // spaces.
  enum {
    /**
     * Allow a break opportunity at the start of this chunk of text.
     */
    BREAK_ALLOW_INITIAL = 0x01,
    /**
     * Allow a break opportunity in the interior of this chunk of text.
     */
    BREAK_ALLOW_INSIDE = 0x02
  };

  /**
   * Append "invisible whitespace". This acts like whitespace, but there is
   * no actual text associated with it.
   */
  nsresult AppendInvisibleWhitespace();

  /**
   * Feed Unicode text into the linebreaker for analysis. aLength must be
   * nonzero.
   */
  nsresult AppendText(nsIAtom* aLangGroup, const PRUnichar* aText, PRUint32 aLength,
                      PRUint32 aFlags, nsILineBreakSink* aSink);
  /**
   * Feed 8-bit text into the linebreaker for analysis. aLength must be nonzero.
   */
  nsresult AppendText(nsIAtom* aLangGroup, const PRUint8* aText, PRUint32 aLength,
                      PRUint32 aFlags, nsILineBreakSink* aSink);
  /**
   * Reset all state. This means the current run has ended; any outstanding
   * calls through nsILineBreakSink are made, and all outstanding references to
   * nsILineBreakSink objects are dropped.
   * After this call, this linebreaker can be reused.
   * This must be called at least once between any call to AppendText() and
   * destroying the object.
   */
  nsresult Reset() { return FlushCurrentWord(); }

private:
  // This is a list of text sources that make up the "current word" (i.e.,
  // run of text which does not contain any whitespace). All the mLengths
  // are are nonzero, these cannot overlap.
  struct TextItem {
    TextItem(nsILineBreakSink* aSink, PRUint32 aSinkOffset, PRUint32 aLength,
             PRUint32 aFlags)
      : mSink(aSink), mSinkOffset(aSinkOffset), mLength(aLength), mFlags(aFlags) {}

    nsILineBreakSink* mSink;
    PRUint32          mSinkOffset;
    PRUint32          mLength;
    PRUint32          mFlags;
  };

  // State for the nonwhitespace "word" that started in previous text and hasn't
  // finished yet.

  // When the current word ends, this computes the linebreak opportunities
  // *inside* the word (excluding either end) and sets them through the
  // appropriate sink(s). Then we clear the current word state.
  nsresult FlushCurrentWord();

  nsAutoTArray<PRUnichar,100> mCurrentWord;
  // All the items that contribute to mCurrentWord
  nsAutoTArray<TextItem,2>    mTextItems;
  PRPackedBool                mCurrentWordContainsCJK;

  // True if the previous character was whitespace
  PRPackedBool                mAfterSpace;
};

#endif /*NSLINEBREAKER_H_*/
