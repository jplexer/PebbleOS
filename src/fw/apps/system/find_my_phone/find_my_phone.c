/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "find_my_phone.h"

#include "applib/app.h"
#include "applib/ui/app_window_stack.h"
#include "applib/ui/text_layer.h"
#include "kernel/pbl_malloc.h"
#include "process_management/pebble_process_md.h"
#include "system/passert.h"

static void prv_main(void) {
  Window *window = app_malloc_check(sizeof(Window));
  window_init(window, WINDOW_NAME("Find My Phone"));

  TextLayer *text = app_malloc_check(sizeof(TextLayer));
  text_layer_init(text, &GRect(0, DISP_ROWS / 3, DISP_COLS, 80));
  text_layer_set_text(text, "Find My\nPhone");
  text_layer_set_text_color(text, GColorBlack);
  text_layer_set_background_color(text, GColorClear);
  text_layer_set_font(text, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_alignment(text, GTextAlignmentCenter);
  layer_add_child(window_get_root_layer(window), text_layer_get_layer(text));

  app_window_stack_push(window, true);
  app_event_loop();
}

const PebbleProcessMd *find_my_phone_get_app_info(void) {
  static const PebbleProcessMdSystem s_app_md = {
    .common = {
      .main_func = prv_main,
      .visibility = ProcessVisibilityShown,
      // UUID: 438e789f-1f16-424e-829c-0c2f4865fbe8
      .uuid = {0x43, 0x8e, 0x78, 0x9f, 0x1f, 0x16, 0x42, 0x4e,
               0x82, 0x9c, 0x0c, 0x2f, 0x48, 0x65, 0xfb, 0xe8},
    },
    .name = "Find My Phone",
  };
  return (const PebbleProcessMd *)&s_app_md;
}
