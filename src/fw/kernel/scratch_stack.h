/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

//! Run a short burst of deep, stack-hungry work (e.g. a flash I/O chain) on a
//! shared borrowed stack instead of the calling task's small stack.

#include <stdint.h>

typedef void (*ScratchStackCallback)(void *context);

#if defined(CONFIG_MPU_TYPE_ARMV8M)

void scratch_stack_init(void);

//! Peak unused bytes on the scratch stack, for sizing during validation.
uint16_t scratch_stack_free_bytes(void);

//! Run fn(context) on the shared scratch stack, serialized by a mutex; fn may
//! block. Falls back to a plain direct call when the caller is unprivileged
//! (its deep syscalls already relocate to the per-task syscall stack), in an
//! ISR, already on the scratch stack, or on a task whose stack layout doesn't
//! allow borrowing.
void scratch_stack_call(ScratchStackCallback fn, void *context);

#else

static inline void scratch_stack_init(void) {}

static inline uint16_t scratch_stack_free_bytes(void) { return 0xFFFF; }

static inline void scratch_stack_call(ScratchStackCallback fn, void *context) {
  fn(context);
}

#endif // CONFIG_MPU_TYPE_ARMV8M
