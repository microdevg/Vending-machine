#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "vm_ui.h"

// ===================== CONFIGURACIÓN DE HARDWARE =====================
#define BTN_UP_GPIO         GPIO_NUM_32      // Botón ARRIBA (anterior)
#define BTN_DOWN_GPIO       GPIO_NUM_33      // Botón ABAJO (siguiente)
#define BTN_SELECT_GPIO     GPIO_NUM_25      // Botón SELECCIONAR

// Debounce
#define DEBOUNCE_MS         50

static const char *TAG = "VM_UI";

// Cola para los eventos de los GPIOs (Faltaba definirla)
static QueueHandle_t gpio_evt_queue = NULL;
static void IRAM_ATTR gpio_isr_handler(void *arg);

// Lista de productos

// Lista de 5 productos de ejemplo para la máquina expendedora
static const ui_product_t product_list[] = {
    { .description = "Coca Cola 500ml", .price = 1200.50f },
    { .description = "Agua Mineral 500ml", .price = 900.00f },
    { .description = "Alfajor Triple", .price = 850.75f },
    { .description = "Papas Fritas 90g", .price = 1500.00f },
    { .description = "Chocolate con Leche", .price = 1100.25f }
};
// Calcula el tamaño automáticamente de forma estática (5 productos)
static size_t product_count = 5;
static int current_index = 0;

// Definición de funciones estáticas previas
static void ui_update_display(void);
static void ui_init_hardware(void);
void  gpio_isr_handler(void *arg);
static void ui_button_task(void *arg);

// DEFINICIÓN OBLIGATORIA: Asigna la memoria del símbolo que busca el linker
ESP_EVENT_DEFINE_BASE(UI_EVENT_BASE);

// Guardamos el handle para poder enviar eventos más adelante
static esp_event_loop_handle_t ui_loop_handle = NULL;

void ui_set_products(const ui_product_t *products, size_t count)
{

    current_index = 0;
    ui_update_display();
}
void ui_init(esp_event_loop_handle_t loop_handler) {
    ui_loop_handle = loop_handler;
    
    // Inicializar el hardware y la tarea de botones
    ui_init_hardware();
    
    // Muestra el primer producto por consola/pantalla al arrancar
    ui_update_display();

    xTaskCreate(ui_button_task, "ui_button_task", 2048, NULL, 10, NULL);

    ESP_LOGI(TAG, "Interfaz de usuario inicializada.");
}
// ===================== HARDWARE =====================

static void ui_init_hardware(void)
{
    // ---------- Configurar botones ----------
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE,            // Flanco de bajada (activo en bajo)
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << BTN_UP_GPIO) |
                        (1ULL << BTN_DOWN_GPIO) |
                        (1ULL << BTN_SELECT_GPIO),
        .pull_up_en = GPIO_PULLUP_ENABLE,           // Pull-up interno
        .pull_down_en = GPIO_PULLDOWN_DISABLE
    };
    gpio_config(&io_conf);

    // Crear cola para eventos de GPIO
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));

    // Instalar servicio de interrupciones
    gpio_install_isr_service(0);

    // Registrar handlers
    gpio_isr_handler_add(BTN_UP_GPIO, gpio_isr_handler, (void *)BTN_UP_GPIO);
    gpio_isr_handler_add(BTN_DOWN_GPIO, gpio_isr_handler, (void *)BTN_DOWN_GPIO);
    gpio_isr_handler_add(BTN_SELECT_GPIO, gpio_isr_handler, (void *)BTN_SELECT_GPIO);

    ESP_LOGI(TAG, "Botones configurados (UP=%d, DOWN=%d, SELECT=%d)",
             BTN_UP_GPIO, BTN_DOWN_GPIO, BTN_SELECT_GPIO);
}


// ISR: solo manda el número de pin a la cola
static void  gpio_isr_handler(void *arg)
 {
     uint32_t gpio_num = (uint32_t)arg;
     xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
 }

// Tarea que procesa los botones (con debounce)
static void ui_button_task(void *arg)
{
    uint32_t io_num;
    TickType_t last_press[3] = {0};

    while (1) {
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
            TickType_t now = xTaskGetTickCount();

            int idx = -1;
            if (io_num == BTN_UP_GPIO) idx = 0;
            else if (io_num == BTN_DOWN_GPIO) idx = 1;
            else if (io_num == BTN_SELECT_GPIO) idx = 2;

            // Debounce simple
            if (idx >= 0 && (now - last_press[idx]) > pdMS_TO_TICKS(DEBOUNCE_MS)) {
                last_press[idx] = now;

                if (ui_loop_handle == NULL) continue;

                switch (io_num) {
                    case BTN_UP_GPIO:
                        ESP_LOGI(TAG, "Botón ARRIBA → PREV");
                        if (product_count > 0) {
                            current_index = (current_index - 1 + product_count) % product_count;
                            ui_update_display();
                        }
                    //    esp_event_post_to(ui_loop_handle, UI_EVENT_BASE, UI_EVENT_PREV,
                     //                    NULL, 0, portMAX_DELAY);
                        break;

                    case BTN_DOWN_GPIO:
                        ESP_LOGI(TAG, "Botón ABAJO → NEXT");
                        if (product_count > 0) {
                            current_index = (current_index + 1) % product_count;
                            ui_update_display();
                        }
                   //     esp_event_post_to(ui_loop_handle, UI_EVENT_BASE, UI_EVENT_NEXT,
                   //                      NULL, 0, portMAX_DELAY);
                        break;

                    case BTN_SELECT_GPIO:
                        ESP_LOGI(TAG, "Botón SELECT");
                        if (product_count > 0) {
                            uint32_t id = (uint32_t)current_index;
                            ui_select_product(id);
                        }
                        break;
                }
            }
        }
    }
}

/**
 * @brief Muestra un producto en la consola (o pantalla)
 */
void ui_show(const char *description, float price)
{
    if (description == NULL) {
        description = "Sin nombre";
    }

    printf("\n");
    printf("==============================\n");
    printf("  %s\n", description);
    printf("  Precio: $%.2f\n", price);
    printf("==============================\n\n");
}

// Actualiza la visualización según el producto actual
static void ui_update_display(void)
{


    const ui_product_t *p = &product_list[current_index];
    ui_show(p->description, p->price);
}

void ui_deinit(void) {
    ui_loop_handle = NULL;
    ESP_LOGI(TAG, "Interfaz de usuario apagada.");
}

// Función llamada al seleccionar un producto
void ui_select_product(uint32_t product_id) {
    if (ui_loop_handle == NULL) return;

    ESP_LOGI(TAG, "Producto %d seleccionado por el usuario. Publicando evento...", product_id);

    esp_event_post_to(
        ui_loop_handle,
        UI_EVENT_BASE,
        UI_EVENT_PRODUCT_SELECTED,
        &product_id,
        sizeof(product_id),
        portMAX_DELAY
    );
}