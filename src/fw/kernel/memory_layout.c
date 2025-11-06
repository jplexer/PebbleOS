/*
 * Copyright 2024 Google LLC
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

#include "memory_layout.h"

#include "kernel/logging_private.h"
#include "system/passert.h"
#include "util/math.h"
#include "util/size.h"
#include "util/string.h"

#include "kernel/mpu_regions.auto.h"

#include <inttypes.h>
#include <string.h>

#ifdef MPU_ARMV8
#define CMSIS_COMPATIBLE
#include <mcu.h>
#endif


static const char* const MEMORY_REGION_NAMES[] = {
#ifdef MICRO_FAMILY_SF32LB52
  "FLASH2",            // Region 0: Static system region
  "PERIPHERALS",       // Region 1: Static system region
  "HPSYS_RAM",         // Region 2: Static system region
  "LPSYS_RAM",         // Region 3: Static system region
#endif
  "UNPRIV_FLASH",
  "UNPRIV_RO_BSS",
  "UNPRIV_RO_DATA",
  "ISR_STACK_GUARD",
  "Task Specific 1",
  "Task Specific 2",
  "Task Specific 3",
  "Task Specific 4"
};


void memory_layout_dump_mpu_regions_to_dbgserial(void) {
  char buffer[90];

  for (size_t i = 0; i < ARRAY_LENGTH(MEMORY_REGION_NAMES); ++i) {
    MpuRegion region = mpu_get_region(i);

    if (!region.enabled) {
      PBL_LOG_FROM_FAULT_HANDLER_FMT(buffer, sizeof(buffer), "%u Not enabled", i);
      continue;
    }

    PBL_LOG_FROM_FAULT_HANDLER_FMT(
        buffer, sizeof(buffer),
        "%u < %-22s>: Addr %p Size 0x%08"PRIx32" Priv: %c%c User: %c%c",
        i, MEMORY_REGION_NAMES[i], (void*) region.base_address, region.size,
        region.priv_read ? 'R' : ' ', region.priv_write ? 'W' : ' ',
        region.user_read ? 'R' : ' ', region.user_write ? 'W' : ' ');

#ifdef MPU_ARMV8
    // Show ARMv8-M specific attributes
    const char *shareability_str = "???";
    switch (region.shareability) {
      case MpuShareability_NonShareable: shareability_str = "Non-Share"; break;
      case MpuShareability_OuterShareable: shareability_str = "Outer-Share"; break;
      case MpuShareability_InnerShareable: shareability_str = "Inner-Share"; break;
      default: break;
    }
    
    PBL_LOG_FROM_FAULT_HANDLER_FMT(
        buffer, sizeof(buffer),
        "  XN: %c  Share: %s  Attr: %u",
        region.execute_never ? 'Y' : 'N',
        shareability_str,
        region.cache_policy);
#else
    if (region.disabled_subregions) {
      PBL_LOG_FROM_FAULT_HANDLER_FMT(
          buffer, sizeof(buffer),
          "  Disabled Subregions: %02x", region.disabled_subregions);
    }
#endif
  }
}

#ifdef UNITTEST
static const uint32_t __privileged_functions_start__ = 0;
static const uint32_t __privileged_functions_size__ = 0;
static const uint32_t __unpriv_ro_bss_start__ = 0;
static const uint32_t __unpriv_ro_bss_size__ = 0;
static const uint32_t __isr_stack_start__ = 0;
static const uint32_t __stack_guard_size__ = 0;

static const uint32_t __APP_RAM__ = 0;
static const uint32_t __WORKER_RAM__ = 0;

static const uint32_t __FLASH_start__ = 0;
static const uint32_t __FLASH_size__ = 0;

static const uint32_t __kernel_main_stack_start__ = 0;
static const uint32_t __kernel_bg_stack_start__ = 0;
#else
extern const uint32_t __privileged_functions_start__[];
extern const uint32_t __privileged_functions_size__[];
extern const uint32_t __unpriv_ro_bss_start__[];
extern const uint32_t __unpriv_ro_bss_size__[];
extern const uint32_t __isr_stack_start__[];
extern const uint32_t __stack_guard_size__[];

extern const uint32_t __APP_RAM__[];
extern const uint32_t __WORKER_RAM__[];

extern const uint32_t __FLASH_start__[];
extern const uint32_t __FLASH_size__[];

extern const uint32_t __kernel_main_stack_start__[];
extern const uint32_t __kernel_bg_stack_start__[];
#endif

#ifdef MICRO_FAMILY_SF32LB52
// Static MPU regions for SF32LB52 ARMv8-M (replaces HAL hardcoded setup)
// Region 0: PSRAM and Flash2 (0x10000000 - 0x1fffffff)
// Original HAL covered full 256MB range including QSPI1/PSRAM and QSPI2/Flash2
// Original HAL used ATTR_CODE = ARM_MPU_ATTR_MEMORY_(0, 0, 1, 0) = WriteThrough, no write-allocate
static const MpuRegion s_flash2_region = {
  .region_num = MemoryRegion_Reserved0,
  .enabled = true,
  .base_address = 0x10000000UL,  // QSPI1/PSRAM and QSPI2/Flash2 base (matches original HAL)
  .size = 0x10000000UL,  // 256 MB (0x10000000-0x1fffffff, matches original HAL)
  .cache_policy = MpuCachePolicy_WriteThrough,  // Matches original HAL ATTR_CODE
  .priv_read = true,
  .priv_write = false,   // Read-only (matches original HAL)
  .user_read = true,
  .user_write = false,
  .execute_never = false,  // Executable (contains code)
  .shareability = MpuShareability_NonShareable,
};

// Region 1: Peripheral space (0x40000000 - 0x5fffffff)
static const MpuRegion s_peripheral_region = {
  .region_num = MemoryRegion_Reserved1,
  .enabled = true,
  .base_address = 0x40000000UL,
  .size = 0x20000000UL,  // 512 MB
  .cache_policy = MpuCachePolicy_DeviceNGnRnE,  // Device memory, strictest ordering
  .priv_read = true,
  .priv_write = true,
  .user_read = true,
  .user_write = true,
  .execute_never = true,  // Never execute from peripherals
  .shareability = MpuShareability_NonShareable,
};

// Region 2: HPSYS RAM (0x20000000 - 0x2027ffff) - 2.5 MB
// IMPORTANT: Must be cacheable for performance! Original HAL used non-cacheable, but that causes
// severe performance degradation leading to stack overflows and timing issues.
// Only LPSYS RAM (shared with LCPU) needs to be non-cacheable for BLE IPC coherency.
static const MpuRegion s_hpsys_ram_region = {
  .region_num = MemoryRegion_Reserved2,
  .enabled = true,
  .base_address = 0x20000000UL,
  .size = 0x00280000UL,  // 2.5 MB
  .cache_policy = MpuCachePolicy_WriteBackWriteAllocate,  // Cacheable for performance!
  .priv_read = true,
  .priv_write = true,
  .user_read = true,
  .user_write = true,
  .execute_never = false,  // Allow execution (contains stacks and may have trampolines)
  .shareability = MpuShareability_NonShareable,
};

// Region 3: LPSYS RAM (0x203fc000 - 0x204fffff)
// This RAM is shared with the BLE controller (LCPU) for IPC communication
// Using NotCacheable to guarantee coherency between cores (performance tradeoff)
static const MpuRegion s_lpsys_ram_region = {
  .region_num = MemoryRegion_Reserved3,
  .enabled = true,
  .base_address = 0x203fc000UL,
  .size = 0x00104000UL,  // ~1 MB
  .cache_policy = MpuCachePolicy_NotCacheable,  // Not cached for guaranteed dual-core coherency
  .priv_read = true,
  .priv_write = true,
  .user_read = true,
  .user_write = true,
  .execute_never = false,  // Allow execution
  .shareability = MpuShareability_InnerShareable,  // Shared with LCPU for IPC!
};
#endif

// Kernel read only RAM. Parts of RAM that it's kosher for unprivileged apps to read
static const MpuRegion s_readonly_bss_region = {
  .region_num = MemoryRegion_ReadOnlyBss,
  .enabled = true,
  .base_address = (uint32_t) __unpriv_ro_bss_start__,
  .size = (uint32_t) __unpriv_ro_bss_size__,
  .cache_policy = MpuCachePolicy_WriteBackWriteAllocate,
  .priv_read = true,
  .priv_write = true,
  .user_read = true,
  .user_write = false,
#ifdef MPU_ARMV8
  .execute_never = true,  // Data only, no execution
  .shareability = MpuShareability_NonShareable,
#endif
};

// ISR stack guard
static const MpuRegion s_isr_stack_guard_region = {
  .region_num = MemoryRegion_IsrStackGuard,
  .enabled = true,
  .base_address = (uint32_t) __isr_stack_start__,
  .size = (uint32_t) __stack_guard_size__,
  .cache_policy = MpuCachePolicy_NotCacheable,
  .priv_read = false,
  .priv_write = false,
  .user_read = false,
  .user_write = false,
#ifdef MPU_ARMV8
  .execute_never = true,  // Guard region - no access at all
  .shareability = MpuShareability_NonShareable,
#endif
};

static const MpuRegion s_app_stack_guard_region = {
  .region_num = MemoryRegion_TaskStackGuard,
  .enabled = true,
  .base_address = (uint32_t) __APP_RAM__,
  .size = (uint32_t) __stack_guard_size__,
  .cache_policy = MpuCachePolicy_NotCacheable,
  .priv_read = false,
  .priv_write = false,
  .user_read = false,
  .user_write = false,
#ifdef MPU_ARMV8
  .execute_never = true,  // Guard region - no access at all
  .shareability = MpuShareability_NonShareable,
#endif
};

static const MpuRegion s_worker_stack_guard_region = {
  .region_num = MemoryRegion_TaskStackGuard,
  .enabled = true,
  .base_address = (uint32_t) __WORKER_RAM__,
  .size = (uint32_t) __stack_guard_size__,
  .cache_policy = MpuCachePolicy_NotCacheable,
  .priv_read = false,
  .priv_write = false,
  .user_read = false,
  .user_write = false,
#ifdef MPU_ARMV8
  .execute_never = true,  // Guard region - no access at all
  .shareability = MpuShareability_NonShareable,
#endif
};

static const MpuRegion s_app_region = {
  .region_num = MemoryRegion_AppRAM,
  .enabled = true,
  .base_address = MPU_REGION_APP_BASE_ADDRESS,
  .size = MPU_REGION_APP_SIZE,
#ifndef MPU_ARMV8
  .disabled_subregions = MPU_REGION_APP_DISABLED_SUBREGIONS,
#endif
  .cache_policy = MpuCachePolicy_WriteBackWriteAllocate,
  .priv_read = true,
  .priv_write = true,
#ifdef MPU_ARMV8
  .execute_never = true,  // App data RAM - no execution
  .shareability = MpuShareability_NonShareable,
#endif
};

static const MpuRegion s_worker_region = {
  .region_num = MemoryRegion_WorkerRAM,
  .enabled = true,
  .base_address = MPU_REGION_WORKER_BASE_ADDRESS,
  .size = MPU_REGION_WORKER_SIZE,
#ifndef MPU_ARMV8
  .disabled_subregions = MPU_REGION_WORKER_DISABLED_SUBREGIONS,
#endif
  .cache_policy = MpuCachePolicy_WriteBackWriteAllocate,
  .priv_read = true,
  .priv_write = true,
#ifdef MPU_ARMV8
  .execute_never = true,  // Worker data RAM - no execution
  .shareability = MpuShareability_NonShareable,
#endif
};

static const MpuRegion s_microflash_region = {
  .region_num = MemoryRegion_Flash,
  .enabled = true,
  .base_address = (uint32_t) __FLASH_start__,
  .size = (uint32_t) __FLASH_size__,
  .cache_policy = MpuCachePolicy_WriteThrough,
  .priv_read = true,
  .priv_write = false,
  .user_read = true,
  .user_write = false,
#ifdef MPU_ARMV8
  .execute_never = false,  // Flash contains code - executable
  .shareability = MpuShareability_NonShareable,
#endif
};

static const MpuRegion s_kernel_main_stack_guard_region = {
  .region_num = MemoryRegion_TaskStackGuard,
  .enabled = true,
  .base_address = (uint32_t) __kernel_main_stack_start__,
  .size = (uint32_t) __stack_guard_size__,
  .cache_policy = MpuCachePolicy_NotCacheable,
  .priv_read = false,
  .priv_write = false,
  .user_read = false,
  .user_write = false,
#ifdef MPU_ARMV8
  .execute_never = true,  // Guard region - no access at all
  .shareability = MpuShareability_NonShareable,
#endif
};

static const MpuRegion s_kernel_bg_stack_guard_region = {
  .region_num = MemoryRegion_TaskStackGuard,
  .enabled = true,
  .base_address = (uint32_t) __kernel_bg_stack_start__,
  .size = (uint32_t) __stack_guard_size__,
  .cache_policy = MpuCachePolicy_NotCacheable,
  .priv_read = false,
  .priv_write = false,
  .user_read = false,
  .user_write = false,
#ifdef MPU_ARMV8
  .execute_never = true,  // Guard region - no access at all
  .shareability = MpuShareability_NonShareable,
#endif
};

void memory_layout_setup_mpu(void) {
#ifdef MICRO_FAMILY_SF32LB52
    // Disable MPU before reconfiguration (matches original HAL approach)
    ARM_MPU_Disable();
    
    // Clear all regions first
    for (uint8_t i = 0U; i < MPU_REGION_NUM; i++) {
        ARM_MPU_ClrRegion(i);
    }
    
    // Configure all 8 MAIR attributes (matching MpuCachePolicy enum)
    // These must be set before enabling MPU
    ARM_MPU_SetMemAttr(0, ARM_MPU_ATTR(ARM_MPU_ATTR_DEVICE, ARM_MPU_ATTR_DEVICE_nGnRnE));
    ARM_MPU_SetMemAttr(1, ARM_MPU_ATTR(ARM_MPU_ATTR_DEVICE_nGnRE, ARM_MPU_ATTR_DEVICE_nGnRE));
    ARM_MPU_SetMemAttr(2, ARM_MPU_ATTR(ARM_MPU_ATTR_DEVICE_nGRE, ARM_MPU_ATTR_DEVICE_nGRE));
    ARM_MPU_SetMemAttr(3, ARM_MPU_ATTR(ARM_MPU_ATTR_DEVICE_GRE, ARM_MPU_ATTR_DEVICE_GRE));
    ARM_MPU_SetMemAttr(4, ARM_MPU_ATTR(ARM_MPU_ATTR_NON_CACHEABLE, ARM_MPU_ATTR_NON_CACHEABLE));
    ARM_MPU_SetMemAttr(5, ARM_MPU_ATTR(ARM_MPU_ATTR_MEMORY_(1, 0, 1, 0), ARM_MPU_ATTR_MEMORY_(1, 0, 1, 0)));
    ARM_MPU_SetMemAttr(6, ARM_MPU_ATTR(ARM_MPU_ATTR_MEMORY_(1, 1, 1, 1), ARM_MPU_ATTR_MEMORY_(1, 1, 1, 1)));
    ARM_MPU_SetMemAttr(7, ARM_MPU_ATTR(ARM_MPU_ATTR_MEMORY_(1, 1, 0, 1), ARM_MPU_ATTR_MEMORY_(1, 1, 0, 1)));
    
    // Configure static regions 0-3 (critical for system operation)
    mpu_set_region(&s_flash2_region);        // Region 0: Flash2/QSPI
    mpu_set_region(&s_peripheral_region);    // Region 1: Peripherals
    mpu_set_region(&s_hpsys_ram_region);     // Region 2: Main RAM (cacheable)
    mpu_set_region(&s_lpsys_ram_region);     // Region 3: LPSYS RAM (non-cacheable for BLE IPC!)
    
    // Clear regions 4-7 so FreeRTOS can configure them for task-specific protection
    for (uint32_t i = 4; i < 8; i++) {
        ARM_MPU_ClrRegion(i);
    }
    
    // Enable MPU with HFNMIENA for HardFault/NMI handler access (matches original HAL)
    ARM_MPU_Enable(MPU_CTRL_HFNMIENA_Msk);
#else
  
  // Configure additional static regions
  // Flash parts - read only for executing code and loading data out of.
  mpu_set_region(&s_microflash_region);

  // RAM parts
  // The background memory map only allows privileged access. We need to add aditional regions to
  // enable access to unprivileged code.

  mpu_set_region(&s_readonly_bss_region);
  mpu_set_region(&s_isr_stack_guard_region);
  mpu_enable();
#endif
}

const MpuRegion* memory_layout_get_app_region(void) {
  return &s_app_region;
}

const MpuRegion* memory_layout_get_readonly_bss_region(void) {
  return &s_readonly_bss_region;
}

const MpuRegion* memory_layout_get_app_stack_guard_region(void) {
  return &s_app_stack_guard_region;
}

const MpuRegion* memory_layout_get_worker_region(void) {
  return &s_worker_region;
}

const MpuRegion* memory_layout_get_worker_stack_guard_region(void) {
  return &s_worker_stack_guard_region;
}

const MpuRegion* memory_layout_get_microflash_region(void) {
  return &s_microflash_region;
}

const MpuRegion* memory_layout_get_kernel_main_stack_guard_region(void) {
  return &s_kernel_main_stack_guard_region;
}

const MpuRegion* memory_layout_get_kernel_bg_stack_guard_region(void) {
  return &s_kernel_bg_stack_guard_region;
}

bool memory_layout_is_pointer_in_region(const MpuRegion *region, const void *ptr) {
  uintptr_t p = (uintptr_t) ptr;
  return (p >= region->base_address && p < (region->base_address + region->size));
}

bool memory_layout_is_buffer_in_region(const MpuRegion *region, const void *buf, size_t length) {
  return memory_layout_is_pointer_in_region(region, buf) && memory_layout_is_pointer_in_region(region, (char *)buf + length - 1);
}

bool memory_layout_is_cstring_in_region(const MpuRegion *region, const char *str, size_t max_length) {
  uintptr_t region_end = region->base_address + region->size;

  if ((uintptr_t) str < region->base_address || (uintptr_t) str >= region_end) {
    return false;
  }

  const char *str_max_end = MIN((const char*) region_end, str + max_length);

  size_t str_len = strnlen(str, str_max_end - str);

  if (str[str_len] != 0) {
    // No null between here and the end of the memory region.
    return false;
  }

  return true;
}
