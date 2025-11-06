/*
 * Copyright 2025 Core Devices LLC
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

#include <stdint.h>

#include "system/passert.h"
#include "util/attributes.h"

#define SF32LB52_COMPATIBLE
#include "mcu.h"

//! These symbols are defined in the linker script for use in initializing
//! the data sections. uint8_t since we do arithmetic with section lengths.
//! These are arrays to avoid the need for an & when dealing with linker symbols.
extern uint8_t __data_load_start[];
extern uint8_t __data_start[];
extern uint8_t __data_end[];
extern uint8_t __bss_start[];
extern uint8_t __bss_end[];

extern uint8_t __retm_ro_load_start[];
extern uint8_t __retm_ro_start[];
extern uint8_t __retm_ro_end[];

// Stack limit symbols from linker script
extern uint32_t __isr_stack_start__[];
extern uint32_t __stack_guard_size__[];

extern int main(void);

NAKED_FUNC NORETURN Reset_Handler(void) {
  // Note: Cache is enabled in SystemInit() after MPU configuration
  // Enabling cache before MPU setup causes coherency issues with BLE IPC and flash

  // Configure ARMv8-M stack limit registers for hardware stack overflow detection
  // MSPLIM: Main Stack Pointer Limit (for privileged/ISR stack)
  // Set to the bottom of the ISR stack (start + guard size)
  __set_MSPLIM((uint32_t)__isr_stack_start__ + (uint32_t)__stack_guard_size__);
  
  // PSPLIM: Process Stack Pointer Limit (for unprivileged task stacks)
  // Will be configured per-task by FreeRTOS during context switches
  // Initialize to 0 for now (FreeRTOS will manage this)
  __set_PSPLIM((uint32_t)(0));

  // Copy data section from flash to RAM
  for (int i = 0; i < (__data_end - __data_start); i++) {
    __data_start[i] = __data_load_start[i];
  }

  for (int i = 0; i < (__retm_ro_end - __retm_ro_start); i++) {
    __retm_ro_start[i] = __retm_ro_load_start[i];
  }

  // Clear the bss section, assumes .bss goes directly after .data
  memset(__bss_start, 0, __bss_end - __bss_start);

  SystemInit();

  main();

  PBL_CROAK("main returned, this should never happen");
}
