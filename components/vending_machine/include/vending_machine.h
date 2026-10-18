#include "esp_event.h"


ESP_EVENT_DECLARE_BASE(VM_EVENT_BASE);

typedef enum {
    VM_EVENT_STATE_CHANGED, // Ej: "Despachando", "Error", "Listo"
} vm_event_id_t;


void vm_init();