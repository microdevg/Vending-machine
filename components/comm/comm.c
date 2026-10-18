#include <stdio.h>
#include "esp_log.h"
#include "esp_event.h"
#include "comm.h"

static const char *TAG = "COMM";

// LÍNEA QUE FALTA: Asigna la memoria para la base de eventos
ESP_EVENT_DEFINE_BASE(COMM_EVENT_BASE);

static esp_event_loop_handle_t comm_loop_handle = NULL;

void comm_init(esp_event_loop_handle_t loop_handle) {
    comm_loop_handle = loop_handle;
    ESP_LOGI(TAG, "Módulo de comunicaciones inicializado.");
}

void comm_start_payment(float amount) {
    if (comm_loop_handle == NULL) return;

    ESP_LOGI(TAG, "Iniciando pago de $%.2f", amount);

    // Ejemplo de emisión de evento usando la base definida arriba
    bool success = true;
    if (success) {
        esp_event_post_to(comm_loop_handle, COMM_EVENT_BASE, COMM_EVENT_PAYMENT_OK, NULL, 0, portMAX_DELAY);
    } else {
        esp_event_post_to(comm_loop_handle, COMM_EVENT_BASE, COMM_EVENT_PAYMENT_FAILED, NULL, 0, portMAX_DELAY);
    }
}