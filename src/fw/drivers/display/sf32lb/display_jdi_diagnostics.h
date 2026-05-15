/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bf0_hal_lcdc.h"

// Why we captured a snapshot. Memfault caps unique CDR reasons at 100, so keep
// this enum small and stable; new reasons must append, never reorder.
typedef enum {
  LcdcCaptureReason_WatchdogFired = 0,
} LcdcCaptureReason;

#if MEMFAULT
// Register the CDR source with Memfault. Idempotent.
void display_jdi_diagnostics_init(void);

// Snapshot LCDC + driver state into the CDR slot. Cheap (bounded MMIO copy +
// a handful of struct reads), rate-limited internally. Caller passes the live
// HAL handle so we read State / ErrorCode / debug counters AT the moment of
// the freeze, before any recovery action mutates them.
void display_jdi_diagnostics_capture(const LCDC_HandleTypeDef *hlcdc,
                                     LcdcCaptureReason reason,
                                     uint16_t update_y0, uint16_t update_y1,
                                     uint32_t watchdog_fire_count);
#else
// On builds without Memfault (e.g. non-release Getafix dev builds) the
// diagnostics module isn't compiled — stub to no-ops so the watchdog handler
// doesn't need MEMFAULT guards at each call site.
static inline void display_jdi_diagnostics_init(void) {}
static inline void display_jdi_diagnostics_capture(const LCDC_HandleTypeDef *hlcdc,
                                                   LcdcCaptureReason reason,
                                                   uint16_t update_y0, uint16_t update_y1,
                                                   uint32_t watchdog_fire_count) {
  (void)hlcdc;
  (void)reason;
  (void)update_y0;
  (void)update_y1;
  (void)watchdog_fire_count;
}
#endif
