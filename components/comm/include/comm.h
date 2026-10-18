#ifndef COMM_H
#define COMM_H

#include "esp_event.h"

// Declaración de la base de eventos
ESP_EVENT_DECLARE_BASE(COMM_EVENT_BASE);

// Identificadores de eventos del módulo COMM
typedef enum {
    COMM_EVENT_PAYMENT_REQUEST,  // Notifica la solicitud de cobro por ítem
    COMM_EVENT_PAYMENT_OK,       // Cobro verificado y aprobado por servidor
    COMM_EVENT_PAYMENT_FAILED    // Fallo de stock, precio o pago rechazado
} comm_event_id_t;


void comm_init(esp_event_loop_handle_t loop_handle);
void comm_start_payment_by_item(uint32_t item_id);

#endif // COMM_H