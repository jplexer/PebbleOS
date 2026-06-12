/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "kernel/scratch_stack.h"

#if defined(CONFIG_MPU_TYPE_ARMV8M)

#include "kernel/pebble_tasks.h"
#include "mcu/interrupts.h"
#include "mcu/privilege.h"
#include "os/mutex.h"
#include "util/attributes.h"

#include "FreeRTOS.h"

#include <cmsis_core.h>
#include <stdint.h>
#include <string.h>

// 2 KiB, sized like the dedicated syscall stacks which host the same deep
// flash I/O chain.
#define SCRATCH_STACK_WORDS 512u

// Linker-placed above the kernel task stacks (see fw_common.ld): the FreeRTOS
// switch-out check reboots when a kernel task's saved SP is below its own
// stack base, so a borrowed stack must sit above every stack that may borrow
// it.
static StackType_t s_scratch_stack[SCRATCH_STACK_WORDS] SECTION(".scratch_stack") ALIGN(8);

static PebbleMutex *s_scratch_stack_mutex;

static bool prv_on_scratch_stack(void) {
  const uintptr_t sp = __get_PSP();
  return (sp > (uintptr_t)&s_scratch_stack[0]) &&
         (sp <= (uintptr_t)&s_scratch_stack[SCRATCH_STACK_WORDS]);
}

static bool prv_task_can_borrow(void) {
  switch (pebble_task_get_current()) {
    // App/Worker: vApplicationStackOverflowHook already ignores their
    // method-1 trip (their real overflows are caught by PSPLIM / MPU guard).
    // KernelMain/KernelBG: their stacks are linker-placed below ours, so the
    // saved-SP check holds while borrowed.
    case PebbleTask_App:
    case PebbleTask_Worker:
    case PebbleTask_KernelMain:
    case PebbleTask_KernelBackground:
      return true;
    default:
      // Other tasks may have heap-allocated stacks above ours, which would
      // trip the FreeRTOS switch-out check while borrowed.
      return false;
  }
}

//! Switch PSP/PSPLIM onto the scratch stack, run fn(context), switch back.
//! Safe across blocking/context switches: the CM33 port saves and restores
//! both PSP and PSPLIM as part of each task's context.
static NAKED_FUNC void prv_call_on_stack(ScratchStackCallback fn, void *context,
                                         StackType_t *stack_top, StackType_t *stack_base) {
  __asm volatile (
    "  push {r4-r6, lr}  \n" // even count keeps 8-byte alignment
    "  mov r4, sp        \n" // r4 = caller SP
    "  mrs r5, psplim    \n" // r5 = caller PSPLIM
    "  mov r6, #0        \n"
    "  msr psplim, r6    \n" // no limit while SP moves between stacks
    "  msr psp, r2       \n" // onto the scratch stack
    "  msr psplim, r3    \n"
    "  isb               \n"
    "  mov r6, r0        \n"
    "  mov r0, r1        \n"
    "  blx r6            \n" // fn(context)
    "  mov r6, #0        \n"
    "  msr psplim, r6    \n"
    "  msr psp, r4       \n" // back onto the caller stack
    "  msr psplim, r5    \n"
    "  isb               \n"
    "  pop {r4-r6, pc}   \n"
  );
}

void scratch_stack_init(void) {
  // .scratch_stack is NOLOAD: zero it ourselves so high-water scans work.
  memset(s_scratch_stack, 0, sizeof(s_scratch_stack));
  s_scratch_stack_mutex = mutex_create();
}

//! Peak unused bytes, by scanning the zeroed base up to the first written
//! word. For sizing during validation.
uint16_t scratch_stack_free_bytes(void) {
  uint32_t i = 0;
  while (i < SCRATCH_STACK_WORDS && s_scratch_stack[i] == 0) {
    i++;
  }
  return (uint16_t)(i * sizeof(StackType_t));
}

void scratch_stack_call(ScratchStackCallback fn, void *context) {
  // Privilege check must come first: everything after it touches
  // privileged-only state. Unprivileged callers run fn directly; their deep
  // syscalls already relocate to the per-task syscall stack at the SVC
  // boundary.
  if (!mcu_state_is_thread_privileged() || mcu_state_is_isr() ||
      (s_scratch_stack_mutex == NULL) || prv_on_scratch_stack() || !prv_task_can_borrow()) {
    fn(context);
    return;
  }

  mutex_lock(s_scratch_stack_mutex);
  prv_call_on_stack(fn, context, &s_scratch_stack[SCRATCH_STACK_WORDS], &s_scratch_stack[0]);
  mutex_unlock(s_scratch_stack_mutex);
}

#endif // CONFIG_MPU_TYPE_ARMV8M
