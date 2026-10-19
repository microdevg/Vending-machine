#ifndef COMM_H
#define COMM_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_event.h"

// Declaración de la base de eventos
ESP_EVENT_DECLARE_BASE(COMM_EVENT_BASE);

// Identificadores de eventos del módulo COMM
typedef enum {
    COMM_EVENT_CONNECTED,        // Conexión con el servidor establecida (hello_ack recibido)
    COMM_EVENT_DISCONNECTED,     // Se perdió la conexión con el servidor
    COMM_EVENT_PAYMENT_OK,       // Cobro verificado y aprobado por servidor   (data: comm_payment_result_t)
    COMM_EVENT_PAYMENT_FAILED    // Fallo de stock, timeout o pago rechazado   (data: comm_payment_result_t)
} comm_event_id_t;

#define COMM_REASON_LEN 24

// Datos de COMM_EVENT_PAYMENT_OK / COMM_EVENT_PAYMENT_FAILED
typedef struct {
    uint32_t item_id;
    uint32_t req_id;
    // Motivo del rechazo. Del servidor: "no_stock", "unknown_item".
    // Locales: "timeout", "disconnected".
    char reason[COMM_REASON_LEN];
} comm_payment_result_t;

void comm_init(esp_event_loop_handle_t loop_handle);

bool comm_is_connected(void);

// Encola un pedido de cobro. El resultado llega como evento PAYMENT_OK / PAYMENT_FAILED.
// Devuelve ESP_ERR_INVALID_STATE si no hay conexión, ESP_ERR_TIMEOUT si ya hay un pago en curso.
esp_err_t comm_start_payment_by_item(uint32_t item_id);

#endif // COMM_H
