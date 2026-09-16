/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "pbl/util/attributes.h"

#include <stdint.h>

//! @addtogroup Foundation
//! @{
//!   @addtogroup Permissions
//!   @{

//! Permissions an app can declare in its manifest and the user can grant or deny.
typedef enum AppPermission {
  //! Live microphone capture (`capabilities: ["microphone"]`)
  AppPermission_Microphone = 0,
  AppPermissionCount,
} AppPermission;

//! State of a permission for one app.
typedef enum AppPermissionState {
  //! The app does not declare the permission in its manifest.
  AppPermissionStateNotDeclared = 0,
  //! The permission is declared but has not been granted by the user.
  AppPermissionStateDenied = 1,
  //! The permission is granted.
  AppPermissionStateGranted = 2,
} AppPermissionState;

//!   @} // end addtogroup Permissions
//! @} // end addtogroup Foundation

//! @internal
typedef uint32_t AppPermissionMask;

//! @internal
#define APP_PERMISSION_BIT(permission) ((AppPermissionMask)1u << (permission))

//! @internal
#define APP_PERMISSIONS_DB_ENTRY_VERSION (1)

//! @internal
//! Serialized grant record pushed by the phone (BlobDBIdAppPermissions, keyed by app Uuid).
typedef struct PACKED AppPermissionsDBEntry {
  uint8_t version; //!< APP_PERMISSIONS_DB_ENTRY_VERSION
  uint8_t reserved[3];
  AppPermissionMask granted_mask;  //!< Permissions the user granted
  AppPermissionMask declared_mask; //!< Permissions the app declared (informational)
} AppPermissionsDBEntry;

_Static_assert(sizeof(AppPermissionsDBEntry) == 12, "AppPermissionsDBEntry layout changed");
