#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t display_gui_init(void);
void display_gui_update_lines(const char *line1, const char *line2);
void display_show(const char *line1, const char *line2);
void display_deinit(void);

#ifdef __cplusplus
}
#endif