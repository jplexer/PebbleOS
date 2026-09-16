/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "pbl/services/mic_capture/mic_capture_service.h"

#include "kernel/event_loop.h"
#include "kernel/events.h"
#include "kernel/pbl_malloc.h"
#include "kernel/ui/modals/modal_manager.h"
#include "pbl/kernel/mutex.h"
#include "pbl/services/app_permissions/app_permissions.h"
#include "pbl/services/mic_manager.h"
#include "pbl/util/circular_buffer.h"
#include "popups/mic_banner.h"
#include <pbl/logging/logging.h>

#include <string.h>

PBL_LOG_MODULE_DEFINE(service_mic_capture, CONFIG_SERVICE_MIC_CAPTURE_LOG_LEVEL);

#define RING_BYTES (MIC_CAPTURE_RING_SAMPLES * sizeof(int16_t))

typedef struct {
  bool active;
  PebbleTask owner;
  uint16_t samples_per_update;
  CircularBuffer ring;
  uint8_t *ring_storage;
  int16_t *chunk;
  bool overrun;
  bool data_event_pending;
} MicCaptureState;

static PBL_MUTEX_DEFINE(s_lock);
static MicCaptureState s_state;

// The banner is UI, so it is driven from KernelMain whichever task stops or starts capture.
static void prv_show_banner_cb(void *unused) {
  mic_banner_show();
}

static void prv_hide_banner_cb(void *unused) {
  mic_banner_hide();
}

void mic_capture_service_init(void) {
  s_state = (MicCaptureState){};
}

static void prv_post_event(uint8_t type, MicCaptureStopReason reason, bool overrun,
                           uint16_t num_samples) {
  PebbleEvent e = {
    .type = PEBBLE_MIC_CAPTURE_EVENT,
    .mic_capture = {
      .type = type,
      .stop_reason = (uint8_t)reason,
      .overrun = overrun,
      .num_samples = num_samples,
    },
  };
  event_put(&e);
}

//! Expects s_lock held. Frees resources and returns the buffers to free.
static void prv_teardown_locked(void) {
  s_state.active = false;
  s_state.owner = PebbleTask_Unknown;
  s_state.data_event_pending = false;
  kernel_free(s_state.ring_storage);
  kernel_free(s_state.chunk);
  s_state.ring_storage = NULL;
  s_state.chunk = NULL;
  launcher_task_add_callback(prv_hide_banner_cb, NULL);
}

//! Stops capture for a system-originated reason and tells the app. `release_mic` is false when
//! the mic manager already took the mic away (preemption).
static void prv_stop_with_reason(MicCaptureStopReason reason, bool release_mic) {
  pbl_mutex_lock(&s_lock, PBL_FOREVER);
  if (!s_state.active) {
    pbl_mutex_unlock(&s_lock);
    return;
  }
  PBL_LOG_DBG("Capture stopped, reason %u", reason);
  prv_teardown_locked();
  pbl_mutex_unlock(&s_lock);

  if (release_mic) {
    mic_manager_release(MicClientAppCapture);
  }
  prv_post_event(MicCaptureEventStopped, reason, false, 0);
}

// Runs on KernelBG, driven by the mic driver.
static void prv_mic_data_handler(int16_t *samples, size_t sample_count, void *context) {
  pbl_mutex_lock(&s_lock, PBL_FOREVER);
  if (!s_state.active) {
    pbl_mutex_unlock(&s_lock);
    return;
  }
  const uint16_t bytes = sample_count * sizeof(int16_t);
  if (!circular_buffer_write(&s_state.ring, samples, bytes)) {
    // Full: drop the newest chunk, the app is not keeping up
    s_state.overrun = true;
  }
  bool post = false;
  bool overrun = false;
  uint16_t available = 0;
  if (!s_state.data_event_pending) {
    s_state.data_event_pending = true;
    post = true;
    overrun = s_state.overrun;
    s_state.overrun = false;
    available = circular_buffer_get_read_space_remaining(&s_state.ring) / sizeof(int16_t);
  }
  pbl_mutex_unlock(&s_lock);

  if (post) {
    prv_post_event(MicCaptureEventData, MicCaptureStopReasonStopped, overrun, available);
  }
}

static void prv_preempted(void *context) {
  prv_stop_with_reason(MicCaptureStopReasonPreempted, false /* mic already gone */);
}

static bool prv_app_is_in_focus(void) {
  return !modal_manager_get_enabled() || (modal_manager_get_properties() & ModalProperty_Unfocused);
}

MicCaptureStartResult mic_capture_service_start(PebbleTask owner, uint16_t samples_per_update) {
  if (owner != PebbleTask_App) {
    return MicCaptureStartErrNotForeground;
  }
  if ((samples_per_update < MIC_CAPTURE_MIN_SAMPLES_PER_UPDATE) ||
      (samples_per_update > MIC_CAPTURE_MAX_SAMPLES_PER_UPDATE)) {
    return MicCaptureStartErrInvalidArgs;
  }
  if (!prv_app_is_in_focus()) {
    return MicCaptureStartErrNotForeground;
  }
  switch (app_permissions_get_state_for_current_app(AppPermission_Microphone)) {
    case AppPermissionStateNotDeclared:
      return MicCaptureStartErrNotDeclared;
    case AppPermissionStateDenied:
      return MicCaptureStartErrDenied;
    case AppPermissionStateGranted:
      break;
  }

  pbl_mutex_lock(&s_lock, PBL_FOREVER);
  if (s_state.active) {
    pbl_mutex_unlock(&s_lock);
    return MicCaptureStartErrBusy;
  }

  uint8_t *ring_storage = kernel_malloc(RING_BYTES);
  int16_t *chunk = kernel_malloc(samples_per_update * sizeof(int16_t));
  if (!ring_storage || !chunk) {
    kernel_free(ring_storage);
    kernel_free(chunk);
    pbl_mutex_unlock(&s_lock);
    return MicCaptureStartErrNoMemory;
  }

  s_state = (MicCaptureState){
    .active = true,
    .owner = owner,
    .samples_per_update = samples_per_update,
    .ring_storage = ring_storage,
    .chunk = chunk,
  };
  circular_buffer_init(&s_state.ring, ring_storage, RING_BYTES);
  pbl_mutex_unlock(&s_lock);

  if (!mic_manager_acquire(MicClientAppCapture, prv_mic_data_handler, NULL, chunk,
                           samples_per_update, prv_preempted, NULL)) {
    pbl_mutex_lock(&s_lock, PBL_FOREVER);
    prv_teardown_locked();
    pbl_mutex_unlock(&s_lock);
    return MicCaptureStartErrBusy;
  }

  launcher_task_add_callback(prv_show_banner_cb, NULL);
  PBL_LOG_DBG("Capture started, %u samples per update", samples_per_update);
  return MicCaptureStartOk;
}

static void prv_stop_silently(PebbleTask task) {
  pbl_mutex_lock(&s_lock, PBL_FOREVER);
  if (!s_state.active || (s_state.owner != task)) {
    pbl_mutex_unlock(&s_lock);
    return;
  }
  prv_teardown_locked();
  pbl_mutex_unlock(&s_lock);
  mic_manager_release(MicClientAppCapture);
  PBL_LOG_DBG("Capture stopped");
}

void mic_capture_service_stop(PebbleTask owner) {
  prv_stop_silently(owner);
}

void mic_capture_service_stop_for_task(PebbleTask task) {
  prv_stop_silently(task);
}

uint32_t mic_capture_service_read(PebbleTask owner, int16_t *out, uint32_t max_samples) {
  pbl_mutex_lock(&s_lock, PBL_FOREVER);
  if (!s_state.active || (s_state.owner != owner) || !out) {
    pbl_mutex_unlock(&s_lock);
    return 0;
  }
  s_state.data_event_pending = false;
  const uint32_t available =
      circular_buffer_get_read_space_remaining(&s_state.ring) / sizeof(int16_t);
  const uint32_t num_samples = (max_samples < available) ? max_samples : available;
  if (num_samples > 0) {
    const uint16_t bytes = num_samples * sizeof(int16_t);
    circular_buffer_copy(&s_state.ring, out, bytes);
    circular_buffer_consume(&s_state.ring, bytes);
  }
  pbl_mutex_unlock(&s_lock);
  return num_samples;
}

uint32_t mic_capture_service_get_available(void) {
  pbl_mutex_lock(&s_lock, PBL_FOREVER);
  const uint32_t available =
      s_state.active ? circular_buffer_get_read_space_remaining(&s_state.ring) / sizeof(int16_t)
                     : 0;
  pbl_mutex_unlock(&s_lock);
  return available;
}

bool mic_capture_service_is_active(void) {
  return s_state.active;
}

void mic_capture_service_handle_app_focus_lost(void) {
  prv_stop_with_reason(MicCaptureStopReasonFocusLost, true);
}

void mic_capture_service_handle_permission_changed(void) {
  if (!s_state.active) {
    return;
  }
  if (!app_permissions_is_granted_for_current_app(AppPermission_Microphone)) {
    prv_stop_with_reason(MicCaptureStopReasonPermissionRevoked, true);
  }
}
