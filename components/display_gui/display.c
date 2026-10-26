#include <stdio.h>
#include <sys/lock.h>
#include <sys/param.h>
#include <unistd.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_st7789.h"
#include "esp_lcd_types.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

// Incluir los encabezados de tu UI de SquareLine Studio
#include "ui/ui.h"
#include "display.h"

static const char *TAG = "JC2432W328_ST7789";

// ============================================================================
// CONFIGURACIÓN DE PINES Y RESOLUCIÓN - JC2432W328
// ============================================================================
#define LCD_HOST            SPI2_HOST

#define LCD_H_RES           320
#define LCD_V_RES           240

#define LCD_PIXEL_CLOCK_HZ  (20000000)
#define BITS_PER_PIXEL      16

#define LCD_MOSI            13
#define LCD_CLK             14
#define LCD_CS              15
#define LCD_DC              2
#define LCD_RST             4
#define LCD_MISO            12
#define LCD_BCKL            27   // Retroiluminación

#define LCD_CMD_BITS        8
#define LCD_PARAM_BITS      8

#define LVGL_DRAW_BUF_LINES 20
#define LVGL_TICK_PERIOD_MS 2
#define LVGL_TASK_MAX_DELAY_MS 500
#define LVGL_TASK_MIN_DELAY_MS 2
#define LVGL_TASK_STACK_SIZE (8 * 1024)
#define LVGL_TASK_PRIORITY   2

static _lock_t lvgl_api_lock;
static esp_lcd_panel_io_handle_t io_handle = NULL;
static esp_lcd_panel_handle_t panel_handle = NULL;
static lv_display_t *display = NULL;

static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io,
                                    esp_lcd_panel_io_event_data_t *edata,
                                    void *user_ctx) {
    lv_display_t *disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}

// Función para actualizar las dos líneas desde cualquier tarea de forma segura
void display_gui_update_lines(const char *line1, const char *line2)
{
    _lock_acquire(&lvgl_api_lock);
    
    if (line1 != NULL && ui_Lbllinea1 != NULL) {
        lv_label_set_text(ui_Lbllinea1, line1);
    }
    if (line2 != NULL && ui_Lbllinea2 != NULL) {
        lv_label_set_text(ui_Lbllinea2, line2);
    }
    
    _lock_release(&lvgl_api_lock);
}

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    esp_lcd_panel_handle_t panel_h = lv_display_get_user_data(disp);
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;

    // Corrección de orden de bytes RGB565 para pantallas SPI de 16 bits
    lv_draw_sw_rgb565_swap(px_map, (offsetx2 - offsetx1 + 1) * (offsety2 - offsety1 + 1));
    esp_lcd_panel_draw_bitmap(panel_h, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, px_map);
}

static void increase_lvgl_tick(void *arg) {
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void init_lcd_spi(void) {
    spi_bus_config_t bus_cfg = {
        .sclk_io_num = LCD_CLK,
        .mosi_io_num = LCD_MOSI,
        .miso_io_num = LCD_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * LVGL_DRAW_BUF_LINES * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus_cfg, SPI_DMA_CH_AUTO));
}

static void init_display(void) {
    // Configurar retroiluminación en GPIO 27
    gpio_reset_pin(LCD_BCKL);
    gpio_set_direction(LCD_BCKL, GPIO_MODE_OUTPUT);
    gpio_set_level(LCD_BCKL, 1);

    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = LCD_DC,
        .cs_gpio_num = LCD_CS,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = BITS_PER_PIXEL,
    };

    ESP_LOGI(TAG, "Instalando driver ST7789 para JC2432W328...");
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    // Ajustes de orientación y color
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, false));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, false, true));

    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
}

static void init_lvgl(void) {
    _lock_init(&lvgl_api_lock);
    lv_init();

    display = lv_display_create(LCD_H_RES, LCD_V_RES);

    size_t draw_buffer_sz = LCD_H_RES * LVGL_DRAW_BUF_LINES * sizeof(lv_color16_t);
    void *buf1 = spi_bus_dma_memory_alloc(LCD_HOST, draw_buffer_sz, 0);
    assert(buf1);
    void *buf2 = spi_bus_dma_memory_alloc(LCD_HOST, draw_buffer_sz, 0);
    assert(buf2);

    lv_display_set_buffers(display, buf1, buf2, draw_buffer_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_user_data(display, panel_handle);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display, lvgl_flush_cb);

    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &increase_lvgl_tick, 
        .name = "lvgl_tick"
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = notify_lvgl_flush_ready,
    };
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(io_handle, &cbs, display));
}

static void lvgl_port_task(void *arg) {
    ESP_LOGI(TAG, "Iniciando tarea LVGL");
    uint32_t time_till_next_ms = 0;

    while (1) {
        _lock_acquire(&lvgl_api_lock);
        time_till_next_ms = lv_timer_handler();
        _lock_release(&lvgl_api_lock);

        time_till_next_ms = MAX(time_till_next_ms, LVGL_TASK_MIN_DELAY_MS);
        time_till_next_ms = MIN(time_till_next_ms, LVGL_TASK_MAX_DELAY_MS);

        TickType_t delay_ticks = pdMS_TO_TICKS(time_till_next_ms);
        if (delay_ticks == 0) delay_ticks = 1;

        vTaskDelay(delay_ticks);
    }
}

void display_init(void) {
    ESP_LOGI(TAG, "Inicializando bus SPI");
    init_lcd_spi();

    ESP_LOGI(TAG, "Inicializando pantalla ST7789");
    init_display();

    ESP_LOGI(TAG, "Inicializando LVGL");
    init_lvgl();

    _lock_acquire(&lvgl_api_lock);
    ui_init(); // Inicializa la interfaz creada en SquareLine Studio
    _lock_release(&lvgl_api_lock);

    ESP_LOGI(TAG, "Creando tarea FreeRTOS de LVGL");
    xTaskCreate(lvgl_port_task, "LVGL", LVGL_TASK_STACK_SIZE, NULL, LVGL_TASK_PRIORITY, NULL);
}

esp_err_t display_gui_init(void) {
    display_init();
    return ESP_OK;
}

void display_show(const char *line1, const char *line2) {
    display_gui_update_lines(line1, line2);
}

void display_deinit(void) {
    // Opcional para limpieza de recursos
}