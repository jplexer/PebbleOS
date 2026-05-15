/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "display_jdi_diagnostics.h"

#include <string.h>

#include "drivers/rtc.h"
#include "memfault/core/custom_data_recording.h"
#include "memfault/core/platform/system_time.h"
#include "os/mutex.h"
#include "system/logging.h"

// Schema version: bump when sLcdcSnapshot layout changes. The cloud-side
// decoder selects a parser by this field.
#define LCDC_SNAPSHOT_SCHEMA_VERSION 1
#define LCDC_SNAPSHOT_MAGIC          0x4C434430u  // 'LCD0'

// How much of the LCDC peripheral register file to dump. Full file is 0x8128
// (~33KB) but the silent-loss-relevant registers (IRQ, SETTING, COMMAND,
// CANVAS_*, LAYER0_*, JDI_PAR_*) all sit in the first 2KB. Sized to fit in
// one Memfault chunk together with the metadata header.
#define LCDC_REG_DUMP_BYTES 2048

// Field rate is preserved by an always-on heartbeat metric; CDR captures are
// diagnostic-quality samples. Three uncapped captures per boot then back off,
// so a recurring bug burst doesn't flood telemetry.
#define RATELIMIT_FREE_CAPTURES    3
#define RATELIMIT_BACKOFF_MS_TIER1 (60u * 1000u)
#define RATELIMIT_BACKOFF_MS_TIER2 (3600u * 1000u)

// Memfault accepts at most one CDR per device per 24h, so any extra uploads
// inside the window are rejected server-side — wasted BT bandwidth. Gate
// exposure (not capture) so the slot keeps overwriting locally and we ship
// the freshest snapshot when the window opens, instead of the first one.
#define EXPOSURE_MIN_INTERVAL_MS (24u * 3600u * 1000u)

typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint32_t schema_version;
  uint64_t uptime_ms;
  uint32_t reason;
  uint32_t watchdog_fire_count;

  // PebbleOS-side driver state at the moment of the freeze.
  uint16_t update_y0;
  uint16_t update_y1;

  // Snapshot of the HAL handle. Just the diagnostic fields — Init/Layer
  // configs are board-constant and don't help diagnose the silent loss.
  uint32_t hal_State;
  uint32_t hal_ErrorCode;
  uint32_t hal_debug_cnt0;
  uint32_t hal_debug_cnt1;
  uint32_t hal_debug_cnt2;
  uint32_t hal_XferCpltCallback;     // function pointer values let us tell
  uint32_t hal_XferErrorCallback;    // which callbacks were registered

  // Pre-recovery probes — disambiguate the major root-cause branches:
  //   irq_raw_status != 0 + hw_fsm_running == 0  -> HAL got the IRQ but lost
  //                                                  the callback path
  //   irq_raw_status == 0 + hw_fsm_running == 1  -> hardware wedged mid-frame,
  //                                                  IRQ never fired
  //   irq_raw_status == 0 + hw_fsm_running == 0  -> kickoff didn't actually
  //                                                  start the engine
  uint32_t irq_raw_status;
  uint8_t  hw_fsm_running;
  uint8_t  _pad[3];

  uint8_t  lcdc_regs[LCDC_REG_DUMP_BYTES];
} sLcdcSnapshot;

typedef enum {
  SlotState_Empty = 0,
  SlotState_Ready,     // snapshot populated, Memfault hasn't picked it up
  SlotState_Draining,  // Memfault is reading via read_data_cb
} SlotState;

static struct {
  sLcdcSnapshot snapshot;
  SlotState state;
  uint32_t captures_total;
  uint64_t last_capture_uptime_ms;
  // Uptime when has_cdr_cb last handed a snapshot to Memfault. Tracking uptime
  // rather than wall-clock means a reboot resets the window, which is correct:
  // post-reboot captures come from a different session and are worth uploading
  // even if it's been <24h since the previous device's submission.
  uint64_t last_exposed_uptime_ms;
  PebbleMutex *lock;
  bool registered;
} s_diag;

static uint64_t prv_uptime_ms(void) {
  // RTC_TICKS_HZ on SF32LB52 is 1000, but compute properly so this stays
  // correct if the JDI driver is ever built for a board with 1024Hz ticks.
  return ((uint64_t)rtc_get_ticks() * 1000u) / RTC_TICKS_HZ;
}

static bool prv_ratelimit_allow(uint64_t now_ms) {
  uint32_t n = s_diag.captures_total;
  if (n < RATELIMIT_FREE_CAPTURES) {
    return true;
  }
  uint32_t backoff_ms = (n < RATELIMIT_FREE_CAPTURES + 3) ? RATELIMIT_BACKOFF_MS_TIER1
                                                          : RATELIMIT_BACKOFF_MS_TIER2;
  return (now_ms - s_diag.last_capture_uptime_ms) >= backoff_ms;
}

// MMIO copy: peripheral registers are device memory and must be read as
// aligned 32-bit volatile loads. memcpy() over a uintptr_t may emit byte-wise
// loads on some toolchains.
static void prv_copy_mmio(volatile const void *src, void *dst, size_t bytes) {
  const volatile uint32_t *s = (const volatile uint32_t *)src;
  uint32_t *d = (uint32_t *)dst;
  for (size_t i = 0; i < bytes / 4; i++) {
    d[i] = s[i];
  }
}

void display_jdi_diagnostics_capture(const LCDC_HandleTypeDef *hlcdc,
                                     LcdcCaptureReason reason,
                                     uint16_t update_y0, uint16_t update_y1,
                                     uint32_t watchdog_fire_count) {
  if (!hlcdc || !hlcdc->Instance || !s_diag.lock) {
    return;
  }

  uint64_t now = prv_uptime_ms();
  mutex_lock(s_diag.lock);

  if (s_diag.state == SlotState_Draining) {
    // Don't clobber a CDR mid-upload.
    mutex_unlock(s_diag.lock);
    return;
  }
  if (!prv_ratelimit_allow(now)) {
    mutex_unlock(s_diag.lock);
    return;
  }

  sLcdcSnapshot *snap = &s_diag.snapshot;
  memset(snap, 0, sizeof(*snap));

  snap->magic = LCDC_SNAPSHOT_MAGIC;
  snap->schema_version = LCDC_SNAPSHOT_SCHEMA_VERSION;
  snap->uptime_ms = now;
  snap->reason = (uint32_t)reason;
  snap->watchdog_fire_count = watchdog_fire_count;
  snap->update_y0 = update_y0;
  snap->update_y1 = update_y1;

  snap->hal_State = (uint32_t)hlcdc->State;
  snap->hal_ErrorCode = hlcdc->ErrorCode;
  snap->hal_debug_cnt0 = hlcdc->debug_cnt0;
  snap->hal_debug_cnt1 = hlcdc->debug_cnt1;
  snap->hal_debug_cnt2 = hlcdc->debug_cnt2;
  snap->hal_XferCpltCallback = (uint32_t)(uintptr_t)hlcdc->XferCpltCallback;
  snap->hal_XferErrorCallback = (uint32_t)(uintptr_t)hlcdc->XferErrorCallback;

  snap->irq_raw_status = hlcdc->Instance->IRQ;
  snap->hw_fsm_running =
      (hlcdc->Instance->JDI_PAR_CTRL & LCD_IF_JDI_PAR_CTRL_ENABLE) ? 1u : 0u;

  prv_copy_mmio((const void *)hlcdc->Instance, snap->lcdc_regs,
                LCDC_REG_DUMP_BYTES);

  s_diag.state = SlotState_Ready;
  s_diag.captures_total++;
  s_diag.last_capture_uptime_ms = now;

  mutex_unlock(s_diag.lock);
}

// ---- Memfault CDR source callbacks ----

static const char *const s_reason_strings[] = {
  [LcdcCaptureReason_WatchdogFired] = "lcdc_silent_loss_watchdog",
};

// sMemfaultCdrMetadata.mimetypes is `const char **` (not const char *const *),
// so the inner pointers can't be const.
static const char *s_mimetypes[] = { MEMFAULT_CDR_BINARY };

static bool prv_has_cdr(sMemfaultCdrMetadata *out) {
  bool ready = false;
  mutex_lock(s_diag.lock);
  if (s_diag.state == SlotState_Ready) {
    uint64_t now = prv_uptime_ms();
    if (s_diag.last_exposed_uptime_ms != 0 &&
        (now - s_diag.last_exposed_uptime_ms) < EXPOSURE_MIN_INTERVAL_MS) {
      // Server would reject this upload — hold it locally. Future captures
      // overwrite the slot, so when the window opens we ship the freshest
      // snapshot from the last 24h, not the staletest.
      mutex_unlock(s_diag.lock);
      return false;
    }
    s_diag.last_exposed_uptime_ms = now;
    s_diag.state = SlotState_Draining;
    uint32_t reason_idx = s_diag.snapshot.reason;
    const char *reason =
        (reason_idx < (sizeof(s_reason_strings) / sizeof(s_reason_strings[0])))
            ? s_reason_strings[reason_idx]
            : s_reason_strings[0];
    *out = (sMemfaultCdrMetadata){
        .start_time = { .type = kMemfaultCurrentTimeType_Unknown },
        .mimetypes = s_mimetypes,
        .num_mimetypes = sizeof(s_mimetypes) / sizeof(s_mimetypes[0]),
        .data_size_bytes = sizeof(sLcdcSnapshot),
        .duration_ms = 0,
        .collection_reason = reason,
    };
    ready = true;
  }
  mutex_unlock(s_diag.lock);
  return ready;
}

static bool prv_read_data(uint32_t offset, void *buf, size_t len) {
  if (offset + len > sizeof(sLcdcSnapshot)) {
    return false;
  }
  mutex_lock(s_diag.lock);
  bool ok = (s_diag.state == SlotState_Draining);
  if (ok) {
    memcpy(buf, (const uint8_t *)&s_diag.snapshot + offset, len);
  }
  mutex_unlock(s_diag.lock);
  return ok;
}

static void prv_mark_read(void) {
  mutex_lock(s_diag.lock);
  s_diag.state = SlotState_Empty;
  mutex_unlock(s_diag.lock);
}

static const sMemfaultCdrSourceImpl s_cdr_source = {
  .has_cdr_cb = prv_has_cdr,
  .read_data_cb = prv_read_data,
  .mark_cdr_read_cb = prv_mark_read,
};

void display_jdi_diagnostics_init(void) {
  if (s_diag.registered) {
    return;
  }
  s_diag.lock = mutex_create();
  if (!memfault_cdr_register_source(&s_cdr_source)) {
    // Default MEMFAULT_CDR_MAX_DATA_SOURCES is 4. If this ever fires the
    // bump goes in third_party/memfault/port (or a project-level config).
    PBL_LOG_ERR("display_jdi_diagnostics: CDR source registration failed");
    return;
  }
  s_diag.registered = true;
}
