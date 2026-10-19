#include <stdio.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_event.h"



#include "nvs_flash.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "vending_machine.h"
#include "vm_ui.h"
#include "comm.h"

static const char *TAG = "VENDING_MACHINE";

// Definición de la base de eventos
ESP_EVENT_DEFINE_BASE(VM_EVENT_BASE);

// Manejador privado del bucle de eventos
static esp_event_loop_handle_t vm_event_loop;

static vm_state_t vm_state = VM_STATE_OFFLINE;

static const char *state_name(vm_state_t state) {
    switch (state) {
    case VM_STATE_OFFLINE:    return "OFFLINE";
    case VM_STATE_IDLE:       return "IDLE";
    case VM_STATE_PAYING:     return "PAYING";
    case VM_STATE_DISPENSING: return "DISPENSING";
    default:                  return "?";
    }
}

static void set_state(vm_state_t new_state) {
    if (new_state == vm_state) return;
    ESP_LOGI(TAG, "Estado: %s -> %s", state_name(vm_state), state_name(new_state));
    vm_state = new_state;
    // Timeout 0: se publica desde el propio handler del bucle, no se debe bloquear
    esp_event_post_to(vm_event_loop, VM_EVENT_BASE, VM_EVENT_STATE_CHANGED, &new_state, sizeof(new_state), 0);
}

static void vm_dispense(uint32_t item_id) {
    set_state(VM_STATE_DISPENSING);
    // TODO: accionar el mecanismo de entrega (motor / solenoide)
    ESP_LOGI(TAG, "Producto %" PRIu32 " despachado.", item_id);
    set_state(VM_STATE_IDLE);
}

    // Handler central en vending_machine.c
    static void vm_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {

        // Si el evento viene del módulo UI
        if (base == UI_EVENT_BASE && id == UI_EVENT_PRODUCT_SELECTED) {
            uint32_t product_id = *(uint32_t *)event_data;

            if (vm_state != VM_STATE_IDLE) {
                ESP_LOGW(TAG, "Selección de producto %" PRIu32 " ignorada (estado %s).", product_id, state_name(vm_state));
                return;
            }

            ESP_LOGI(TAG, "Producto seleccionado: %" PRIu32 ". Iniciando proceso de pago...", product_id);
            // Vending Machine decide ordenar el cobro a comm
            if (comm_start_payment_by_item(product_id) == ESP_OK) {
                set_state(VM_STATE_PAYING);
            } else {
                ESP_LOGW(TAG, "No se pudo iniciar el pago. Notificando al usuario...");
            }
        }

        // Si el evento viene del módulo COMM
        else if (base == COMM_EVENT_BASE && id == COMM_EVENT_CONNECTED) {
            ESP_LOGI(TAG, "Servidor conectado. Máquina lista para recibir compras.");
            set_state(VM_STATE_IDLE);
        }
        else if (base == COMM_EVENT_BASE && id == COMM_EVENT_DISCONNECTED) {
            ESP_LOGW(TAG, "Sistema desconectado del servidor.");
            set_state(VM_STATE_OFFLINE);
        }
        else if (base == COMM_EVENT_BASE && id == COMM_EVENT_PAYMENT_OK) {
            const comm_payment_result_t *result = event_data;
            // Vending Machine procesa la confirmación de pago y despacha
            ESP_LOGI(TAG, "Pago confirmado (item %" PRIu32 "). Despachando producto...", result->item_id);
            vm_dispense(result->item_id);
        }
        else if (base == COMM_EVENT_BASE && id == COMM_EVENT_PAYMENT_FAILED) {
            const comm_payment_result_t *result = event_data;
            // Vending Machine procesa el fallo de pago y notifica al usuario
            ESP_LOGW(TAG, "Pago fallido (item %" PRIu32 ", motivo: %s). Notificando al usuario...",
                     result->item_id, result->reason);
            if (vm_state == VM_STATE_PAYING) {
                set_state(comm_is_connected() ? VM_STATE_IDLE : VM_STATE_OFFLINE);
            }
        }
    }

void vm_init(void) {

// 1. Inicializar la memoria Flash NVS (necesaria para el stack de red y Wi-Fi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. INICIALIZAR EL STACK DE RED (Esto soluciona el crash "Invalid mbox")
    ESP_ERROR_CHECK(esp_netif_init());

    // 3. Conectarse a internet. El driver Wi-Fi publica sus eventos en el bucle por defecto,
    //    distinto del bucle de la vending machine. SSID y password se configuran en menuconfig.
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());


    // 4. Configurar y crear el bucle de eventos dedicado
    esp_event_loop_args_t loop_args = {
        .queue_size = 20,              // Capacidad de eventos en cola
        .task_name = "vm_event_task",  // Nombre de la tarea que procesa eventos
        .task_priority = 5,            // Prioridad de FreeRTOS
        .task_stack_size = 4096,       // Tamaño de stack dedicado
        .task_core_id = tskNO_AFFINITY
    };

    ESP_ERROR_CHECK(esp_event_loop_create(&loop_args, &vm_event_loop));
    ESP_LOGI(TAG, "Bucle de eventos instanciado correctamente.");

    // 5. Suscribir el manejador principal de la Vending Machine
    ESP_ERROR_CHECK(esp_event_handler_register_with(
        vm_event_loop,
        ESP_EVENT_ANY_BASE,
        ESP_EVENT_ANY_ID,
        vm_event_handler,
        NULL
    ));


    // 6. Inicializar componentes pasándoles el handle del bucle de eventos
    comm_init(vm_event_loop);
    ui_init(vm_event_loop);

    // 7. Probar elementos mecánicos y ponerse en modo espera
    ESP_LOGI(TAG, "Probando elementos mecánicos...");
    // ...
    ESP_LOGI(TAG, "Esperando conexión con el servidor...");
}