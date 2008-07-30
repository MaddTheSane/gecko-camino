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
 * Contributor(s): Michael Martin
 *                 Chris Double (chris.double@double.co.nz)
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
#include <stdlib.h>
#include <pthread.h>
#include <alsa/asoundlib.h>
#include "sydney_audio.h"

/* ALSA implementation based heavily on sydney_audio_mac.c */

/*
 * The audio interface is based on a "pull" I/O model, which means you
 * can't just provide a data buffer and tell the audio device to play; you must
 * register a callback and provide data as the device asks for it. To support
 * sydney audio's "write-to-play" style interface, we have to buffer up the
 * data as it arrives and feed it to the callback as required.
 *
 * This is handled by a simple linked list of buffers; data is always written
 * to the tail and read from the head. Each buffer tracks the start and end
 * positions of its contained data. Buffers are allocated when the tail buffer
 * fills, and freed when the head buffer empties. There is always at least one
 * buffer allocated.
 *
 *       s   e      s      e      s  e            + data read
 *    +++#####  ->  ########  ->  ####----        # data written
 *    ^                           ^               - empty
 *    bl_head                     bl_tail
 */

typedef struct sa_buf sa_buf;
struct sa_buf {
  unsigned int      size;
  unsigned int      start;
  unsigned int      end;
  sa_buf          * next;
  unsigned char     data[0];
};

struct sa_stream {
  snd_pcm_t*        output_unit;
  pthread_t         thread_id;
  pthread_mutex_t   mutex;
  char              playing;
  int64_t           bytes_played;

  /* audio format info */
  unsigned int      rate;
  unsigned int      n_channels;
  unsigned int      bytes_per_ch;

  /* buffer list */
  sa_buf          * bl_head;
  sa_buf          * bl_tail;
  int               n_bufs;
};


/*
 * Use a default buffer size with enough room for one second of audio,
 * assuming stereo data at 44.1kHz with 32 bits per channel, and impose
 * a generous limit on the number of buffers.
 */
#define BUF_SIZE    (2 * 44100 * 4)
#define BUF_LIMIT   5

#if BUF_LIMIT < 2
#error BUF_LIMIT must be at least 2!
#endif

static void audio_callback(void* s);
static sa_buf *new_buffer(void);


/*
 * -----------------------------------------------------------------------------
 * Startup and shutdown functions
 * -----------------------------------------------------------------------------
 */

int
sa_stream_create_pcm(
  sa_stream_t      ** _s,
  const char        * client_name,
  sa_mode_t           mode,
  sa_pcm_format_t     format,
  unsigned  int       rate,
  unsigned  int       n_channels
) {
  sa_stream_t   * s = 0;

  /*
   * Make sure we return a NULL stream pointer on failure.
   */
  if (_s == NULL) {
    return SA_ERROR_INVALID;
  }
  *_s = NULL;

  if (mode != SA_MODE_WRONLY) {
    return SA_ERROR_NOT_SUPPORTED;
  }
  if (format != SA_PCM_FORMAT_S16_LE) {
    return SA_ERROR_NOT_SUPPORTED;
  }

  /*
   * Allocate the instance and required resources.
   */
  if ((s = malloc(sizeof(sa_stream_t))) == NULL) {
    return SA_ERROR_OOM;
  }
  if ((s->bl_head = new_buffer()) == NULL) {
    free(s);
    return SA_ERROR_OOM;
  }
  if (pthread_mutex_init(&s->mutex, NULL) != 0) {
    free(s->bl_head);
    free(s);
    return SA_ERROR_SYSTEM;
  }

  s->output_unit  = NULL;
  s->thread_id    = 0;
  s->playing      = 0;
  s->bytes_played = 0;
  s->rate         = rate;
  s->n_channels   = n_channels;
  s->bytes_per_ch = 2;
  s->bl_tail      = s->bl_head;
  s->n_bufs       = 1;

  *_s = s;
  return SA_SUCCESS;
}


int
sa_stream_open(sa_stream_t *s) {

  if (s == NULL) {
    return SA_ERROR_NO_INIT;
  }
  if (s->output_unit != NULL) {
    return SA_ERROR_INVALID;
  }

  if (snd_pcm_open(&s->output_unit, 
		   "default", 
		   SND_PCM_STREAM_PLAYBACK, 
		   0) < 0) {
    return SA_ERROR_NO_DEVICE;
  }
  
  if (snd_pcm_set_params(s->output_unit,
			 SND_PCM_FORMAT_S16_LE,
			 SND_PCM_ACCESS_RW_INTERLEAVED,
			 s->n_channels,
			 s->rate,
			 1,
			 0) < 0) {
    snd_pcm_close(s->output_unit);
    s->output_unit = NULL;
    return SA_ERROR_NOT_SUPPORTED;
  }

  return SA_SUCCESS;
}


int
sa_stream_destroy(sa_stream_t *s) {
  int result = SA_SUCCESS;

  if (s == NULL) {
    return SA_SUCCESS;
  }

  pthread_mutex_lock(&s->mutex);

  /*
   * This causes the thread sending data to ALSA to stop
   */
  s->thread_id = 0;

  /*
   * Shut down the audio output device.
   */
  if (s->output_unit != NULL) {
    if (s->playing && snd_pcm_close(s->output_unit) < 0) {
      result = SA_ERROR_SYSTEM;
    }
  }

  pthread_mutex_unlock(&s->mutex);

  /*
   * Release resources.
   */
  if (pthread_mutex_destroy(&s->mutex) != 0) {
    result = SA_ERROR_SYSTEM;
  }
  while (s->bl_head != NULL) {
    sa_buf  * next = s->bl_head->next;
    free(s->bl_head);
    s->bl_head = next;
  }
  free(s);

  return result;
}



/*
 * -----------------------------------------------------------------------------
 * Data read and write functions
 * -----------------------------------------------------------------------------
 */

int
sa_stream_write(sa_stream_t *s, const void *data, size_t nbytes) {
  int result = SA_SUCCESS;

  if (s == NULL || s->output_unit == NULL) {
    return SA_ERROR_NO_INIT;
  }
  if (nbytes == 0) {
    return SA_SUCCESS;
  }

  pthread_mutex_lock(&s->mutex);

  /*
   * Append the new data to the end of our buffer list.
   */
  while (1) {
    unsigned int avail = s->bl_tail->size - s->bl_tail->end;

    if (nbytes <= avail) {

      /*
       * The new data will fit into the current tail buffer, so
       * just copy it in and we're done.
       */
      memcpy(s->bl_tail->data + s->bl_tail->end, data, nbytes);
      s->bl_tail->end += nbytes;
      break;

    } else {

      /*
       * Copy what we can into the tail and allocate a new buffer
       * for the rest.
       */
      memcpy(s->bl_tail->data + s->bl_tail->end, data, avail);
      s->bl_tail->end += avail;
      data = ((unsigned char *)data) + avail;
      nbytes -= avail;

      /* 
       * If we still have data left to copy but we've hit the limit of
       * allowable buffer allocations, we need to spin for a bit to allow
       * the audio callback function to slurp some more data up.
       */
      if (nbytes > 0 && s->n_bufs == BUF_LIMIT) {
#ifdef TIMING_TRACE
        printf("#");  /* too much audio data */
#endif
        if (!s->playing) {
          /*
           * We haven't even started playing yet! That means the
           * BUF_SIZE/BUF_LIMIT values are too low... Not much we can
           * do here; spinning won't help because the audio callback
           * hasn't been enabled yet. Oh well, error time.
           */
          printf("Too much audio data received before audio device enabled!\n");
          result = SA_ERROR_SYSTEM;
          break;
        }
        while (s->n_bufs == BUF_LIMIT) {
          struct timespec ts = {0, 1000000};
          pthread_mutex_unlock(&s->mutex);
          nanosleep(&ts, NULL);
          pthread_mutex_lock(&s->mutex);
        }
      }

      /* 
       * Allocate a new tail buffer, and go 'round again to fill it up.
       */
      if ((s->bl_tail->next = new_buffer()) == NULL) {
        result = SA_ERROR_OOM;
        break;
      }
      s->n_bufs++;
      s->bl_tail = s->bl_tail->next;
    
    } /* if (nbytes <= avail), else */

  } /* while (1) */

  pthread_mutex_unlock(&s->mutex);

  /*
   * Once we have our first block of audio data, enable the audio callback
   * function. This doesn't need to be protected by the mutex, because
   * s->playing is not used in the audio callback thread, and it's probably
   * better not to be inside the lock when we enable the audio callback.
   */
  if (!s->playing) {
    s->playing = 1;
    if (pthread_create(&s->thread_id, NULL, (void *)audio_callback, s) != 0) {
      result = SA_ERROR_SYSTEM;
    }
  }

  return result;
}


static void audio_callback(void* data)
{
  sa_stream_t* s = (sa_stream_t*)data;
  snd_pcm_uframes_t buffer_size;
  snd_pcm_uframes_t period_size;
  unsigned int bytes_per_frame = s->n_channels * s->bytes_per_ch;
  char* buffer = 0;

#ifdef TIMING_TRACE
  printf(".");  /* audio read 'tick' */
#endif

  snd_pcm_get_params(s->output_unit, &buffer_size, &period_size);
 
  buffer = malloc(period_size * bytes_per_frame);
 
  while(1) {
   char* dst = buffer;
   unsigned int bytes_to_copy   = period_size * bytes_per_frame;
   snd_pcm_sframes_t frames;

   pthread_mutex_lock(&s->mutex);
   if (!s->thread_id)
     break;

    /*
     * Consume data from the start of the buffer list.
     */
    while (1) {
      unsigned int avail = s->bl_head->end - s->bl_head->start;
      assert(s->bl_head->start <= s->bl_head->end);

      if (avail >= bytes_to_copy) {
	/*
	 * We have all we need in the head buffer, so just grab it and go.
	 */
	memcpy(dst, s->bl_head->data + s->bl_head->start, bytes_to_copy);
	s->bl_head->start += bytes_to_copy;
	s->bytes_played += bytes_to_copy;
	break;
	
      } else {
	sa_buf* next = 0;
	/*
	 * Copy what we can from the head and move on to the next buffer.
	 */
	memcpy(dst, s->bl_head->data + s->bl_head->start, avail);
	s->bl_head->start += avail;
	dst += avail;
	bytes_to_copy -= avail;
	s->bytes_played += avail;

	/*
	 * We want to free the now-empty buffer, but not if it's also the
	 * current tail. If it is the tail, we don't have enough data to fill
	 * the destination buffer, so we'll just zero it out and give up.
	 */
	next = s->bl_head->next;
	if (next == NULL) {
#ifdef TIMING_TRACE
	  printf("!");  /* not enough audio data */
#endif
	  memset(dst, 0, bytes_to_copy);
	  break;
	}
	free(s->bl_head);
	s->bl_head = next;
	s->n_bufs--;
	
      } /* if (avail >= bytes_to_copy), else */
      
    } /* while (1) */
    
    pthread_mutex_unlock(&s->mutex);
    
    frames = snd_pcm_writei(s->output_unit, buffer, period_size);
    if (frames < 0) {
      frames = snd_pcm_recover(s->output_unit, frames, 1);
      if (frames < 0) {
	printf("snc_pcm_recover error: %s\n", snd_strerror(frames));
      }
      if(frames > 0 && frames < period_size)
	printf("short write (expected %d, wrote %d)\n", (int)period_size, (int)frames);;
    }
  }
  free(buffer);
}



/*
 * -----------------------------------------------------------------------------
 * General query and support functions
 * -----------------------------------------------------------------------------
 */

int
sa_stream_get_write_size(sa_stream_t *s, size_t *size) {
  sa_buf  * b;
  size_t    used = 0;

  if (s == NULL || s->output_unit == NULL) {
    return SA_ERROR_NO_INIT;
  }

  pthread_mutex_lock(&s->mutex);

  /*
   * Sum up the used portions of our buffers and subtract that from
   * the pre-defined max allowed allocation.
   */
  for (b = s->bl_head; b != NULL; b = b->next) {
    used += b->end - b->start;
  }
  *size = BUF_SIZE * BUF_LIMIT - used;

  pthread_mutex_unlock(&s->mutex);
  return SA_SUCCESS;
}


int
sa_stream_get_position(sa_stream_t *s, sa_position_t position, int64_t *pos) {

  if (s == NULL || s->output_unit == NULL) {
    return SA_ERROR_NO_INIT;
  }
  if (position != SA_POSITION_WRITE_SOFTWARE) {
    return SA_ERROR_NOT_SUPPORTED;
  }

  pthread_mutex_lock(&s->mutex);
  *pos = s->bytes_played;
  pthread_mutex_unlock(&s->mutex);
  return SA_SUCCESS;
}


int
sa_stream_pause(sa_stream_t *s) {

  if (s == NULL || s->output_unit == NULL) {
    return SA_ERROR_NO_INIT;
  }

  pthread_mutex_lock(&s->mutex);
#if 0 /* TODO */
  AudioOutputUnitStop(s->output_unit);
#endif
  pthread_mutex_unlock(&s->mutex);
  return SA_SUCCESS;
}


int
sa_stream_resume(sa_stream_t *s) {

  if (s == NULL || s->output_unit == NULL) {
    return SA_ERROR_NO_INIT;
  }

  pthread_mutex_lock(&s->mutex);

  /*
   * The audio device resets its mSampleTime counter after pausing,
   * so we need to clear our tracking value to keep that in sync.
   */
  s->bytes_played = 0;
#if 0 /* TODO */
  AudioOutputUnitStart(s->output_unit);
#endif
  pthread_mutex_unlock(&s->mutex);
  return SA_SUCCESS;
}


static sa_buf *
new_buffer(void) {
  sa_buf  * b = malloc(sizeof(sa_buf) + BUF_SIZE);
  if (b != NULL) {
    b->size  = BUF_SIZE;
    b->start = 0;
    b->end   = 0;
    b->next  = NULL;
  }
  return b;
}



/*
 * -----------------------------------------------------------------------------
 * Extension functions
 * -----------------------------------------------------------------------------
 */

int
sa_stream_set_volume_abs(sa_stream_t *s, float vol) {
  snd_mixer_t* mixer = 0;
  snd_mixer_elem_t* elem = 0;
  if (s == NULL || s->output_unit == NULL) {
    return SA_ERROR_NO_INIT;
  }

  if (snd_mixer_open(&mixer, 0) < 0) {
    return SA_ERROR_SYSTEM;
  }

  if (snd_mixer_attach(mixer, "default") < 0) {
    snd_mixer_close(mixer);
    return SA_ERROR_SYSTEM;
  }

  if (snd_mixer_selem_register(mixer, NULL, NULL) < 0) {
    snd_mixer_close(mixer);
    return SA_ERROR_SYSTEM;
  }

  if (snd_mixer_load(mixer) < 0) {
    snd_mixer_close(mixer);
    return SA_ERROR_SYSTEM;
  }

#if 0
  snd_mixer_elem_t* elem = 0;
  for (elem = snd_mixer_first_elem(mixer); elem != NULL; elem = snd_mixer_elem_next(elem)) {
    if (snd_mixer_selem_has_playback_volume(elem)) {
      printf("Playback %s\n", snd_mixer_selem_get_name(elem));
    }
    else {
      printf("No Playback: %s\n", snd_mixer_selem_get_name(elem));
    }
  }
#endif
  elem = snd_mixer_first_elem(mixer);
  if (elem && snd_mixer_selem_has_playback_volume(elem)) {
    long min = 0;
    long max = 0;
    if (snd_mixer_selem_get_playback_volume_range(elem, &min, &max) >= 0) {
      snd_mixer_selem_set_playback_volume_all(elem, (max-min)*vol + min);
    } 
  }
  snd_mixer_close(mixer);

  return SA_SUCCESS;
}


int
sa_stream_get_volume_abs(sa_stream_t *s, float *vol) {
  snd_mixer_t* mixer = 0;
  snd_mixer_elem_t* elem = 0;
  long value = 0;

  if (s == NULL || s->output_unit == NULL) {
    return SA_ERROR_NO_INIT;
  }

  if (snd_mixer_open(&mixer, 0) < 0) {
    return SA_ERROR_SYSTEM;
  }

  if (snd_mixer_attach(mixer, "default") < 0) {
    snd_mixer_close(mixer);
    return SA_ERROR_SYSTEM;
  }

  if (snd_mixer_selem_register(mixer, NULL, NULL) < 0) {
    snd_mixer_close(mixer);
    return SA_ERROR_SYSTEM;
  }

  if (snd_mixer_load(mixer) < 0) {
    snd_mixer_close(mixer);
    return SA_ERROR_SYSTEM;
  }

  elem = snd_mixer_first_elem(mixer);
  if (elem && snd_mixer_selem_get_playback_volume(elem, 0, &value) >= 0) {
    long min = 0;
    long max = 0;
    if (snd_mixer_selem_get_playback_volume_range(elem, &min, &max) >= 0) {
      *vol = (float)(value-min)/(float)(max-min);
    } 
  }
  snd_mixer_close(mixer);

  return SA_SUCCESS;
}



/*
 * -----------------------------------------------------------------------------
 * Unsupported functions
 * -----------------------------------------------------------------------------
 */
#define UNSUPPORTED(func)   func { return SA_ERROR_NOT_SUPPORTED; }

UNSUPPORTED(int sa_stream_create_opaque(sa_stream_t **s, const char *client_name, sa_mode_t mode, const char *codec))
UNSUPPORTED(int sa_stream_set_write_lower_watermark(sa_stream_t *s, size_t size))
UNSUPPORTED(int sa_stream_set_read_lower_watermark(sa_stream_t *s, size_t size))
UNSUPPORTED(int sa_stream_set_write_upper_watermark(sa_stream_t *s, size_t size))
UNSUPPORTED(int sa_stream_set_read_upper_watermark(sa_stream_t *s, size_t size))
UNSUPPORTED(int sa_stream_set_channel_map(sa_stream_t *s, const sa_channel_t map[], unsigned int n))
UNSUPPORTED(int sa_stream_set_xrun_mode(sa_stream_t *s, sa_xrun_mode_t mode))
UNSUPPORTED(int sa_stream_set_non_interleaved(sa_stream_t *s, int enable))
UNSUPPORTED(int sa_stream_set_dynamic_rate(sa_stream_t *s, int enable))
UNSUPPORTED(int sa_stream_set_driver(sa_stream_t *s, const char *driver))
UNSUPPORTED(int sa_stream_start_thread(sa_stream_t *s, sa_event_callback_t callback))
UNSUPPORTED(int sa_stream_stop_thread(sa_stream_t *s))
UNSUPPORTED(int sa_stream_change_device(sa_stream_t *s, const char *device_name))
UNSUPPORTED(int sa_stream_change_read_volume(sa_stream_t *s, const int32_t vol[], unsigned int n))
UNSUPPORTED(int sa_stream_change_write_volume(sa_stream_t *s, const int32_t vol[], unsigned int n))
UNSUPPORTED(int sa_stream_change_rate(sa_stream_t *s, unsigned int rate))
UNSUPPORTED(int sa_stream_change_meta_data(sa_stream_t *s, const char *name, const void *data, size_t size))
UNSUPPORTED(int sa_stream_change_user_data(sa_stream_t *s, const void *value))
UNSUPPORTED(int sa_stream_set_adjust_rate(sa_stream_t *s, sa_adjust_t direction))
UNSUPPORTED(int sa_stream_set_adjust_nchannels(sa_stream_t *s, sa_adjust_t direction))
UNSUPPORTED(int sa_stream_set_adjust_pcm_format(sa_stream_t *s, sa_adjust_t direction))
UNSUPPORTED(int sa_stream_set_adjust_watermarks(sa_stream_t *s, sa_adjust_t direction))
UNSUPPORTED(int sa_stream_get_mode(sa_stream_t *s, sa_mode_t *access_mode))
UNSUPPORTED(int sa_stream_get_codec(sa_stream_t *s, char *codec, size_t *size))
UNSUPPORTED(int sa_stream_get_pcm_format(sa_stream_t *s, sa_pcm_format_t *format))
UNSUPPORTED(int sa_stream_get_rate(sa_stream_t *s, unsigned int *rate))
UNSUPPORTED(int sa_stream_get_nchannels(sa_stream_t *s, int *nchannels))
UNSUPPORTED(int sa_stream_get_user_data(sa_stream_t *s, void **value))
UNSUPPORTED(int sa_stream_get_write_lower_watermark(sa_stream_t *s, size_t *size))
UNSUPPORTED(int sa_stream_get_read_lower_watermark(sa_stream_t *s, size_t *size))
UNSUPPORTED(int sa_stream_get_write_upper_watermark(sa_stream_t *s, size_t *size))
UNSUPPORTED(int sa_stream_get_read_upper_watermark(sa_stream_t *s, size_t *size))
UNSUPPORTED(int sa_stream_get_channel_map(sa_stream_t *s, sa_channel_t map[], unsigned int *n))
UNSUPPORTED(int sa_stream_get_xrun_mode(sa_stream_t *s, sa_xrun_mode_t *mode))
UNSUPPORTED(int sa_stream_get_non_interleaved(sa_stream_t *s, int *enabled))
UNSUPPORTED(int sa_stream_get_dynamic_rate(sa_stream_t *s, int *enabled))
UNSUPPORTED(int sa_stream_get_driver(sa_stream_t *s, char *driver_name, size_t *size))
UNSUPPORTED(int sa_stream_get_device(sa_stream_t *s, char *device_name, size_t *size))
UNSUPPORTED(int sa_stream_get_read_volume(sa_stream_t *s, int32_t vol[], unsigned int *n))
UNSUPPORTED(int sa_stream_get_write_volume(sa_stream_t *s, int32_t vol[], unsigned int *n))
UNSUPPORTED(int sa_stream_get_meta_data(sa_stream_t *s, const char *name, void*data, size_t *size))
UNSUPPORTED(int sa_stream_get_adjust_rate(sa_stream_t *s, sa_adjust_t *direction))
UNSUPPORTED(int sa_stream_get_adjust_nchannels(sa_stream_t *s, sa_adjust_t *direction))
UNSUPPORTED(int sa_stream_get_adjust_pcm_format(sa_stream_t *s, sa_adjust_t *direction))
UNSUPPORTED(int sa_stream_get_adjust_watermarks(sa_stream_t *s, sa_adjust_t *direction))
UNSUPPORTED(int sa_stream_get_state(sa_stream_t *s, sa_state_t *state))
UNSUPPORTED(int sa_stream_get_event_error(sa_stream_t *s, sa_error_t *error))
UNSUPPORTED(int sa_stream_get_event_notify(sa_stream_t *s, sa_notify_t *notify))
UNSUPPORTED(int sa_stream_read(sa_stream_t *s, void *data, size_t nbytes))
UNSUPPORTED(int sa_stream_read_ni(sa_stream_t *s, unsigned int channel, void *data, size_t nbytes))
UNSUPPORTED(int sa_stream_write_ni(sa_stream_t *s, unsigned int channel, const void *data, size_t nbytes))
UNSUPPORTED(int sa_stream_pwrite(sa_stream_t *s, const void *data, size_t nbytes, int64_t offset, sa_seek_t whence))
UNSUPPORTED(int sa_stream_pwrite_ni(sa_stream_t *s, unsigned int channel, const void *data, size_t nbytes, int64_t offset, sa_seek_t whence))
UNSUPPORTED(int sa_stream_get_read_size(sa_stream_t *s, size_t *size))
UNSUPPORTED(int sa_stream_drain(sa_stream_t *s))

const char *sa_strerror(int code) { return NULL; }

