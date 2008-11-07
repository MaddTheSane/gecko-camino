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
#if !defined(nsAudioStream_h_)
#define nsAudioStream_h_

#include "nscore.h"
#include "prlog.h"

extern PRLogModuleInfo* gAudioStreamLog;

class nsAudioStream 
{
 public:
  // Initialize Audio Library. Some Audio backends (eg. PortAudio) require initializing
  // library before using it. 
  static void InitLibrary();

  // Shutdown Audio Library. Some Audio backends (eg. PortAudio) require shutting down
  // the library after using it. 
  static void ShutdownLibrary();

  nsAudioStream();

  // Initialize the audio stream. aNumChannels is the number of audio channels 
  // (1 for mono, 2 for stereo, etc) and aRate is the frequency of the sound 
  // samples (22050, 44100, etc).
  void Init(PRInt32 aNumChannels, PRInt32 aRate);

  // Closes the stream. All future use of the stream is an error.
  void Shutdown();

  // Write sound data to the audio hardware. aBuf is an array of floats of
  // length aCount. aCount should be evenly divisible by the number of 
  // channels in this audio stream.
  void Write(const float* aBuf, PRUint32 aCount);

  // Write sound data to the audio hardware.  aBuf is an array of shorts in
  // signed 16-bit little endian format of length aCount.  Acount should be
  // evenly divisible by the number of channels in this audio stream.
  void Write(const short* aBuf, PRUint32 aCount);

  // Return the number of sound samples that can be written to the audio device
  // without blocking.
  PRInt32 Available();

  // Store in aVolume the value of the volume setting. This is a value from
  // 0 (meaning muted) to 1 (meaning full volume).
  float GetVolume();

  // Set the current volume of the audio playback. This is a value from
  // 0 (meaning muted) to 1 (meaning full volume).
  void SetVolume(float aVolume);

  // Block until buffered audio data has been consumed.
  void Drain();

  // Pause sound playback.
  void Pause();

  // Resume sound playback.
  void Resume();

  // Return the position (in seconds) of the audio sample currently being
  // played by the audio hardware.
  double GetTime();

 private:
  double mVolume;
  void* mAudioHandle;
  int mRate;
  int mChannels;

  // The byte position in the audio buffer where playback was last paused.
  PRInt64 mSavedPauseBytes;
  PRInt64 mPauseBytes;

  float mStartTime;
  float mPauseTime;
  PRInt64 mSamplesBuffered;

  PRPackedBool mPaused;
};
#endif
