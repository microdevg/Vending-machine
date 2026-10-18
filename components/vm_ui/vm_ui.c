#include <stdio.h>
#include "esp_log.h"
#include "vm_ui.h"

static const char *TAG = "VM_UI";

// DEFINICIÓN OBLIGATORIA: Asigna la memoria del símbolo que busca el linker
ESP_EVENT_DEFINE_BASE(UI_EVENT_BASE);

// Guardamos el handle para poder enviar eventos más adelante
static esp_event_loop_handle_t ui_loop_handle = NULL;

void ui_init(esp_event_loop_handle_t loop_handler) {
    ui_loop_handle = loop_handler;
    ESP_LOGI(TAG, "Interfaz de usuario inicializada.");


    // Prueba: Simular la selección de un producto después de 2 segundos

    vTaskDelay(pdMS_TO_TICKS(10000));
    ui_select_product(42);  // Simula que el usuario selecciona el producto con ID 42
}

void ui_deinit(void) {
    ui_loop_handle = NULL;
    ESP_LOGI(TAG, "Interfaz de usuario apagada.");
}

// Función que se llamaría cuando el usuario toca la pantalla
void ui_select_product(uint32_t product_id) {
    if (ui_loop_handle == NULL) return;
   

    ESP_LOGI(TAG, "Producto %d seleccionado por el usuario. Publicando evento...", product_id);

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