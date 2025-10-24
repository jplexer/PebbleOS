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

#include "settings_blob_db.h"

#include "kernel/pbl_malloc.h"
#include "services/normal/settings/settings_file.h"
#include "shell/prefs_private.h"
#include "system/logging.h"
#include "util/list.h"
#include "util/size.h"

#include <string.h>

//! Settings Whitelist
//!
//! Only these settings will be synced via BlobDB.
//! This prevents sensitive data (Bluetooth pairing, debug flags, etc.) from syncing.
static const char *s_syncable_settings[] = {
  // Clock preferences
  "clock24h",
  "timezoneSource",
  "automaticTimezoneID",

  // Display preferences
  "unitsDistance",
  "textStyle",

  // Backlight preferences
  "lightEnabled",
  "lightAmbientSensorEnabled",
  "lightTimeoutMs",
  "lightIntensity",
  "lightMotion",
  "lightAmbientThreshold",

  // Language preferences
  "langEnglish",

  // App preferences
  "watchface",
  "qlUp",
  "qlDown",
  "qlSelect",
  "qlBack",
  "qlSetupOpened",

  // Activity preferences
#if CAPABILITY_HAS_HEALTH_TRACKING
  "activityPreferences",
  "activityHealthAppOpened",
#endif

  // Worker preferences
  "workerId",
};

static const size_t s_num_syncable_settings = ARRAY_LENGTH(s_syncable_settings);

static bool s_initialized = false;

//! Check if a setting key is in the sync whitelist
static bool prv_is_syncable(const uint8_t *key, int key_len) {
  for (size_t i = 0; i < s_num_syncable_settings; i++) {
    const char *syncable_key = s_syncable_settings[i];
    int syncable_len = (int)(strlen(syncable_key) + 1); // Include null terminator
    if (key_len == syncable_len && memcmp(key, syncable_key, (size_t)key_len) == 0) {
      return true;
    }
  }
  return false;
}

// BlobDB Interface Implementation

void settings_blob_db_init(void) {
  if (s_initialized) {
    return;
  }

  s_initialized = true;
  PBL_LOG(LOG_LEVEL_INFO, "Settings BlobDB initialized (%u whitelisted settings)",
          (unsigned int) s_num_syncable_settings);
}

status_t settings_blob_db_insert(const uint8_t *key, int key_len,
                                 const uint8_t *val, int val_len) {
  if (!s_initialized) {
    return E_INTERNAL;
  }

  // Only allow whitelisted settings to be synced
  if (!prv_is_syncable(key, key_len)) {
    char key_str[128];
    size_t copy_len = (key_len > 0 && (size_t)key_len < sizeof(key_str)) ?
                      (size_t)key_len : sizeof(key_str) - 1;
    memcpy(key_str, key, copy_len);
    key_str[copy_len] = '\0';
    PBL_LOG(LOG_LEVEL_WARNING, "Rejecting non-whitelisted setting: %s", key_str);
    return E_INVALID_OPERATION;
  }

  SettingsFile file;
  status_t status = settings_file_open(&file, SHELL_PREFS_FILE_NAME, SHELL_PREFS_FILE_LEN);
  if (FAILED(status)) {
    return status;
  }

  status = settings_file_set(&file, key, key_len, val, val_len);
  settings_file_close(&file);
  return status;
}

int settings_blob_db_get_len(const uint8_t *key, int key_len) {
  if (!s_initialized) {
    return E_INTERNAL;
  }

  SettingsFile file;
  status_t status = settings_file_open(&file, SHELL_PREFS_FILE_NAME, SHELL_PREFS_FILE_LEN);
  if (FAILED(status)) {
    return status;
  }

  int len = settings_file_get_len(&file, key, key_len);
  settings_file_close(&file);
  return len;
}

status_t settings_blob_db_read(const uint8_t *key, int key_len,
                               uint8_t *val_out, int val_len) {
  if (!s_initialized) {
    return E_INTERNAL;
  }

  SettingsFile file;
  status_t status = settings_file_open(&file, SHELL_PREFS_FILE_NAME, SHELL_PREFS_FILE_LEN);
  if (FAILED(status)) {
    return status;
  }

  status = settings_file_get(&file, key, key_len, val_out, val_len);
  settings_file_close(&file);
  return status;
}

status_t settings_blob_db_delete(const uint8_t *key, int key_len) {
  if (!s_initialized) {
    return E_INTERNAL;
  }

  // Only allow whitelisted settings to be deleted
  if (!prv_is_syncable(key, key_len)) {
    return E_INVALID_OPERATION;
  }

  SettingsFile file;
  status_t status = settings_file_open(&file, SHELL_PREFS_FILE_NAME, SHELL_PREFS_FILE_LEN);
  if (FAILED(status)) {
    return status;
  }

  status = settings_file_delete(&file, key, key_len);
  settings_file_close(&file);
  return status;
}

// Dirty list management

typedef struct {
  BlobDBDirtyItem *dirty_list;
  BlobDBDirtyItem *dirty_list_tail;
} BuildDirtyListContext;

static bool prv_build_dirty_list_callback(SettingsFile *file,
                                          SettingsRecordInfo *info,
                                          void *context) {
  BuildDirtyListContext *ctx = (BuildDirtyListContext *)context;

  // Skip settings that are already synced
  if (!info->dirty) {
    return true;
  }

  // Read the key to check whitelist
  uint8_t key_buf[SETTINGS_KEY_MAX_LEN];
  info->get_key(file, key_buf, info->key_len);

  // Only include whitelisted settings
  if (!prv_is_syncable(key_buf, info->key_len)) {
    return true; // Skip, continue iteration
  }

  // Allocate dirty item
  BlobDBDirtyItem *item = kernel_malloc_check(sizeof(BlobDBDirtyItem) + info->key_len);
  list_init((ListNode *)item);
  item->last_updated = (time_t)info->last_modified;
  item->key_len = info->key_len;
  memcpy(item->key, key_buf, info->key_len);

  // Add to list
  if (ctx->dirty_list == NULL) {
    ctx->dirty_list = item;
    ctx->dirty_list_tail = item;
  } else {
    ctx->dirty_list_tail = (BlobDBDirtyItem *)list_append(
        (ListNode *)ctx->dirty_list_tail, (ListNode *)item);
  }

  return true; // Continue iteration
}

BlobDBDirtyItem *settings_blob_db_get_dirty_list(void) {
  if (!s_initialized) {
    return NULL;
  }

  SettingsFile file;
  status_t status = settings_file_open(&file, SHELL_PREFS_FILE_NAME, SHELL_PREFS_FILE_LEN);
  if (FAILED(status)) {
    return NULL;
  }

  BuildDirtyListContext ctx = { .dirty_list = NULL, .dirty_list_tail = NULL };
  settings_file_each(&file, prv_build_dirty_list_callback, &ctx);
  settings_file_close(&file);

  return ctx.dirty_list;
}

status_t settings_blob_db_mark_synced(const uint8_t *key, int key_len) {
  if (!s_initialized) {
    return E_INTERNAL;
  }

  SettingsFile file;
  status_t status = settings_file_open(&file, SHELL_PREFS_FILE_NAME, SHELL_PREFS_FILE_LEN);
  if (FAILED(status)) {
    return status;
  }

  status = settings_file_mark_synced(&file, key, key_len);
  settings_file_close(&file);
  return status;
}

status_t settings_blob_db_is_dirty(bool *is_dirty_out) {
  if (!s_initialized) {
    return E_INTERNAL;
  }

  // Quick check: iterate and return true on first dirty whitelisted setting
  typedef struct {
    bool found_dirty;
  } IsDirtyContext;

  bool is_dirty_callback(SettingsFile *file, SettingsRecordInfo *info, void *context) {
    IsDirtyContext *ctx = (IsDirtyContext *)context;

    if (!info->dirty) {
      return true; // Continue
    }

    // Check if whitelisted
    uint8_t key_buf[SETTINGS_KEY_MAX_LEN];
    info->get_key(file, key_buf, info->key_len);

    if (prv_is_syncable(key_buf, info->key_len)) {
      ctx->found_dirty = true;
      return false; // Stop iteration
    }

    return true; // Continue
  }

  SettingsFile file;
  status_t status = settings_file_open(&file, SHELL_PREFS_FILE_NAME, SHELL_PREFS_FILE_LEN);
  if (FAILED(status)) {
    return status;
  }

  IsDirtyContext ctx = { .found_dirty = false };
  settings_file_each(&file, is_dirty_callback, &ctx);
  settings_file_close(&file);

  *is_dirty_out = ctx.found_dirty;
  return S_SUCCESS;
}

status_t settings_blob_db_flush(void) {
  if (!s_initialized) {
    return E_INTERNAL;
  }

  // SettingsFile writes are already atomic, no explicit flush needed
  PBL_LOG(LOG_LEVEL_DEBUG, "Settings BlobDB flush (no-op for SettingsFile)");
  return S_SUCCESS;
}
