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

#include "prefs_sync.h"

#include "applib/event_service_client.h"
#include "kernel/events.h"
#include "services/common/debounced_connection_service.h"
#include "services/normal/blob_db/api.h"
#include "services/normal/blob_db/sync.h"
#include "system/logging.h"

//! Prefs Sync using BlobDB
//!
//! Settings are now synced via BlobDB with database ID 0x0F (BlobDBIdSettings).
//! 
//! The whitelist filtering and sync logic are implemented in:
//!   services/normal/blob_db/settings_blob_db.c
//!
//! This module simply triggers BlobDB sync when the phone connects.

static bool s_sync_initialized = false;
static bool s_is_connected = false;
static EventServiceInfo s_event_info;

//! Connection state change callback
static void prv_connection_handler(PebbleEvent *event, void *context) {
  bool connected = event->bluetooth.comm_session_event.is_open;
  s_is_connected = connected;
  
  if (connected) {
    PBL_LOG(LOG_LEVEL_INFO, "Phone connected, triggering settings sync via BlobDB");
    
    // Trigger BlobDB sync for settings database
    status_t status = blob_db_sync_db(BlobDBIdSettings);
    
    if (status == S_SUCCESS) {
      PBL_LOG(LOG_LEVEL_INFO, "Settings sync started");
    } else if (status == S_NO_ACTION_REQUIRED) {
      PBL_LOG(LOG_LEVEL_DEBUG, "No settings need syncing");
    } else if (status == E_BUSY) {
      PBL_LOG(LOG_LEVEL_DEBUG, "Settings sync already in progress");
    } else {
      PBL_LOG(LOG_LEVEL_ERROR, "Failed to start settings sync: 0x%"PRIx32"", status);
    }
  } else {
    PBL_LOG(LOG_LEVEL_INFO, "Phone disconnected");
  }
}

// Public API

void prefs_sync_init(void) {
  if (s_sync_initialized) {
    PBL_LOG(LOG_LEVEL_WARNING, "Prefs sync already initialized");
    return;
  }
  
  // Subscribe to connection events using kernel-side event service
  s_event_info = (EventServiceInfo) {
    .type = PEBBLE_BT_CONNECTION_DEBOUNCED_EVENT,
    .handler = prv_connection_handler,
  };
  event_service_client_subscribe(&s_event_info);
  
  // Start with disconnected state - will be updated when we receive connection events
  s_is_connected = false;
  
  s_sync_initialized = true;
  
  PBL_LOG(LOG_LEVEL_INFO, "Prefs sync initialized (using BlobDB ID 0x0F)");
}

void prefs_sync_deinit(void) {
  if (!s_sync_initialized) {
    return;
  }
  
  // Unsubscribe from connection events
  event_service_client_unsubscribe(&s_event_info);
  
  s_sync_initialized = false;
  s_is_connected = false;
  
  PBL_LOG(LOG_LEVEL_INFO, "Prefs sync deinitialized");
}

void prefs_sync_trigger(void) {
  if (!s_sync_initialized) {
    PBL_LOG(LOG_LEVEL_WARNING, "Prefs sync not initialized");
    return;
  }
  
  if (!s_is_connected) {
    PBL_LOG(LOG_LEVEL_WARNING, "Not connected to phone, cannot sync");
    return;
  }
  
  PBL_LOG(LOG_LEVEL_INFO, "Manually triggering settings sync via BlobDB");
  
  status_t status = blob_db_sync_db(BlobDBIdSettings);
  
  if (status == S_SUCCESS) {
    PBL_LOG(LOG_LEVEL_INFO, "Settings sync started");
  } else if (status == S_NO_ACTION_REQUIRED) {
    PBL_LOG(LOG_LEVEL_INFO, "No settings need syncing");
  } else if (status == E_BUSY) {
    PBL_LOG(LOG_LEVEL_WARNING, "Settings sync already in progress");
  } else {
    PBL_LOG(LOG_LEVEL_ERROR, "Failed to start settings sync: 0x%"PRIx32"", status);
  }
}
