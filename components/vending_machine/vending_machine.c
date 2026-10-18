#include <stdio.h>
#include "esp_log.h"
#include "esp_event.h"

#include "vending_machine.h"
#include "vm_ui.h"
#include "comm.h"

static const char *TAG = "VENDING_MACHINE";

// Definición de la base de eventos
ESP_EVENT_DEFINE_BASE(VM_EVENT_BASE);

// Manejador privado del bucle de eventos
static esp_event_loop_handle_t vm_event_loop;

    // Handler central en vending_machine.c
    static void vm_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
        
        // Si el evento viene del módulo UI
        if (base == UI_EVENT_BASE && id == UI_EVENT_PRODUCT_SELECTED) {
            uint8_t product_id = *(uint8_t *)event_data;
            
            // Vending Machine decide ordenar el cobro a comm
            comm_start_payment(150.0); 
        }
        
        // Si el evento viene del módulo COMM
        else if (base == COMM_EVENT_BASE && id == COMM_EVENT_PAYMENT_OK) {
            // Vending Machine procesa la confirmación de pago y despacha
        }
    }

void vm_init(void) {
    // 1. Configurar y crear el bucle de eventos dedicado
    esp_event_loop_args_t loop_args = {
        .queue_size = 20,              // Capacidad de eventos en cola
        .task_name = "vm_event_task",  // Nombre de la tarea que procesa eventos
        .task_priority = 5,            // Prioridad de FreeRTOS
        .task_stack_size = 4096,       // Tamaño de stack dedicado
        .task_core_id = tskNO_AFFINITY
    };

    ESP_ERROR_CHECK(esp_event_loop_create(&loop_args, &vm_event_loop));
    ESP_LOGI(TAG, "Bucle de eventos instanciado correctamente.");

    // 2. Suscribir el manejador principal de la Vending Machine
    ESP_ERROR_CHECK(esp_event_handler_register_with(
        vm_event_loop, 
        VM_EVENT_BASE, 
        ESP_EVENT_ANY_ID, 
        vm_event_handler, 
        NULL
    ));

    // 3. Inicializar componentes pasándoles el handle del bucle de eventos
    ui_init(vm_event_loop);
    comm_init(vm_event_loop);

    // 4. Probar elementos mecánicos y ponerse en modo espera
    ESP_LOGI(TAG, "Probando elementos mecánicos...");
    // ...
    ESP_LOGI(TAG, "Máquina lista para recibir compras.");
}