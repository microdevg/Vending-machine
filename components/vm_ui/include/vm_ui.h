#ifndef VM_UI_H
#define VM_UI_H

#include "esp_event.h"

// Declaración pública de la base de eventos
ESP_EVENT_DECLARE_BASE(UI_EVENT_BASE);

typedef enum {
    UI_EVENT_PRODUCT_SELECTED,
    UI_EVENT_CANCEL_PRESSED,
} ui_event_id_t;

// Prototipos de funciones
void ui_init(esp_event_loop_handle_t loop_handler);
void ui_deinit(void);

// Ejemplo de función para simular la selección de un producto desde la UI
void ui_select_product(uint8_t product_id);

#endif // VM_UI_H