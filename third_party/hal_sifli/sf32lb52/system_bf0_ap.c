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

#include "bf0_hal.h"
#include "register.h"

#define DCACHE_SIZE 16384
#define ICACHE_SIZE (DCACHE_SIZE << 1)

#if defined(__VTOR_PRESENT) && (__VTOR_PRESENT == 1U)
uint32_t __Vectors;
#endif

uint32_t SystemCoreClock = 48000000UL;

void SystemCoreClockUpdate(void) {}

int mpu_dcache_invalidate(void *data, uint32_t size) {
  int r = 0;

  if (IS_DCACHED_RAM(data)) {
    if (size > DCACHE_SIZE) {
      SCB_InvalidateDCache();
      r = 1;
    } else
      SCB_InvalidateDCache_by_Addr(data, size);
  }

  return r;
}

int mpu_icache_invalidate(void *data, uint32_t size) {
  int r = 0;

  if (IS_DCACHED_RAM(data)) {
    if (size > ICACHE_SIZE) {
      SCB_InvalidateICache();
      r = 1;
    } else
      SCB_InvalidateICache_by_Addr(data, size);
  }

  return r;
}

pm_power_on_mode_t SystemPowerOnModeGet(void) { return PM_COLD_BOOT; }

// Forward declaration - implemented in memory_layout.c
extern void memory_layout_setup_mpu(void);

void SystemInit(void) {
#if defined(__VTOR_PRESENT) && (__VTOR_PRESENT == 1U)
  SCB->VTOR = (uint32_t)&__Vectors;
#endif

  // enable CP0/CP1/CP2 Full Access
  SCB->CPACR |= (3U << (0U * 2U)) | (3U << (1U * 2U)) | (3U << (2U * 2U));

#if defined(__FPU_USED) && (__FPU_USED == 1U)
  SCB->CPACR |= ((3U << 10U * 2U) | // enable CP10 Full Access
                 (3U << 11U * 2U)); // enable CP11 Full Access
#endif

  // Invalidate caches before MPU configuration
  SCB_InvalidateDCache();
  SCB_InvalidateICache();

  // Configure MPU with all regions (including critical LPSYS RAM for BLE IPC)
  // This must happen before enabling caches to ensure proper cache coherency
  memory_layout_setup_mpu();

  // Enable caches AFTER MPU configuration
  SCB_EnableICache();
  SCB_EnableDCache();
}
