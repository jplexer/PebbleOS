/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "applib/event_service_client.h"
#include "applib/mic_data_service.h"

typedef struct MicDataServiceState {
  EventServiceInfo event_info;
  MicDataHandlers handlers;
  void *context;
  int16_t *buffer; //!< samples_per_update samples, owned by the subscription
  uint32_t samples_per_update;
  bool active;
} MicDataServiceState;

void mic_data_service_state_init(MicDataServiceState *state);
