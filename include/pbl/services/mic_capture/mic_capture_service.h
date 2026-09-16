/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "kernel/pebble_tasks.h"

#include <stdbool.h>
#include <stdint.h>

//! Live PCM capture for the foreground app.
//!
//! The service owns the mic (through mic_manager), a kernel ring buffer the app drains through
//! syscalls, and the OS "listening" banner. It only ever serves the app task, only while the app
//! is in focus and holds the microphone permission. Capture stops on any focus loss, when the
//! grant is revoked, when dictation preempts the mic, or when the app goes away.
//!
//! Data flow: the mic driver hands chunks of samples_per_update samples on KernelBG; they are
//! appended to the ring buffer (dropping the newest chunk when full) and a coalesced
//! PEBBLE_MIC_CAPTURE_EVENT tells the app to read.

typedef enum MicCaptureStartResult {
  MicCaptureStartOk = 0,
  MicCaptureStartErrNotDeclared,
  MicCaptureStartErrDenied,
  MicCaptureStartErrBusy,
  MicCaptureStartErrNotForeground,
  MicCaptureStartErrInvalidArgs,
  MicCaptureStartErrNoMemory,
} MicCaptureStartResult;

typedef enum MicCaptureStopReason {
  MicCaptureStopReasonStopped = 0,
  MicCaptureStopReasonFocusLost,
  MicCaptureStopReasonPreempted,
  MicCaptureStopReasonPermissionRevoked,
  MicCaptureStopReasonAppExit,
  MicCaptureStopReasonError,
} MicCaptureStopReason;

#define MIC_CAPTURE_SAMPLE_RATE            (16000)
#define MIC_CAPTURE_MIN_SAMPLES_PER_UPDATE (80)   // 5 ms
#define MIC_CAPTURE_MAX_SAMPLES_PER_UPDATE (1600) // 100 ms
#define MIC_CAPTURE_RING_SAMPLES           (5120) // 320 ms

void mic_capture_service_init(void);

//! Starts capture for the given task (must be the app task, in focus, with the permission).
MicCaptureStartResult mic_capture_service_start(PebbleTask owner, uint16_t samples_per_update);

//! Stops capture at the owner's request. No stop event is sent.
void mic_capture_service_stop(PebbleTask owner);

//! Stops capture because the task is going away. No stop event is sent. Safe to call when idle.
void mic_capture_service_stop_for_task(PebbleTask task);

//! Copies up to max_samples out of the ring buffer and consumes them.
//! @return number of samples copied
uint32_t mic_capture_service_read(PebbleTask owner, int16_t *out, uint32_t max_samples);

uint32_t mic_capture_service_get_available(void);

bool mic_capture_service_is_active(void);

//! The app lost focus to a modal window: capture stops with MicCaptureStopReasonFocusLost.
void mic_capture_service_handle_app_focus_lost(void);

//! The running app's grants changed: capture stops if the mic is no longer granted.
void mic_capture_service_handle_permission_changed(void);
