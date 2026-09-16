/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "clar.h"

#include "kernel/events.h"
#include "kernel/ui/modals/modal_manager.h"
#include "pbl/services/app_permissions/app_permissions.h"
#include "pbl/services/mic_capture/mic_capture_service.h"
#include "pbl/services/mic_manager.h"

#include <string.h>

// Fakes
////////////////////////////////////////////////////////////////
#include "fake_events.h"
#include "fake_mutex.h"
#include "fake_pbl_malloc.h"

// Stubs
////////////////////////////////////////////////////////////////
#include "stubs_logging.h"
#include "stubs_passert.h"

// Fake mic driver, driven by the tests
////////////////////////////////////////////////////////////////

MicDevice *const MIC = NULL;

static bool s_mic_running;
static MicDataHandlerCB s_mic_handler;
static void *s_mic_context;
static int16_t *s_mic_buffer;
static size_t s_mic_buffer_len;

bool mic_start(MicDevice *this, MicDataHandlerCB data_handler, void *context, int16_t *audio_buffer,
               size_t audio_buffer_len) {
  if (s_mic_running) {
    return false;
  }
  s_mic_running = true;
  s_mic_handler = data_handler;
  s_mic_context = context;
  s_mic_buffer = audio_buffer;
  s_mic_buffer_len = audio_buffer_len;
  return true;
}

void mic_stop(MicDevice *this) {
  s_mic_running = false;
  s_mic_handler = NULL;
}

//! Simulates the driver delivering one chunk of `value`-filled samples on KernelBG.
static void prv_deliver_chunk(int16_t value) {
  cl_assert(s_mic_running);
  for (size_t i = 0; i < s_mic_buffer_len; i++) {
    s_mic_buffer[i] = value;
  }
  s_mic_handler(s_mic_buffer, s_mic_buffer_len, s_mic_context);
}

// Permissions / focus
////////////////////////////////////////////////////////////////

static AppPermissionState s_permission_state;

AppPermissionState app_permissions_get_state_for_current_app(AppPermission permission) {
  cl_assert_equal_i(permission, AppPermission_Microphone);
  return s_permission_state;
}

bool app_permissions_is_granted_for_current_app(AppPermission permission) {
  return (app_permissions_get_state_for_current_app(permission) == AppPermissionStateGranted);
}

static bool s_modal_focused;

bool modal_manager_get_enabled(void) {
  return true;
}

ModalProperty modal_manager_get_properties(void) {
  return s_modal_focused ? ModalProperty_Exists : ModalPropertyDefault;
}

// Dictation, used to test preemption
////////////////////////////////////////////////////////////////

static int16_t s_dictation_buffer[320];

static void prv_dictation_handler(int16_t *samples, size_t sample_count, void *context) {
}

static bool prv_start_dictation(void) {
  return mic_manager_acquire(MicClientVoiceDictation, prv_dictation_handler, NULL,
                             s_dictation_buffer, 320, NULL, NULL);
}

// Helpers
////////////////////////////////////////////////////////////////

#define SPU (320)

static PebbleEvent prv_last_event(void) {
  return fake_event_get_last();
}

static void prv_assert_stopped_event(MicCaptureStopReason reason) {
  PebbleEvent e = prv_last_event();
  cl_assert_equal_i(e.type, PEBBLE_MIC_CAPTURE_EVENT);
  cl_assert_equal_i(e.mic_capture.type, MicCaptureEventStopped);
  cl_assert_equal_i(e.mic_capture.stop_reason, reason);
}

// Setup
////////////////////////////////////////////////////////////////

void test_mic_capture_service__initialize(void) {
  fake_event_init();
  fake_mutex_reset(false);
  fake_pbl_malloc_clear_tracking();
  s_mic_running = false;
  s_mic_handler = NULL;
  s_permission_state = AppPermissionStateGranted;
  s_modal_focused = false;
  mic_manager_init();
  mic_capture_service_init();
}

void test_mic_capture_service__cleanup(void) {
  mic_capture_service_stop_for_task(PebbleTask_App);
  mic_manager_release(MicClientVoiceDictation);
  fake_mutex_assert_all_unlocked();
  fake_pbl_malloc_check_net_allocs();
}

// Tests
////////////////////////////////////////////////////////////////

void test_mic_capture_service__start_refusals(void) {
  cl_assert_equal_i(MicCaptureStartErrNotForeground,
                    mic_capture_service_start(PebbleTask_Worker, SPU));
  cl_assert_equal_i(
      MicCaptureStartErrInvalidArgs,
      mic_capture_service_start(PebbleTask_App, MIC_CAPTURE_MIN_SAMPLES_PER_UPDATE - 1));
  cl_assert_equal_i(
      MicCaptureStartErrInvalidArgs,
      mic_capture_service_start(PebbleTask_App, MIC_CAPTURE_MAX_SAMPLES_PER_UPDATE + 1));

  s_modal_focused = true;
  cl_assert_equal_i(MicCaptureStartErrNotForeground,
                    mic_capture_service_start(PebbleTask_App, SPU));
  s_modal_focused = false;

  s_permission_state = AppPermissionStateNotDeclared;
  cl_assert_equal_i(MicCaptureStartErrNotDeclared, mic_capture_service_start(PebbleTask_App, SPU));
  s_permission_state = AppPermissionStateDenied;
  cl_assert_equal_i(MicCaptureStartErrDenied, mic_capture_service_start(PebbleTask_App, SPU));
  s_permission_state = AppPermissionStateGranted;

  // Dictation owns the mic
  cl_assert(prv_start_dictation());
  cl_assert_equal_i(MicCaptureStartErrBusy, mic_capture_service_start(PebbleTask_App, SPU));
  cl_assert(!mic_capture_service_is_active());
  mic_manager_release(MicClientVoiceDictation);

  cl_assert(!s_mic_running);
  cl_assert_equal_i(0, fake_event_get_count());
}

void test_mic_capture_service__start_read_stop(void) {
  cl_assert_equal_i(MicCaptureStartOk, mic_capture_service_start(PebbleTask_App, SPU));
  cl_assert(mic_capture_service_is_active());
  cl_assert(s_mic_running);
  cl_assert_equal_i(SPU, s_mic_buffer_len);
  cl_assert_equal_i(MicClientAppCapture, mic_manager_get_owner());

  // Second start is refused
  cl_assert_equal_i(MicCaptureStartErrBusy, mic_capture_service_start(PebbleTask_App, SPU));

  cl_assert_equal_i(0, mic_capture_service_get_available());

  prv_deliver_chunk(7);
  cl_assert_equal_i(SPU, mic_capture_service_get_available());
  cl_assert_equal_i(1, fake_event_get_count());
  PebbleEvent e = prv_last_event();
  cl_assert_equal_i(e.type, PEBBLE_MIC_CAPTURE_EVENT);
  cl_assert_equal_i(e.mic_capture.type, MicCaptureEventData);
  cl_assert_equal_i(e.mic_capture.num_samples, SPU);
  cl_assert_equal_b(e.mic_capture.overrun, false);

  // More chunks before the app reads are coalesced into the pending event
  prv_deliver_chunk(8);
  prv_deliver_chunk(9);
  cl_assert_equal_i(1, fake_event_get_count());
  cl_assert_equal_i(3 * SPU, mic_capture_service_get_available());

  // Wrong task cannot read
  int16_t out[SPU];
  cl_assert_equal_i(0, mic_capture_service_read(PebbleTask_Worker, out, SPU));

  cl_assert_equal_i(SPU, mic_capture_service_read(PebbleTask_App, out, SPU));
  cl_assert_equal_i(7, out[0]);
  cl_assert_equal_i(7, out[SPU - 1]);
  cl_assert_equal_i(SPU, mic_capture_service_read(PebbleTask_App, out, SPU));
  cl_assert_equal_i(8, out[0]);
  // Short read at the end
  cl_assert_equal_i(SPU, mic_capture_service_read(PebbleTask_App, out, 2 * SPU));
  cl_assert_equal_i(9, out[0]);
  cl_assert_equal_i(0, mic_capture_service_read(PebbleTask_App, out, SPU));

  // After a read, the next chunk posts a new event
  prv_deliver_chunk(1);
  cl_assert_equal_i(2, fake_event_get_count());

  // App-initiated stop: no event
  mic_capture_service_stop(PebbleTask_Worker); // not the owner
  cl_assert(mic_capture_service_is_active());
  mic_capture_service_stop(PebbleTask_App);
  cl_assert(!mic_capture_service_is_active());
  cl_assert(!s_mic_running);
  cl_assert_equal_i(MicClientNone, mic_manager_get_owner());
  cl_assert_equal_i(2, fake_event_get_count());
  cl_assert_equal_i(0, mic_capture_service_read(PebbleTask_App, out, SPU));
}

void test_mic_capture_service__overrun_drops_newest(void) {
  cl_assert_equal_i(MicCaptureStartOk, mic_capture_service_start(PebbleTask_App, SPU));
  const int chunks_that_fit = MIC_CAPTURE_RING_SAMPLES / SPU;
  for (int i = 0; i < chunks_that_fit; i++) {
    prv_deliver_chunk(i);
  }
  cl_assert_equal_i(MIC_CAPTURE_RING_SAMPLES, mic_capture_service_get_available());
  prv_deliver_chunk(99); // dropped
  cl_assert_equal_i(MIC_CAPTURE_RING_SAMPLES, mic_capture_service_get_available());

  // Drain, the dropped chunk is not there
  int16_t out[SPU];
  for (int i = 0; i < chunks_that_fit; i++) {
    cl_assert_equal_i(SPU, mic_capture_service_read(PebbleTask_App, out, SPU));
    cl_assert_equal_i(i, out[0]);
  }
  cl_assert_equal_i(0, mic_capture_service_get_available());

  // The overrun is reported on the next data event
  prv_deliver_chunk(1);
  PebbleEvent e = prv_last_event();
  cl_assert_equal_i(e.mic_capture.type, MicCaptureEventData);
  cl_assert_equal_b(e.mic_capture.overrun, true);
  cl_assert_equal_i(SPU, mic_capture_service_read(PebbleTask_App, out, SPU));
  prv_deliver_chunk(2);
  e = prv_last_event();
  cl_assert_equal_b(e.mic_capture.overrun, false);
}

void test_mic_capture_service__focus_lost(void) {
  // Idle: nothing happens
  mic_capture_service_handle_app_focus_lost();
  cl_assert_equal_i(0, fake_event_get_count());

  cl_assert_equal_i(MicCaptureStartOk, mic_capture_service_start(PebbleTask_App, SPU));
  mic_capture_service_handle_app_focus_lost();
  cl_assert(!mic_capture_service_is_active());
  cl_assert(!s_mic_running);
  cl_assert_equal_i(MicClientNone, mic_manager_get_owner());
  prv_assert_stopped_event(MicCaptureStopReasonFocusLost);
}

void test_mic_capture_service__permission_revoked(void) {
  cl_assert_equal_i(MicCaptureStartOk, mic_capture_service_start(PebbleTask_App, SPU));

  // Still granted: nothing happens
  mic_capture_service_handle_permission_changed();
  cl_assert(mic_capture_service_is_active());
  cl_assert_equal_i(0, fake_event_get_count());

  s_permission_state = AppPermissionStateDenied;
  mic_capture_service_handle_permission_changed();
  cl_assert(!mic_capture_service_is_active());
  cl_assert(!s_mic_running);
  prv_assert_stopped_event(MicCaptureStopReasonPermissionRevoked);
}

void test_mic_capture_service__preempted_by_dictation(void) {
  cl_assert_equal_i(MicCaptureStartOk, mic_capture_service_start(PebbleTask_App, SPU));
  prv_deliver_chunk(3);

  cl_assert(prv_start_dictation());
  cl_assert(!mic_capture_service_is_active());
  cl_assert_equal_i(MicClientVoiceDictation, mic_manager_get_owner());
  cl_assert(s_mic_running);
  cl_assert(s_mic_buffer == s_dictation_buffer);
  prv_assert_stopped_event(MicCaptureStopReasonPreempted);

  // Late reads after preemption yield nothing
  int16_t out[SPU];
  cl_assert_equal_i(0, mic_capture_service_read(PebbleTask_App, out, SPU));
  cl_assert_equal_i(0, mic_capture_service_get_available());
}

void test_mic_capture_service__stop_for_task(void) {
  cl_assert_equal_i(MicCaptureStartOk, mic_capture_service_start(PebbleTask_App, SPU));
  prv_deliver_chunk(3);
  const uint32_t events = fake_event_get_count();

  mic_capture_service_stop_for_task(PebbleTask_Worker);
  cl_assert(mic_capture_service_is_active());

  mic_capture_service_stop_for_task(PebbleTask_App);
  cl_assert(!mic_capture_service_is_active());
  cl_assert(!s_mic_running);
  cl_assert_equal_i(events, fake_event_get_count());

  // Can start again afterwards
  cl_assert_equal_i(MicCaptureStartOk, mic_capture_service_start(PebbleTask_App, SPU));
}
