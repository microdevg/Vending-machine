#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "vm_ui.h"

static const char *TAG = "VM_UI";

// DEFINICIÓN OBLIGATORIA: Asigna la memoria del símbolo que busca el linker
ESP_EVENT_DEFINE_BASE(UI_EVENT_BASE);

// Guardamos el handle para poder enviar eventos más adelante
static esp_event_loop_handle_t ui_loop_handle = NULL;

#ifdef CONFIG_VM_UI_SIMULATE
// Prueba: simula que el usuario selecciona productos periódicamente.
// Recorre productos con stock, sin stock (3) e inexistentes (999) del servidor de prueba.
static void ui_simulation_task(void *pvParameters) {
    const uint32_t products[] = { 42, 1, 3, 999 };
    size_t i = 0;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(CONFIG_VM_UI_SIMULATE_INTERVAL_MS));
        ui_select_product(products[i]);
        i = (i + 1) % (sizeof(products) / sizeof(products[0]));
    }
}
#endif

void ui_init(esp_event_loop_handle_t loop_handler) {
    ui_loop_handle = loop_handler;
    ESP_LOGI(TAG, "Interfaz de usuario inicializada.");

#ifdef CONFIG_VM_UI_SIMULATE
    xTaskCreate(ui_simulation_task, "ui_sim_task", 2048, NULL, 4, NULL);
#endif
}

void ui_deinit(void) {
    ui_loop_handle = NULL;
    ESP_LOGI(TAG, "Interfaz de usuario apagada.");
}

// Función que se llamaría cuando el usuario toca la pantalla
void ui_select_product(uint32_t product_id) {
    if (ui_loop_handle == NULL) return;


    ESP_LOGI(TAG, "Producto %" PRIu32 " seleccionado por el usuario. Publicando evento...", product_id);

    // Emite el evento hacia el bucle central
    esp_event_post_to(
        ui_loop_handle,
        UI_EVENT_BASE,
        UI_EVENT_PRODUCT_SELECTED,
        &product_id,
        sizeof(product_id),
        portMAX_DELAY
    );
}