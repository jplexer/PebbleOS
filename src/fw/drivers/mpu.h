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

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "freertos_types.h"

#ifdef MICRO_FAMILY_SF32LB52
#define MPU_ARMV8
#endif

typedef enum MpuCachePolicy {
  MpuCachePolicy_DeviceNGnRnE,              // Device-nGnRnE (peripherals, no gathering, reordering, early write ack)
  MpuCachePolicy_DeviceNGnRE,               // Device-nGnRE (peripherals, no gathering/reordering, early write ack)
  MpuCachePolicy_DeviceNGRE,                // Device-nGRE (peripherals, no gathering, early write ack)
  MpuCachePolicy_DeviceGRE,                 // Device-GRE (peripherals, early write ack allowed)
  MpuCachePolicy_NotCacheable,
  MpuCachePolicy_WriteThrough,
  MpuCachePolicy_WriteBackWriteAllocate,
  MpuCachePolicy_WriteBackNoWriteAllocate,
} MpuCachePolicy;

// ARMv8-M Shareability attributes
typedef enum MpuShareability {
  MpuShareability_NonShareable = 0,   // Non-shareable
  MpuShareability_Reserved = 1,       // Reserved (do not use)
  MpuShareability_OuterShareable = 2, // Outer shareable
  MpuShareability_InnerShareable = 3, // Inner shareable
} MpuShareability;

typedef struct MpuRegion {
  uint8_t region_num:4;
  bool enabled:1;
  uintptr_t base_address;
  uint32_t size;
  MpuCachePolicy cache_policy;
  bool priv_read:1;
  bool priv_write:1;
  bool user_read:1;
  bool user_write:1;
#ifdef MPU_ARMV8
  bool execute_never:1;              // ARMv8-M XN bit: prevents code execution from this region
  MpuShareability shareability:2;    // ARMv8-M shareability attribute
#else
  uint8_t disabled_subregions;       // ARMv7-M: 8 bits, each disables 1/8 of the region.
#endif
} MpuRegion;

void mpu_enable(void);

void mpu_disable(void);

void mpu_set_region(const MpuRegion* region);

MpuRegion mpu_get_region(int region_num);

void mpu_get_register_settings(const MpuRegion* region, uint32_t *base_address_reg,
                               uint32_t *attributes_reg);

void mpu_set_task_configurable_regions(MemoryRegion_t *memory_regions,
                                       const MpuRegion **region_ptrs);

bool mpu_memory_is_cachable(const void *addr);

void mpu_init_region_from_region(MpuRegion *copy, const MpuRegion *from, bool allow_user_access);
