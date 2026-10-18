#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_event.h"
#include "lwip/sockets.h"
#include "comm.h"

// --- CONFIGURACIÓN CONEXIÓN SOCKET ---
#define SERVER_HOST         "192.168.1.100"  // IP o Hostname del servidor
#define SERVER_PORT         8080             // Puerto del servidor
#define SOCKET_TIMEOUT_SEC  5                // Tiempo de espera para enviar/recibir

static const char *TAG = "COMM";

ESP_EVENT_DEFINE_BASE(COMM_EVENT_BASE);

static esp_event_loop_handle_t comm_loop_handle = NULL;
static QueueHandle_t payment_queue = NULL;

// Tarea en segundo plano para manejar los sockets de forma sincrónica
static void comm_task(void *pvParameters) {
    uint32_t item_id;

    while (1) {
        // Bloquea hasta que comm_start_payment_by_item reciba una petición
        if (xQueueReceive(payment_queue, &item_id, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "Iniciando transacción socket para Item ID: %lu", item_id);

            // 1. Configurar dirección del servidor
            struct sockaddr_in dest_addr;
            dest_addr.sin_addr.s_addr = inet_addr(SERVER_HOST);
            dest_addr.sin_family = AF_INET;
            dest_addr.sin_port = htons(SERVER_PORT);

            // 2. Crear socket TCP
            int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
            if (sock < 0) {
                ESP_LOGE(TAG, "No se pudo crear el socket: errno %d", errno);
                esp_event_post_to(comm_loop_handle, COMM_EVENT_BASE, COMM_EVENT_PAYMENT_FAILED, &item_id, sizeof(item_id), portMAX_DELAY);
                continue;
            }

            // Configurar timeout para evitar que el sistema se quede congelado
            struct timeval tv = { .tv_sec = SOCKET_TIMEOUT_SEC, .tv_usec = 0 };
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

            // 3. Conectar al servidor
            if (connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) != 0) {
                ESP_LOGE(TAG, "Error al conectar con %s:%d (errno %d)", SERVER_HOST, SERVER_PORT, errno);
                close(sock);
                esp_event_post_to(comm_loop_handle, COMM_EVENT_BASE, COMM_EVENT_PAYMENT_FAILED, &item_id, sizeof(item_id), portMAX_DELAY);
                continue;
            }

            // 4. Enviar petición al servidor
            char tx_buffer[64];
            int len = snprintf(tx_buffer, sizeof(tx_buffer), "PAY_REQ:%lu\n", item_id);

            if (send(sock, tx_buffer, len, 0) < 0) {
                ESP_LOGE(TAG, "Error enviando datos por socket");
                close(sock);
                esp_event_post_to(comm_loop_handle, COMM_EVENT_BASE, COMM_EVENT_PAYMENT_FAILED, &item_id, sizeof(item_id), portMAX_DELAY);
                continue;
            }

            // 5. Esperar la respuesta del servidor
            char rx_buffer[128];
            int bytes_received = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);

            if (bytes_received > 0) {
                rx_buffer[bytes_received] = '\0'; // Asegurar terminación del string
                ESP_LOGI(TAG, "Respuesta del servidor: %s", rx_buffer);

                // Evaluar si la respuesta contiene "OK"
                if (strstr(rx_buffer, "OK") != NULL) {
                    ESP_LOGI(TAG, "Pago APROBADO.");
                    esp_event_post_to(comm_loop_handle, COMM_EVENT_BASE, COMM_EVENT_PAYMENT_OK, &item_id, sizeof(item_id), portMAX_DELAY);
                } else {
                    ESP_LOGW(TAG, "Pago RECHAZADO por el servidor.");
                    esp_event_post_to(comm_loop_handle, COMM_EVENT_BASE, COMM_EVENT_PAYMENT_FAILED, &item_id, sizeof(item_id), portMAX_DELAY);
                }
            } else {
                ESP_LOGE(TAG, "Error o timeout al esperar la respuesta.");
                esp_event_post_to(comm_loop_handle, COMM_EVENT_BASE, COMM_EVENT_PAYMENT_FAILED, &item_id, sizeof(item_id), portMAX_DELAY);
            }

            // 6. Cerrar la conexión
            close(sock);
        }
    }
}

void comm_init(esp_event_loop_handle_t loop_handle) {
    comm_loop_handle = loop_handle;

    // Crear cola para 1 sola petición a la vez
    payment_queue = xQueueCreate(1, sizeof(uint32_t));

    // Crear la tarea en segundo plano (Stack: 4096 bytes, Prioridad: 5)
    xTaskCreate(comm_task, "comm_task", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "Módulo de comunicaciones inicializado.");
}

void comm_start_payment_by_item(uint32_t item_id) {
    if (payment_queue == NULL) {
        ESP_LOGE(TAG, "El módulo COMM no está inicializado.");
        return;
    }

    // Intenta enviar el item_id a la cola.
    // Si la cola está llena (ya hay un pago en curso), no espera (0) y rechaza.
    if (xQueueSend(payment_queue, &item_id, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Ocupado. Ya hay una transacción en curso. Se ignora item: %lu", item_id);
    } else {
        ESP_LOGI(TAG, "Petición para item %lu agregada a la cola.", item_id);
    }
}