#ifndef COMM_H
#define COMM_H

#include "esp_event.h"

// Declaración pública de la base de eventos
ESP_EVENT_DECLARE_BASE(COMM_EVENT_BASE);

typedef enum {
    COMM_EVENT_PAYMENT_OK,
    COMM_EVENT_PAYMENT_FAILED,
    COMM_EVENT_CONNECTION_LOST,
} comm_event_id_t;

void comm_init(esp_event_loop_handle_t loop_handle);
void comm_start_payment(float amount);

#endif // COMM_H