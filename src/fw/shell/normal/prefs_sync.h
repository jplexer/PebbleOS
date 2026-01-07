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

//! Prefs Sync Integration
//!
//! This module integrates settings sync with the shell prefs system.
//! It handles:
//! - Whitelisting of syncable preferences
//! - Automatic sync on connection to phone
//! - Debouncing for rapid preference changes

//! Initialize prefs sync
//! Call this from shell_prefs_init() after prefs are loaded
void prefs_sync_init(void);

//! Deinitialize prefs sync
void prefs_sync_deinit(void);

//! Manually trigger a sync (e.g., for testing)
void prefs_sync_trigger(void);
