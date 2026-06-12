/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <inttypes.h>

#include "console/prompt.h"
#include "drivers/ambient_light.h"
#include "pbl/services/light.h"

void command_light_test(void) {
  char buffer[32];
  prompt_send_response_fmt(buffer, sizeof(buffer), "als: %" PRIu32,
                           ambient_light_get_light_level());
  light_enable_interaction();
  prompt_send_response_fmt(buffer, sizeof(buffer), "brightness: %" PRIu8 "%%",
                           light_get_current_brightness_percent());
}
