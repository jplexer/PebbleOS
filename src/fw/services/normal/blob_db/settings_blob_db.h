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

#pragma once

#include "api.h"

//! Settings BlobDB - wraps SettingsFile to provide BlobDB interface
//!
//! This allows settings to sync using the existing BlobDB protocol,
//! so the mobile app can reuse its BlobDB sync implementation.
//!
//! Only whitelisted settings are synced (see settings_blob_db.c for list).

//! Initialize the settings BlobDB
void settings_blob_db_init(void);

//! Insert/update a setting
status_t settings_blob_db_insert(const uint8_t *key, int key_len,
                                 const uint8_t *val, int val_len);

//! Get the length of a setting value
int settings_blob_db_get_len(const uint8_t *key, int key_len);

//! Read a setting value
status_t settings_blob_db_read(const uint8_t *key, int key_len,
                               uint8_t *val_out, int val_len);

//! Delete a setting
status_t settings_blob_db_delete(const uint8_t *key, int key_len);

//! Get list of dirty (unsynced) settings
BlobDBDirtyItem *settings_blob_db_get_dirty_list(void);

//! Mark a setting as synced
status_t settings_blob_db_mark_synced(const uint8_t *key, int key_len);

//! Check if there are dirty settings
status_t settings_blob_db_is_dirty(bool *is_dirty_out);

//! Flush settings to disk
status_t settings_blob_db_flush(void);
