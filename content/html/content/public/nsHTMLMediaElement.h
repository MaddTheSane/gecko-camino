/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
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
 * The Original Code is Mozilla code.
 *
 * The Initial Developer of the Original Code is the Mozilla Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2007
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *  Chris Double <chris.double@double.co.nz>
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
#include "nsIDOMHTMLMediaElement.h"
#include "nsGenericHTMLElement.h"
#include "nsMediaDecoder.h"

// Define to output information on decoding and painting framerate
/* #define DEBUG_FRAME_RATE 1 */

typedef PRUint16 nsMediaNetworkState;
typedef PRUint16 nsMediaReadyState;

class nsHTMLMediaElement : public nsGenericHTMLElement
{
public:
  nsHTMLMediaElement(nsINodeInfo *aNodeInfo, PRBool aFromParser = PR_FALSE);
  virtual ~nsHTMLMediaElement();

  // nsIDOMHTMLMediaElement
  NS_DECL_NSIDOMHTMLMEDIAELEMENT

  virtual PRBool ParseAttribute(PRInt32 aNamespaceID,
                                nsIAtom* aAttribute,
                                const nsAString& aValue,
                                nsAttrValue& aResult);
  // SetAttr override.  C++ is stupid, so have to override both
  // overloaded methods.
  nsresult SetAttr(PRInt32 aNameSpaceID, nsIAtom* aName,
                   const nsAString& aValue, PRBool aNotify)
  {
    return SetAttr(aNameSpaceID, aName, nsnull, aValue, aNotify);
  }
  virtual nsresult SetAttr(PRInt32 aNameSpaceID, nsIAtom* aName,
                           nsIAtom* aPrefix, const nsAString& aValue,
                           PRBool aNotify);

  virtual nsresult BindToTree(nsIDocument* aDocument, nsIContent* aParent,
                              nsIContent* aBindingParent,
                              PRBool aCompileEventHandlers);
  virtual void UnbindFromTree(PRBool aDeep = PR_TRUE,
                              PRBool aNullParent = PR_TRUE);

  virtual PRBool IsDoneAddingChildren();
  virtual nsresult DoneAddingChildren(PRBool aHaveNotified);
  virtual void DestroyContent();

  // Called by the video decoder object, on the main thread,
  // when it has read the metadata containing video dimensions,
  // etc.
  void MetadataLoaded();

  // Called by the video decoder object, on the main thread,
  // when it has read the first frame of the video
  void FirstFrameLoaded();

  // Called by the video decoder object, on the main thread,
  // when the resource has completed downloading.
  void ResourceLoaded();

  // Called by the video decoder object, on the main thread,
  // when the resource has a network error during loading.
  void NetworkError();

  // Called by the video decoder object, on the main thread,
  // when the video playback has ended.
  void PlaybackEnded();

  // Called by the decoder object, on the main thread, when
  // approximately enough of the resource has been loaded to play
  // through without pausing for buffering.
  void CanPlayThrough();

  // Called by the video decoder object, on the main thread,
  // when the resource has started seeking.
  void SeekStarted();

  // Called by the video decoder object, on the main thread,
  // when the resource has completed seeking.
  void SeekCompleted();

  // Draw the latest video data. See nsMediaDecoder for 
  // details.
  void Paint(gfxContext* aContext, const gfxRect& aRect);

  // Dispatch events
  nsresult DispatchSimpleEvent(const nsAString& aName);
  nsresult DispatchProgressEvent(const nsAString& aName);
  nsresult DispatchAsyncSimpleEvent(const nsAString& aName);
  nsresult DispatchAsyncProgressEvent(const nsAString& aName);

  // Use this method to change the mReadyState member, so required
  // events can be fired.
  void ChangeReadyState(nsMediaReadyState aState);

  // Is the media element actively playing as defined by the HTML 5 specification.
  // http://www.whatwg.org/specs/web-apps/current-work/#actively
  PRBool IsActivelyPlaying() const;

  // Has playback ended as defined by the HTML 5 specification.
  // http://www.whatwg.org/specs/web-apps/current-work/#ended
  PRBool IsPlaybackEnded() const;

  // principal of the currently playing stream
  nsIPrincipal* GetCurrentPrincipal();

  // Update the visual size of the media. Called from the decoder on the
  // main thread when/if the size changes.
  void UpdateMediaSize(nsIntSize size);

protected:
  nsresult PickMediaElement(nsAString& aChosenMediaResource);
  virtual nsresult InitializeDecoder(nsAString& aChosenMediaResource);

  nsRefPtr<nsMediaDecoder> mDecoder;

  // Error attribute
  nsCOMPtr<nsIDOMHTMLMediaError> mError;

  // Media loading flags. See: 
  //   http://www.whatwg.org/specs/web-apps/current-work/#video)
  nsMediaNetworkState mNetworkState;
  nsMediaReadyState mReadyState;

  // Value of the volume before it was muted
  float mMutedVolume;

  // Size of the media. Updated by the decoder on the main thread if
  // it changes. Defaults to a width and height of -1 if not set.
  nsIntSize mMediaSize;

  // The defaultPlaybackRate attribute gives the desired speed at
  // which the media resource is to play, as a multiple of its
  // intrinsic speed.
  float mDefaultPlaybackRate;

  // The playbackRate attribute gives the speed at which the media
  // resource plays, as a multiple of its intrinsic speed. If it is
  // not equal to the defaultPlaybackRate, then the implication is
  // that the user is using a feature such as fast forward or slow
  // motion playback.
  float mPlaybackRate;

  // If true then we have begun downloading the media content.
  // Set to false when completed, or not yet started.
  PRPackedBool mBegun;

  // If truen then the video playback has completed.
  PRPackedBool mEnded;

  // True when the decoder has loaded enough data to display the
  // first frame of the content.
  PRPackedBool mLoadedFirstFrame;

  // Indicates whether current playback is a result of user action
  // (ie. calling of the Play method), or automatic playback due to
  // the 'autoplay' attribute being set. A true value indicates the 
  // latter case.
  // The 'autoplay' HTML attribute indicates that the video should
  // start playing when loaded. The 'autoplay' attribute of the object
  // is a mirror of the HTML attribute. These are different from this
  // 'mAutoplaying' flag, which indicates whether the current playback
  // is a result of the autoplay attribute.
  PRPackedBool mAutoplaying;

  // Playback of the video is paused either due to calling the
  // 'Pause' method, or playback not yet having started.
  PRPackedBool mPaused;

  // True if the sound is muted
  PRPackedBool mMuted;

  // Flag to indicate if the child elements (eg. <source/>) have been
  // parsed.
  PRPackedBool mIsDoneAddingChildren;

  // If TRUE then the media element was actively playing before the currently
  // in progress seeking. If FALSE then the media element is either not seeking
  // or was not actively playing before the current seek. Used to decide whether
  // to raise the 'waiting' event as per 4.7.1.8 in HTML 5 specification.
  PRPackedBool mPlayingBeforeSeek;
};
