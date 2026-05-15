#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

"""
Decode an LCDC silent-loss snapshot captured by the JDI display driver and
shipped to Memfault as a Custom Data Recording.

The snapshot is produced by display_jdi_diagnostics_capture() in
src/fw/drivers/display/sf32lb/display_jdi_diagnostics.c when the update-timeout
watchdog fires. Each capture is a fixed-size sLcdcSnapshot binary blob: a
header with the diagnostic fields followed by a 2KB dump of the LCDC
peripheral register file.

Usage:
    tools/decode_lcdc_snapshot.py <snapshot.bin>
    tools/decode_lcdc_snapshot.py --json <snapshot.bin>

Match the parser by the snapshot's schema_version field. SCHEMA_VERSION below
must equal LCDC_SNAPSHOT_SCHEMA_VERSION in display_jdi_diagnostics.c.
"""

import argparse
import json
import struct
import sys

SCHEMA_VERSION = 1
MAGIC = 0x4C434430  # 'LCD0'

# sLcdcSnapshot, little-endian, __attribute__((packed)). Keep in lock-step
# with the C struct.
_HEADER_FMT = "<IIQII HH IIIIIII I B 3x"
_HEADER_SIZE = struct.calcsize(_HEADER_FMT)
assert _HEADER_SIZE == 64, _HEADER_SIZE

_REG_DUMP_BYTES = 2048
_SNAPSHOT_SIZE = _HEADER_SIZE + _REG_DUMP_BYTES

# HAL_LCDC_StateTypeDef values, from bf0_hal_lcdc.h.
_HAL_STATE = {
    0: "RESET",
    1: "READY",
    2: "BUSY",
    3: "TIMEOUT",
    4: "ERROR",
    5: "SUSPEND",
    6: "LOWPOWER",
}

# HAL_LCDC_ERROR_* bitmask values, from bf0_hal_lcdc.h.
_HAL_ERROR_BITS = [
    (0x00000001, "TIMEOUT"),
    (0x00000002, "OVERFLOW"),  # ICB decompress buffer overflow
    (0x00000004, "UNDERRUN"),
    (0x80000000, "HAL_LOCKED"),
]

# LcdcCaptureReason values, from display_jdi_diagnostics.h.
_REASONS = {0: "WatchdogFired"}

# LCD_IF_TypeDef register layout, from third_party/hal_sifli/.../cmsis/sf32lb52x/lcd_if.h.
# Names are aligned to the C struct so callers can correlate with HAL source.
_REG_MAP = [
    (0x000, "COMMAND"),
    (0x004, "STATUS"),
    (0x008, "IRQ"),
    (0x00C, "SETTING"),
    (0x010, "CANVAS_TL_POS"),
    (0x014, "CANVAS_BR_POS"),
    (0x018, "CANVAS_BG"),
    (0x01C, "LAYER0_CONFIG"),
    (0x020, "LAYER0_TL_POS"),
    (0x024, "LAYER0_BR_POS"),
    (0x028, "LAYER0_FILTER"),
    (0x02C, "LAYER0_SRC"),
    (0x030, "LAYER0_FILL"),
    (0x034, "LAYER0_DECOMP"),
    (0x038, "LAYER0_DECOMP_CFG0"),
    (0x03C, "LAYER0_DECOMP_CFG1"),
    (0x040, "LAYER0_DECOMP_STAT"),
    (0x060, "LAYER1_CONFIG"),
    (0x064, "LAYER1_TL_POS"),
    (0x068, "LAYER1_BR_POS"),
    (0x078, "DITHER_CONF"),
    (0x080, "LCD_CONF"),
    (0x084, "LCD_IF_CONF"),
    (0x0A0, "TE_CONF"),
    (0x0A4, "TE_CONF2"),
    (0x0D0, "JDI_PAR_CONF1"),
    (0x0D4, "JDI_PAR_CONF2"),
    (0x0D8, "JDI_PAR_CONF3"),
    (0x0DC, "JDI_PAR_CONF4"),
    (0x0E0, "JDI_PAR_CONF5"),
    (0x0E4, "JDI_PAR_CONF6"),
    (0x0E8, "JDI_PAR_CONF7"),
    (0x0EC, "JDI_PAR_CTRL"),
    (0x0F0, "JDI_PAR_STAT"),
    (0x0F4, "JDI_PAR_EX_CTRL"),
    (0x0F8, "JDI_PAR_CONF8"),
    (0x0FC, "JDI_PAR_CONF9"),
    (0x100, "JDI_PAR_CONF10"),
]


def _decode_error(err: int) -> str:
    bits = [name for mask, name in _HAL_ERROR_BITS if err & mask]
    return "|".join(bits) if bits else "NONE"


def _root_cause_guess(irq_raw: int, fsm_running: bool, state: int) -> str:
    """Map the three pre-recovery probes to a likely root-cause branch.

    See the comment in sLcdcSnapshot in display_jdi_diagnostics.c for the
    full table. Heuristic, not authoritative — confirm against the register
    dump.
    """
    if state == 1:  # READY
        return "system idle at capture (synthetic / non-freeze)"
    if irq_raw and not fsm_running:
        return (
            "HAL got IRQ but lost callback path (silent-loss / ICB overflow signature)"
        )
    if not irq_raw and fsm_running:
        return "hardware wedged mid-frame, IRQ never fired"
    if not irq_raw and not fsm_running:
        return "kickoff didn't start the engine, or already-quiesced wedge"
    return "IRQ pending AND FSM running — transfer still active (watchdog raced)"


def decode(blob: bytes) -> dict:
    if len(blob) != _SNAPSHOT_SIZE:
        raise ValueError(
            f"Snapshot size {len(blob)} != expected {_SNAPSHOT_SIZE} bytes"
        )
    (
        magic,
        schema,
        uptime_ms,
        reason,
        fires,
        y0,
        y1,
        h_state,
        h_err,
        dc0,
        dc1,
        dc2,
        cb_cplt,
        cb_err,
        irq,
        fsm,
    ) = struct.unpack_from(_HEADER_FMT, blob)

    if magic != MAGIC:
        raise ValueError(f"Bad magic 0x{magic:08x} (expected 0x{MAGIC:08x})")
    if schema != SCHEMA_VERSION:
        raise ValueError(
            f"Schema version {schema} (this decoder is v{SCHEMA_VERSION}); "
            "update SCHEMA_VERSION + add a parser branch"
        )

    regs = blob[_HEADER_SIZE:]
    reg_words = {off: struct.unpack_from("<I", regs, off)[0] for off, _ in _REG_MAP}

    return {
        "magic": magic,
        "schema_version": schema,
        "uptime_ms": uptime_ms,
        "reason": reason,
        "reason_name": _REASONS.get(reason, f"unknown_{reason}"),
        "watchdog_fire_count": fires,
        "is_synthetic": fires == 0xDEADBEEF,
        "update_y0": y0,
        "update_y1": y1,
        "hal": {
            "state": h_state,
            "state_name": _HAL_STATE.get(h_state, f"unknown_{h_state}"),
            "error_code": h_err,
            "error_bits": _decode_error(h_err),
            "debug_cnt0": dc0,
            "debug_cnt1_kickoffs": dc1,
            "debug_cnt2_completions": dc2,
            "kickoff_completion_delta": dc1 - dc2,
            "xfer_cplt_callback": cb_cplt,
            "xfer_error_callback": cb_err,
        },
        "probes": {
            "irq_raw_status": irq,
            "hw_fsm_running": bool(fsm),
            "root_cause_guess": _root_cause_guess(irq, bool(fsm), h_state),
        },
        "registers": reg_words,
    }


def print_human(d: dict) -> None:
    print(f"magic              0x{d['magic']:08x}  (OK)")
    print(f"schema_version     {d['schema_version']}")
    print(f"uptime_ms          {d['uptime_ms']}  ({d['uptime_ms'] / 1000:.1f}s)")
    print(f"reason             {d['reason']}  ({d['reason_name']})")
    fires_tag = (
        "SYNTHETIC TEST" if d["is_synthetic"] else f"real ({d['watchdog_fire_count']})"
    )
    print(f"watchdog_fires     0x{d['watchdog_fire_count']:08x}  ({fires_tag})")
    print(f"update_y0..y1      {d['update_y0']}..{d['update_y1']}")
    print()
    h = d["hal"]
    print(f"hal_State          {h['state']}  ({h['state_name']})")
    print(f"hal_ErrorCode      0x{h['error_code']:08x}  ({h['error_bits']})")
    print(f"hal_debug_cnt0     {h['debug_cnt0']}")
    print(f"hal_debug_cnt1     {h['debug_cnt1_kickoffs']}  (LCDC_ASYNC_MODE kickoffs)")
    print(
        f"hal_debug_cnt2     {h['debug_cnt2_completions']}  (TransCpltCallback fires)"
    )
    delta = h["kickoff_completion_delta"]
    delta_tag = "balanced" if delta == 0 else f"!! {delta} unfinished kickoff(s)"
    print(f"  kickoffs - completions = {delta}  ({delta_tag})")
    print(
        f"XferCpltCallback   0x{h['xfer_cplt_callback']:08x}"
        f"  ({'set' if h['xfer_cplt_callback'] else 'NULL'})"
    )
    print(
        f"XferErrorCallback  0x{h['xfer_error_callback']:08x}"
        f"  ({'set' if h['xfer_error_callback'] else 'NULL'})"
    )
    print()
    p = d["probes"]
    print(f"irq_raw_status     0x{p['irq_raw_status']:08x}")
    print(f"hw_fsm_running     {int(p['hw_fsm_running'])}  (JDI_PAR_CTRL.ENABLE)")
    print(f"  root cause guess: {p['root_cause_guess']}")
    print()
    print("Named LCDC registers:")
    for off, name in _REG_MAP:
        val = d["registers"][off]
        print(f"  +0x{off:03x}  {name:22s}  0x{val:08x}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    ap.add_argument("snapshot", help="binary CDR downloaded from Memfault")
    ap.add_argument(
        "--json", action="store_true", help="emit JSON instead of human-readable text"
    )
    args = ap.parse_args()

    with open(args.snapshot, "rb") as f:
        blob = f.read()

    try:
        decoded = decode(blob)
    except ValueError as e:
        print(f"decode error: {e}", file=sys.stderr)
        return 1

    if args.json:
        print(json.dumps(decoded, indent=2))
    else:
        print_human(decoded)
    return 0


if __name__ == "__main__":
    sys.exit(main())
