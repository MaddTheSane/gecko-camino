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
 * The Initial Developer of the Original Code is
 * CSIRO
 * Portions created by the Initial Developer are Copyright (C) 2007
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s): Marcin Lubonski 
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
 * ***** END LICENSE BLOCK ***** *
 */

#include "sydney_audio.h"
#include <stdio.h>
#include <stdlib.h>

#include <windows.h>
#include <mmreg.h>
#include <mmsystem.h>
#include <math.h>


// FIX ME: block size and block should be determined based on the OggPlay offset 
// for audio track
#define BLOCK_SIZE  1024
#define BLOCK_COUNT 32
#define DEFAULT_DEVICE_NAME "Default WAVE Device"
#define DEFAULT_DEVICE WAVE_MAPPER

#define VERBOSE_OUTPUT 1

// INFO: if you get weird compile errors make sure there is no extra chars pass '\' 
#if defined(VERBOSE_OUTPUT)
#define WAVE_ERROR_VERBOSE(error, message) \
  switch (error) { \
    case MMSYSERR_ALLOCATED: \
      printf("[WAVE API] Device allocation error returned while executing %s\n", message); \
      break; \
    case MMSYSERR_BADDEVICEID: \
      printf("[WAVE API] Wrong device ID error returned while executing %s\n", message); \
      break; \
    case MMSYSERR_NODRIVER: \
      printf("[WAVE API] System driver not present error returned while executing %s\n", message); \
      break; \
    case MMSYSERR_INVALHANDLE: \
      printf("[WAVE API] Invalid device handle error returned while executing %s\n", message); \
      break; \
    case MMSYSERR_NOMEM: \
      printf("[WAVE API] No memory error returned while executing %s\n", message); \
      break; \
    case MMSYSERR_NOTSUPPORTED: \
      printf("[WAVE API] Not supported error returned while executing %s\n", message); \
      break; \
    case WAVERR_BADFORMAT: \
      printf("[WAVE API] Not valid audio format returned while executing %s\n", message); \
      break; \
    case WAVERR_SYNC: \
      printf("[WAVE API] Device synchronous error returned while executing %s\n", message); \
      break; \
    default: \
      printf("[WAVE API] Error while executing %s\n", message); \
      break; \
  }
#else
#define WAVE_ERROR_VERBOSE(error, message) \
  do {} while(0)
#endif

#define HANDLE_WAVE_ERROR(status, location) \
  if (status != MMSYSERR_NOERROR) { \
      WAVE_ERROR_VERBOSE(status, location); \
      return getSAErrorCode(status); \
  }

#define ERROR_IF_NO_INIT(handle) \
  if (handle == NULL) { \
		return SA_ERROR_NO_INIT; \
	}

/* local implementation of the sa_stream_t type */
struct sa_stream {   
  char*           deviceName;
  UINT				    device;
  UINT				    channels;
  UINT				    rate;
	
  sa_mode_t			  rwMode;
  sa_pcm_format_t	format;   
 
  HWAVEOUT			  hWaveOut;
  HANDLE			    callbackEvent;
  CRITICAL_SECTION  waveCriticalSection;  
  WAVEHDR*			  waveBlocks;  
  volatile int		waveFreeBlockCount;
  int				      waveCurrentBlock;
};


/** Forward definitions of audio api specific functions */
int allocateBlocks(int size, int count, WAVEHDR** blocks);
int freeBlocks(WAVEHDR* blocks);
int openAudio(sa_stream_t *s);
int closeAudio(sa_stream_t * s);
int writeAudio(sa_stream_t *s, LPSTR data, int bytes);
int getSAErrorCode(int waveErrorCode);

void CALLBACK waveOutProc(HWAVEOUT hWaveOut, UINT uMsg, 
    DWORD dwInstance, DWORD dwParam1, DWORD dwParam2);

/** Normal way to open a PCM device */
int sa_stream_create_pcm(sa_stream_t **s, 
                         const char *client_name, 
                         sa_mode_t mode, 
                         sa_pcm_format_t format, 
                         unsigned int rate, 
                         unsigned int nchannels) {
  sa_stream_t * _s = NULL;
  
  ERROR_IF_NO_INIT(s);
  
  *s = NULL;
  
  /* FIX ME: for formats different than PCM extend using WAVEFORMATEXTENSIBLE */
  if (format != SA_PCM_FORMAT_S16_NE) {
    return SA_ERROR_NOT_SUPPORTED;
  }

  if (mode != SA_MODE_WRONLY) {
    return SA_ERROR_NOT_SUPPORTED;
  }

  if ((_s = (sa_stream_t*)calloc(1, sizeof(sa_stream_t))) == NULL) {
    return SA_ERROR_OOM;
  }
   
  _s->rwMode = mode;
  _s->format = format;
  _s->rate = rate;
  _s->channels = nchannels;
  _s->deviceName = DEFAULT_DEVICE_NAME;
  _s->device = DEFAULT_DEVICE;

  *s = _s; 
  return SA_SUCCESS;
}

/** Initialise the device */
int sa_stream_open(sa_stream_t *s) {  
  int status = SA_SUCCESS;

  ERROR_IF_NO_INIT(s);

  switch (s->rwMode) {
    case SA_MODE_WRONLY: 
      status = openAudio(s);
      break;
	default:
      status = SA_ERROR_NOT_SUPPORTED;      
      break;
  }    
  return status;
}

/** Interleaved playback function */
int sa_stream_write(sa_stream_t *s, const void *data, size_t nbytes) {
  int status = SA_SUCCESS;

  ERROR_IF_NO_INIT(s);

  status = writeAudio(s, (LPSTR)data, nbytes);

  return status;
}

/** Query how much can be written without blocking */
int sa_stream_get_write_size(sa_stream_t *s, size_t *size) {
  unsigned int avail;
  WAVEHDR* current;

  ERROR_IF_NO_INIT(s);

  EnterCriticalSection(&(s->waveCriticalSection));
  avail = (s->waveFreeBlockCount-1) * BLOCK_SIZE;
  if (s->waveFreeBlockCount != BLOCK_COUNT) {
    current = &(s->waveBlocks[s->waveCurrentBlock]);
    avail += BLOCK_SIZE - current->dwUser;
  }
  LeaveCriticalSection(&(s->waveCriticalSection));

  *size = avail;

  return SA_SUCCESS;
}

/** Close/destroy everything */
int sa_stream_destroy(sa_stream_t *s) {
  int status;

  ERROR_IF_NO_INIT(s);
  /* close and release all allocated resources */
  status = closeAudio(s);

  free(s);

  return status;
}

#define LEFT_CHANNEL_MASK 0x0000FFFF
#define RIGHT_CHANNEL_MASK 0xFFFF0000

/** 
 * retrieved volume as an int in a scale from 0x0000 to 0xFFFF
 * only one value for all channels
 */
int sa_stream_get_write_volume(sa_stream_t *s, int32_t vol[], unsigned int *n) {
  int status;
	DWORD volume;
	WORD left;
	WORD right;

	ERROR_IF_NO_INIT(s);
  
	status = waveOutGetVolume(s->hWaveOut, &volume);
	HANDLE_WAVE_ERROR(status, "reading audio volume level");

	left = volume & LEFT_CHANNEL_MASK;
	right = (volume & RIGHT_CHANNEL_MASK) >> 16;
  vol[0] = (int32_t)(left + right /2);	

	return SA_SUCCESS;

}

/** changes volume as an int in a scale from 0x0000 to 0xFFFF*/
int sa_stream_change_write_volume(sa_stream_t *s, const int32_t vol[], unsigned int n) {
  int status;
	DWORD volume;
	WORD left;
	WORD right;
	
	ERROR_IF_NO_INIT(s);
	
  volume = (DWORD)vol[0];
	left = volume & LEFT_CHANNEL_MASK;	  
	right = left;	  
	volume =  (left << 16) | right;	
	
	status = waveOutSetVolume(s->hWaveOut, volume);
	HANDLE_WAVE_ERROR(status, "setting new audio volume level");	

	return SA_SUCCESS;


}

/** sync/timing */
int sa_stream_get_position(sa_stream_t *s, sa_position_t position, int64_t *pos) {
	int status;
  MMTIME  mm;

  ERROR_IF_NO_INIT(s);

  if (position != SA_POSITION_WRITE_HARDWARE) {
    return SA_ERROR_NOT_SUPPORTED;
  }
  // request playback progress in bytes
  mm.wType = TIME_BYTES;		
	status = waveOutGetPosition(s->hWaveOut, &mm, sizeof(MMTIME));
  HANDLE_WAVE_ERROR(status, "reading audio buffer position");
  *pos = (int64_t)mm.u.cb;

	return SA_SUCCESS;
}

/* Control/xrun */
/** Resume playing after a pause */
int sa_stream_resume(sa_stream_t *s) {
  int status;  
  
  ERROR_IF_NO_INIT(s);

  status = waveOutRestart(s->hWaveOut);
  HANDLE_WAVE_ERROR(status, "resuming audio playback");

  return SA_SUCCESS;
}
/** Pause audio playback (do not empty the buffer) */
int sa_stream_pause(sa_stream_t *s) {
  int status;

  ERROR_IF_NO_INIT(s);
  
  status = waveOutPause(s->hWaveOut);
  HANDLE_WAVE_ERROR(status, "resuming audio playback");

  return SA_SUCCESS;
}
/** Block until all audio has been played */
int sa_stream_drain(sa_stream_t *s) {
  ERROR_IF_NO_INIT(s);
  
  /* wait for all blocks to complete */
  EnterCriticalSection(&(s->waveCriticalSection));
  while(s->waveFreeBlockCount < BLOCK_COUNT) {
    LeaveCriticalSection(&(s->waveCriticalSection));
    Sleep(10);
    EnterCriticalSection(&(s->waveCriticalSection));
  }
  LeaveCriticalSection(&(s->waveCriticalSection));

  return SA_SUCCESS;
}

/*
 * -----------------------------------------------------------------------------
 * Private WAVE API specific functions
 * -----------------------------------------------------------------------------
 */

/** 
 * \brief - allocate buffer for writing to system WAVE audio device
 * \param size - size of each audio block
 * \param cound - number of blocks to be allocated
 * \param blocks - pointer to the blocks buffer to be allocated
 * \return - completion status
 */
int allocateBlocks(int size, int count, WAVEHDR** blocks)
{
  unsigned char* buffer;    
  int i;    
  WAVEHDR* headers;
  DWORD totalBufferSize = (size + sizeof(WAVEHDR)) * count;
    
  /* allocate memory on heap for the entire set in one go  */    
  if((buffer = HeapAlloc(
     GetProcessHeap(), 
     HEAP_ZERO_MEMORY, 
     totalBufferSize
     )) == NULL) {
      printf("Memory allocation error\n");
      return SA_ERROR_OOM;
    }

  /* and set up the pointers to each bit */
  headers = *blocks = (WAVEHDR*)buffer;
  buffer += sizeof(WAVEHDR) * count;
  for(i = 0; i < count; i++) {    
	  headers[i].dwBufferLength = size;
    headers[i].lpData = buffer;
    buffer += size;
  }
    
  return SA_SUCCESS;
}

/**
 * \brief - free allocated audio buffer
 * \param blocks - pointer to allocated the buffer of audio bloks
 * \return - completion status
 */
int freeBlocks(WAVEHDR* blocks)
{    
  if (blocks == NULL) 
    return SA_ERROR_INVALID;

  /* and this is why allocateBlocks works the way it does */     
  HeapFree(GetProcessHeap(), 0, blocks);
  blocks = NULL;

  return SA_SUCCESS;
}

/** 
 * \brief - open system default WAVE device
 * \param s - sydney audio stream handle
 * \return - completion status
 */ 
int openAudio(sa_stream_t *s) {
  int status;
  WAVEFORMATEX wfx;    
  UINT supported = FALSE;
		  
  status = allocateBlocks(BLOCK_SIZE, BLOCK_COUNT, &(s->waveBlocks));  
	HANDLE_WAVE_ERROR(status, "allocating audio buffer blocks");
  
  s->waveFreeBlockCount	= BLOCK_COUNT;
  s->waveCurrentBlock	= 0;  
  wfx.nSamplesPerSec	= (DWORD)s->rate;	/* sample rate */
  wfx.wBitsPerSample	= 16;				/* sample size */
  wfx.nChannels			= s->channels;	/* channels    */
  wfx.cbSize			= 0;				/* size of _extra_ info */
  wfx.wFormatTag		= WAVE_FORMAT_PCM;
  wfx.nBlockAlign		= (wfx.wBitsPerSample * wfx.nChannels) >> 3;
  wfx.nAvgBytesPerSec	= wfx.nBlockAlign * wfx.nSamplesPerSec;

  supported = waveOutOpen(NULL, WAVE_MAPPER, &wfx, (DWORD_PTR)0, (DWORD_PTR)0, 
				WAVE_FORMAT_QUERY);
  if (supported == MMSYSERR_NOERROR) { // audio device opened sucessfully 
    status = waveOutOpen((LPHWAVEOUT)&(s->hWaveOut), WAVE_MAPPER, &wfx, 
	  (DWORD_PTR)waveOutProc, (DWORD_PTR)s, CALLBACK_FUNCTION);
    HANDLE_WAVE_ERROR(status, "opening audio device for playback");
		printf("Audio device sucessfully opened\n");
  } 
  else if (supported == WAVERR_BADFORMAT) {
    printf("Requested format not supported...\n");
	  // clean up the memory
	  freeBlocks(s->waveBlocks);
    return SA_ERROR_NOT_SUPPORTED;
  } 
  else {
    printf("Error opening default audio device. Exiting...\n");
	  // clean up the memory
	  freeBlocks(s->waveBlocks);
    return SA_ERROR_SYSTEM;
  }
  // create notification for data written to a device
  s->callbackEvent = CreateEvent(0, FALSE, FALSE, 0);
  // initialise critical section for operations on waveFreeBlockCound variable
  InitializeCriticalSection(&(s->waveCriticalSection));

  return SA_SUCCESS;
}
/**
 * \brief - closes opened audio device handle
 * \param s - sydney audio stream handle
 * \return - completion status
 */
int closeAudio(sa_stream_t * s) {
  int status, i;
  
  // reseting audio device and flushing buffers
  status = waveOutReset(s->hWaveOut);    
  HANDLE_WAVE_ERROR(status, "resetting audio device");
  
  /* wait for all blocks to complete */  
  while(s->waveFreeBlockCount < BLOCK_COUNT)
	  Sleep(10);

  /* unprepare any blocks that are still prepared */  
  for(i = 0; i < s->waveFreeBlockCount; i++) {
    if(s->waveBlocks[i].dwFlags & WHDR_PREPARED) {
	    status = waveOutUnprepareHeader(s->hWaveOut, &(s->waveBlocks[i]), sizeof(WAVEHDR));
      HANDLE_WAVE_ERROR(status, "closing audio device");
    }
  }    

  freeBlocks(s->waveBlocks);  
  status = waveOutClose(s->hWaveOut);    
  HANDLE_WAVE_ERROR(status, "closing audio device");

  DeleteCriticalSection(&(s->waveCriticalSection));
  CloseHandle(s->callbackEvent);
  printf("[audio] audio resources cleanup completed\n");
  
  return SA_SUCCESS;
}
/**
 * \brief - writes PCM audio samples to audio device
 * \param s - valid handle to opened sydney stream
 * \param data - pointer to memory storing audio samples to be played
 * \param nsamples - number of samples in the memory pointed by previous parameter
 * \return - completion status
 */
int writeAudio(sa_stream_t *s, LPSTR data, int bytes) {    
  UINT status;
  WAVEHDR* current;	  
  int remain;

  current = &(s->waveBlocks[s->waveCurrentBlock]);

  while(bytes > 0) {
     /*
     * wait for a block to become free
     */
    while (!(s->waveFreeBlockCount))
      WaitForSingleObject(s->callbackEvent, INFINITE);

    /* first make sure the header we're going to use is unprepared */
    if(current->dwFlags & WHDR_PREPARED) {      
        status = waveOutUnprepareHeader(s->hWaveOut, current, sizeof(WAVEHDR));         
        HANDLE_WAVE_ERROR(status, "preparing audio headers for writing");
    }
		  
    if(bytes < (int)(BLOCK_SIZE - current->dwUser)) {							  	    
		  memcpy(current->lpData + current->dwUser, data, bytes);
      current->dwUser += bytes;
      break;
    }

    /* remain is even as BLOCK_SIZE and dwUser are even too */
    remain = BLOCK_SIZE - current->dwUser;      
  	memcpy(current->lpData + current->dwUser, data, remain);
    bytes -= remain;
    data += remain;
	  current->dwBufferLength = BLOCK_SIZE;
	  /* write to audio device */
    waveOutPrepareHeader(s->hWaveOut, current, sizeof(WAVEHDR));
	  status = waveOutWrite(s->hWaveOut, current, sizeof(WAVEHDR));      
    HANDLE_WAVE_ERROR(status, "writing audio to audio device");
      
    EnterCriticalSection(&(s->waveCriticalSection));
    s->waveFreeBlockCount--;
    LeaveCriticalSection(&(s->waveCriticalSection));

    /*
     * point to the next block
     */
    (s->waveCurrentBlock)++;
    (s->waveCurrentBlock) %= BLOCK_COUNT;		

    current = &(s->waveBlocks[s->waveCurrentBlock]);
    current->dwUser = 0;
  }
  return SA_SUCCESS;
}

/**
 * \brief - audio callback function called when next WAVE header is played by audio device
 */
void CALLBACK waveOutProc(
    HWAVEOUT hWaveOut, 
    UINT uMsg, 
    DWORD dwInstance,  
    DWORD dwParam1,    
    DWORD dwParam2     
)
{
    /*
     * pointer to free block counter
     */
    sa_stream_t* handle = (sa_stream_t*)dwInstance;
    /*
     * ignore calls that occur due to openining and closing the
     * device.
     */
    if(uMsg != WOM_DONE)
        return;

    EnterCriticalSection(&(handle->waveCriticalSection));
    (handle->waveFreeBlockCount)++;
    if ((handle->waveFreeBlockCount) == 1) 
       SetEvent(handle->callbackEvent);
    LeaveCriticalSection(&(handle->waveCriticalSection));	
}

/**
 * \brief - converts frequently reported WAVE error codes to Sydney audio API codes
 */
int getSAErrorCode(int waveErrorCode) {
  int error = SA_ERROR_NOT_SUPPORTED;

  switch (waveErrorCode) {
    case MMSYSERR_NOERROR: 
      error = SA_SUCCESS;
      break;
    case MMSYSERR_ALLOCATED: 
      error = SA_ERROR_SYSTEM;
      break;
    case MMSYSERR_BADDEVICEID:
      error = SA_ERROR_INVALID;
      break;
    case MMSYSERR_NODRIVER:
      error = SA_ERROR_NO_DRIVER;
      break;
    case MMSYSERR_NOTSUPPORTED:
      error = SA_ERROR_NOT_SUPPORTED;
      break;          
    case MMSYSERR_NOMEM: 
      error = SA_ERROR_OOM;
      break;
    case MMSYSERR_INVALHANDLE:
      error = SA_ERROR_INVALID;
      break;
    case WAVERR_BADFORMAT: 
      error = SA_ERROR_NOT_SUPPORTED;
      break;
    case WAVERR_SYNC: 
      error = SA_ERROR_NOT_SUPPORTED;
      break;    
  }
  return error;
}


/*
 * -----------------------------------------------------------------------------
 * Functions to be implemented next 
 * -----------------------------------------------------------------------------
 */

#define NOT_IMPLEMENTED(func)   func { return SA_ERROR_NOT_SUPPORTED; }

/* "Soft" params */
NOT_IMPLEMENTED(int sa_stream_set_write_lower_watermark(sa_stream_t *s, size_t size))
NOT_IMPLEMENTED(int sa_stream_set_read_lower_watermark(sa_stream_t *s, size_t size))
NOT_IMPLEMENTED(int sa_stream_set_write_upper_watermark(sa_stream_t *s, size_t size))
NOT_IMPLEMENTED(int sa_stream_set_read_upper_watermark(sa_stream_t *s, size_t size))

/** Set the mapping between channels and the loudspeakers */
NOT_IMPLEMENTED(int sa_stream_set_channel_map(sa_stream_t *s, const sa_channel_t map[], unsigned int n))

/* Query functions */
NOT_IMPLEMENTED(int sa_stream_get_mode(sa_stream_t *s, sa_mode_t *access_mode))
NOT_IMPLEMENTED(int sa_stream_get_pcm_format(sa_stream_t *s, sa_pcm_format_t *format))
NOT_IMPLEMENTED(int sa_stream_get_rate(sa_stream_t *s, unsigned int *rate))
NOT_IMPLEMENTED(int sa_stream_get_nchannels(sa_stream_t *s, int *nchannels))
NOT_IMPLEMENTED(int sa_stream_get_device(sa_stream_t *s, char *device_name, size_t *size))
NOT_IMPLEMENTED(int sa_stream_get_write_lower_watermark(sa_stream_t *s, size_t *size))
NOT_IMPLEMENTED(int sa_stream_get_read_lower_watermark(sa_stream_t *s, size_t *size))
NOT_IMPLEMENTED(int sa_stream_get_write_upper_watermark(sa_stream_t *s, size_t *size))
NOT_IMPLEMENTED(int sa_stream_get_read_upper_watermark(sa_stream_t *s, size_t *size))
NOT_IMPLEMENTED(int sa_stream_get_channel_map(sa_stream_t *s, sa_channel_t map[], unsigned int *n))

/*
 * -----------------------------------------------------------------------------
 * Unsupported functions
 * -----------------------------------------------------------------------------
 */
#define UNSUPPORTED(func)   func { return SA_ERROR_NOT_SUPPORTED; }

/** Create an opaque (e.g. AC3) codec stream */
UNSUPPORTED(int sa_stream_create_opaque(sa_stream_t **s, const char *client_name, sa_mode_t mode, const char *codec))
/** Whether xruns cause the card to reset */
UNSUPPORTED(int sa_stream_set_xrun_mode(sa_stream_t *s, sa_xrun_mode_t mode))
/** Set the device to non-interleaved mode */
UNSUPPORTED(int sa_stream_set_non_interleaved(sa_stream_t *s, int enable))
/** Require dynamic sample rate */
UNSUPPORTED(int sa_stream_set_dynamic_rate(sa_stream_t *s, int enable))
/** Select driver */
UNSUPPORTED(int sa_stream_set_driver(sa_stream_t *s, const char *driver))
/** Start callback */
UNSUPPORTED(int sa_stream_start_thread(sa_stream_t *s, sa_event_callback_t callback))
/** Stop callback */
UNSUPPORTED(int sa_stream_stop_thread(sa_stream_t *s))
/** Change the device connected to the stream */
UNSUPPORTED(int sa_stream_change_device(sa_stream_t *s, const char *device_name))
/** volume in hundreths of dB*/
UNSUPPORTED(int sa_stream_change_read_volume(sa_stream_t *s, const int32_t vol[], unsigned int n))
/** Change the sampling rate */
UNSUPPORTED(int sa_stream_change_rate(sa_stream_t *s, unsigned int rate))
/** Change some meta data that is attached to the stream */
UNSUPPORTED(int sa_stream_change_meta_data(sa_stream_t *s, const char *name, const void *data, size_t size))
/** Associate opaque user data */
UNSUPPORTED(int sa_stream_change_user_data(sa_stream_t *s, const void *value))
/* Hardware-related. This is implementation-specific and hardware specific. */
UNSUPPORTED(int sa_stream_set_adjust_rate(sa_stream_t *s, sa_adjust_t direction))
UNSUPPORTED(int sa_stream_set_adjust_nchannels(sa_stream_t *s, sa_adjust_t direction))
UNSUPPORTED(int sa_stream_set_adjust_pcm_format(sa_stream_t *s, sa_adjust_t direction))
UNSUPPORTED(int sa_stream_set_adjust_watermarks(sa_stream_t *s, sa_adjust_t direction))
/* Query functions */
UNSUPPORTED(int sa_stream_get_codec(sa_stream_t *s, char *codec, size_t *size))
UNSUPPORTED(int sa_stream_get_user_data(sa_stream_t *s, void **value))

UNSUPPORTED(int sa_stream_get_xrun_mode(sa_stream_t *s, sa_xrun_mode_t *mode))
UNSUPPORTED(int sa_stream_get_non_interleaved(sa_stream_t *s, int *enabled))
UNSUPPORTED(int sa_stream_get_dynamic_rate(sa_stream_t *s, int *enabled))
UNSUPPORTED(int sa_stream_get_driver(sa_stream_t *s, char *driver_name, size_t *size))            
UNSUPPORTED(int sa_stream_get_read_volume(sa_stream_t *s, int32_t vol[], unsigned int *n))
UNSUPPORTED(int sa_stream_get_meta_data(sa_stream_t *s, const char *name, void*data, size_t *size))
UNSUPPORTED(int sa_stream_get_adjust_rate(sa_stream_t *s, sa_adjust_t *direction))
UNSUPPORTED(int sa_stream_get_adjust_nchannels(sa_stream_t *s, sa_adjust_t *direction))
UNSUPPORTED(int sa_stream_get_adjust_pcm_format(sa_stream_t *s, sa_adjust_t *direction))
UNSUPPORTED(int sa_stream_get_adjust_watermarks(sa_stream_t *s, sa_adjust_t *direction))
/** Get current state of the audio device */
UNSUPPORTED(int sa_stream_get_state(sa_stream_t *s, sa_state_t *state))
/** Obtain the error code */
UNSUPPORTED(int sa_stream_get_event_error(sa_stream_t *s, sa_error_t *error))
/** Obtain the notification code */
UNSUPPORTED(int sa_stream_get_event_notify(sa_stream_t *s, sa_notify_t *notify))

/* Blocking IO calls */
/** Interleaved capture function */
UNSUPPORTED(int sa_stream_read(sa_stream_t *s, void *data, size_t nbytes))
/** Non-interleaved capture function */
UNSUPPORTED(int sa_stream_read_ni(sa_stream_t *s, unsigned int channel, void *data, size_t nbytes))

/** Non-interleaved playback function */
UNSUPPORTED(int sa_stream_write_ni(sa_stream_t *s, unsigned int channel, const void *data, size_t nbytes))
/** Interleaved playback function with seek offset */
UNSUPPORTED(int sa_stream_pwrite(sa_stream_t *s, const void *data, size_t nbytes, int64_t offset, sa_seek_t whence))
/** Non-interleaved playback function with seek offset */
UNSUPPORTED(int sa_stream_pwrite_ni(sa_stream_t *s, unsigned int channel, const void *data, size_t nbytes, int64_t offset, sa_seek_t whence))

/** Query how much can be read without blocking */
UNSUPPORTED(int sa_stream_get_read_size(sa_stream_t *s, size_t *size))

/** Return a human readable error */
const char *sa_strerror(int code);
