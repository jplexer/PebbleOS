/*
 * Copyright 2025 Joshua Jun
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "voice_speex.h"

#include "system/logging.h"
#include "system/passert.h"
#include "kernel/pbl_malloc.h"
#include "services/normal/audio_endpoint.h"

#include <speex/speex.h>
#include <speex/speex_bits.h>
#include <speex/speex_header.h>

#include <string.h>

#define VOICE_SPEEX_LOG(fmt, args...) PBL_LOG_D(LOG_DOMAIN_VOICE, LOG_LEVEL_DEBUG, fmt, ## args)

// Speex bitstream version
#define SPEEX_BITSTREAM_VERSION 4

// Speex encoder state
typedef struct {
  void *enc_state;
  SpeexBits bits;
  int frame_size;
  int sample_rate;
  int bit_rate;
  int bitstream_version;
  bool initialized;
  uint8_t *frame_buffer;
  size_t frame_buffer_size;
  uint8_t *encoded_buffer;
  size_t encoded_buffer_size;
} VoiceSpeexEncoder;

static VoiceSpeexEncoder s_encoder = {0};

// Speex configuration
#define SPEEX_SAMPLE_RATE 8000  // 8 kHz narrowband
#define SPEEX_BIT_RATE 8000     // 8 kbps
#define SPEEX_QUALITY 4         // Quality level (0-10)
#define SPEEX_COMPLEXITY 1      // Complexity (1-10, lower for embedded)
#define SPEEX_ENCODED_BUFFER_SIZE 200  // Max encoded frame size

bool voice_speex_init(void) {
  if (s_encoder.initialized) {
    return true;
  }

  memset(&s_encoder, 0, sizeof(s_encoder));

  // Initialize Speex encoder
  const SpeexMode *mode = speex_lib_get_mode(SPEEX_MODEID_NB);
  if (!mode) {
    VOICE_SPEEX_LOG("Failed to get Speex narrowband mode");
    return false;
  }

  s_encoder.enc_state = speex_encoder_init(mode);
  if (!s_encoder.enc_state) {
    VOICE_SPEEX_LOG("Failed to initialize Speex encoder");
    return false;
  }

  // Initialize bits structure
  speex_bits_init(&s_encoder.bits);

  // Get frame size
  speex_encoder_ctl(s_encoder.enc_state, SPEEX_GET_FRAME_SIZE, &s_encoder.frame_size);
  VOICE_SPEEX_LOG("Speex frame size: %d samples", s_encoder.frame_size);

  // Set encoder parameters
  int tmp = SPEEX_QUALITY;
  speex_encoder_ctl(s_encoder.enc_state, SPEEX_SET_QUALITY, &tmp);
  
  tmp = SPEEX_COMPLEXITY;
  speex_encoder_ctl(s_encoder.enc_state, SPEEX_SET_COMPLEXITY, &tmp);

  tmp = SPEEX_SAMPLE_RATE;
  speex_encoder_ctl(s_encoder.enc_state, SPEEX_SET_SAMPLING_RATE, &tmp);

  tmp = SPEEX_BIT_RATE;
  speex_encoder_ctl(s_encoder.enc_state, SPEEX_SET_BITRATE, &tmp);

  // Get actual parameters
  speex_encoder_ctl(s_encoder.enc_state, SPEEX_GET_SAMPLING_RATE, &s_encoder.sample_rate);
  speex_encoder_ctl(s_encoder.enc_state, SPEEX_GET_BITRATE, &s_encoder.bit_rate);
  
  s_encoder.bitstream_version = SPEEX_BITSTREAM_VERSION;

  // Allocate frame buffer (16-bit samples)
  s_encoder.frame_buffer_size = s_encoder.frame_size * sizeof(int16_t);
  s_encoder.frame_buffer = (uint8_t *)kernel_malloc_check(s_encoder.frame_buffer_size);

  // Allocate encoded buffer
  s_encoder.encoded_buffer_size = SPEEX_ENCODED_BUFFER_SIZE;
  s_encoder.encoded_buffer = kernel_malloc_check(s_encoder.encoded_buffer_size);

  s_encoder.initialized = true;

  VOICE_SPEEX_LOG("Speex encoder initialized: sample_rate=%d, bit_rate=%d, frame_size=%d",
                  s_encoder.sample_rate, s_encoder.bit_rate, s_encoder.frame_size);

  return true;
}

void voice_speex_deinit(void) {
  if (!s_encoder.initialized) {
    return;
  }

  if (s_encoder.enc_state) {
    speex_encoder_destroy(s_encoder.enc_state);
  }

  speex_bits_destroy(&s_encoder.bits);

  kernel_free(s_encoder.frame_buffer);
  kernel_free(s_encoder.encoded_buffer);

  memset(&s_encoder, 0, sizeof(s_encoder));
}

void voice_speex_get_transfer_info(AudioTransferInfoSpeex *info) {
  PBL_ASSERTN(s_encoder.initialized);
  PBL_ASSERTN(info);

  memset(info, 0, sizeof(AudioTransferInfoSpeex));
  strncpy(info->version, "1.2.1", sizeof(info->version) - 1);
  info->sample_rate = s_encoder.sample_rate;
  info->bit_rate = s_encoder.bit_rate;
  info->frame_size = s_encoder.frame_size;
  info->bitstream_version = s_encoder.bitstream_version;
}

int voice_speex_get_frame_size(void) {
  return s_encoder.initialized ? s_encoder.frame_size : 0;
}

int16_t *voice_speex_get_frame_buffer(void) {
  return s_encoder.initialized ? (int16_t *)s_encoder.frame_buffer : NULL;
}

size_t voice_speex_get_frame_buffer_size(void) {
  return s_encoder.initialized ? s_encoder.frame_buffer_size : 0;
}

int voice_speex_encode_frame(const int16_t *samples, uint8_t *encoded_data, size_t max_encoded_size) {
  if (!s_encoder.initialized) {
    return -1;
  }

  // Reset bits structure
  speex_bits_reset(&s_encoder.bits);

  // Encode frame
  speex_encode_int(s_encoder.enc_state, (spx_int16_t *)samples, &s_encoder.bits);

  // Write encoded data to buffer
  int encoded_bytes = speex_bits_write(&s_encoder.bits, (char *)encoded_data, max_encoded_size);
  
  if (encoded_bytes < 0) {
    VOICE_SPEEX_LOG("Failed to write Speex encoded data");
    return -1;
  }

  return encoded_bytes;
}

bool voice_speex_is_initialized(void) {
  return s_encoder.initialized;
}
