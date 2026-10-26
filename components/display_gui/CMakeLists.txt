
file(GLOB_RECURSE UISQUARELINE "*.c")

idf_component_register(SRCS ${UISQUARELINE} "display.c" "lvgl_example.c"
                    INCLUDE_DIRS "include" "ui" "ui/components" "ui/fonts" "ui/screens"
                    REQUIRES driver esp_timer esp_lcd_st7789 lvgl espressif__esp_lvgl_port
)
                 