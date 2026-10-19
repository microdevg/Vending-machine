#pragma once

#include "esp_event.h"


ESP_EVENT_DECLARE_BASE(VM_EVENT_BASE);

typedef enum {
    VM_EVENT_STATE_CHANGED, // data: vm_state_t. Ej: "Despachando", "Error", "Listo"
} vm_event_id_t;

typedef enum {
    VM_STATE_OFFLINE,       // Sin conexión con el servidor: no se aceptan compras
    VM_STATE_IDLE,          // Lista para recibir compras
    VM_STATE_PAYING,        // Esperando la respuesta del servidor
    VM_STATE_DISPENSING,    // Entregando el producto
} vm_state_t;


void vm_init(void);