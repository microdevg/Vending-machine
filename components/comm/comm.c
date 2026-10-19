#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "comm.h"
#include "comm_proto.h"
#include "comm_transport.h"

// Cada cuánto se revisa la cola de pagos y los timeouts mientras se espera recibir
#define POLL_INTERVAL_MS   100
#define LINE_BUFFER_SIZE   512
#define TX_BUFFER_SIZE     128

static const char *TAG = "COMM";

ESP_EVENT_DEFINE_BASE(COMM_EVENT_BASE);

static esp_event_loop_handle_t comm_loop_handle = NULL;
static QueueHandle_t payment_queue = NULL;
static volatile bool s_connected = false;

// Estado de una conexión con el servidor
typedef struct {
    bool ready;                  // hello_ack recibido
    int64_t hello_deadline;
    int64_t last_tx;
    int64_t pong_deadline;       // 0 = no hay ping pendiente

    bool pay_pending;
    uint32_t pay_req_id;
    uint32_t pay_item_id;
    int64_t pay_deadline;

    char line[LINE_BUFFER_SIZE];
    size_t line_len;
} session_t;

static uint32_t s_next_req_id = 1;

static int64_t now_ms(void) {
    return esp_timer_get_time() / 1000;
}

static void post_payment_result(bool ok, uint32_t req_id, uint32_t item_id, const char *reason) {
    comm_payment_result_t result = { .item_id = item_id, .req_id = req_id };
    if (reason) {
        strlcpy(result.reason, reason, sizeof(result.reason));
    }
    esp_event_post_to(comm_loop_handle, COMM_EVENT_BASE,
                      ok ? COMM_EVENT_PAYMENT_OK : COMM_EVENT_PAYMENT_FAILED,
                      &result, sizeof(result), portMAX_DELAY);
}

static esp_err_t session_send(session_t *s, const char *msg, int len) {
    if (len < 0) {
        ESP_LOGE(TAG, "No se pudo construir el mensaje");
        return ESP_FAIL;
    }
    ESP_LOGD(TAG, "-> %.*s", len - 1, msg);
    esp_err_t err = comm_transport_send((const uint8_t *)msg, len);
    if (err == ESP_OK) {
        s->last_tx = now_ms();
    }
    return err;
}

// Procesa un mensaje completo recibido del servidor
static void handle_line(session_t *s, const char *line) {
    ESP_LOGD(TAG, "<- %s", line);

    comm_msg_t msg;
    if (!comm_proto_parse(line, &msg)) {
        ESP_LOGW(TAG, "Mensaje no es JSON válido: %s", line);
        return;
    }

    switch (msg.type) {
    case COMM_MSG_HELLO_ACK:
        if (!s->ready) {
            s->ready = true;
            s_connected = true;
            ESP_LOGI(TAG, "Sesión establecida con el servidor.");
            esp_event_post_to(comm_loop_handle, COMM_EVENT_BASE, COMM_EVENT_CONNECTED, NULL, 0, portMAX_DELAY);
        }
        break;

    case COMM_MSG_PONG:
        s->pong_deadline = 0;
        break;

    case COMM_MSG_PAY_RESP:
        if (!s->pay_pending || msg.req_id != s->pay_req_id) {
            // Respuesta tardía de un pago que ya se dio por fallido
            ESP_LOGW(TAG, "pay_resp ignorado (req_id %" PRIu32 " no está pendiente)", msg.req_id);
            break;
        }
        s->pay_pending = false;
        if (msg.ok) {
            ESP_LOGI(TAG, "Pago APROBADO (req %" PRIu32 ", item %" PRIu32 ")", s->pay_req_id, s->pay_item_id);
        } else {
            ESP_LOGW(TAG, "Pago RECHAZADO (req %" PRIu32 ", item %" PRIu32 "): %s", s->pay_req_id, s->pay_item_id, msg.reason);
        }
        post_payment_result(msg.ok, s->pay_req_id, s->pay_item_id, msg.ok ? NULL : msg.reason);
        break;

    case COMM_MSG_ERROR:
        ESP_LOGW(TAG, "El servidor reportó un error: %s", msg.reason);
        break;

    default:
        ESP_LOGW(TAG, "Mensaje desconocido: %s", line);
        break;
    }
}

// Acumula bytes recibidos y despacha cada línea completa.
// TCP es un stream: un recv puede traer medio mensaje o varios juntos.
static void feed_bytes(session_t *s, const char *data, int len) {
    for (int i = 0; i < len; i++) {
        char c = data[i];
        if (c == '\n') {
            s->line[s->line_len] = '\0';
            if (s->line_len > 0) {
                handle_line(s, s->line);
            }
            s->line_len = 0;
        } else if (s->line_len < sizeof(s->line) - 1) {
            s->line[s->line_len++] = c;
        } else {
            ESP_LOGW(TAG, "Línea recibida demasiado larga, se descarta");
            s->line_len = 0;
        }
    }
}

// Mantiene una sesión hasta que se pierde la conexión
static void run_session(void) {
    session_t s = { 0 };
    char tx[TX_BUFFER_SIZE];
    char rx[128];

    s.hello_deadline = now_ms() + CONFIG_COMM_RESPONSE_TIMEOUT_MS;
    if (session_send(&s, tx, comm_proto_build_hello(tx, sizeof(tx), CONFIG_COMM_DEVICE_ID)) != ESP_OK) {
        return;
    }

    while (1) {
        int n = comm_transport_recv((uint8_t *)rx, sizeof(rx));
        if (n < 0) {
            ESP_LOGW(TAG, "El servidor cerró la conexión o hubo un error de lectura.");
            break;
        }
        feed_bytes(&s, rx, n);

        int64_t now = now_ms();

        if (!s.ready) {
            if (now > s.hello_deadline) {
                ESP_LOGE(TAG, "El servidor no respondió al hello.");
                break;
            }
            continue;
        }

        // Nuevo pedido de pago (de a uno por vez)
        uint32_t item_id;
        if (!s.pay_pending && xQueueReceive(payment_queue, &item_id, 0) == pdTRUE) {
            uint32_t req_id = s_next_req_id++;
            ESP_LOGI(TAG, "Enviando pedido de pago req %" PRIu32 " item %" PRIu32, req_id, item_id);
            if (session_send(&s, tx, comm_proto_build_pay_req(tx, sizeof(tx), req_id, item_id)) != ESP_OK) {
                post_payment_result(false, req_id, item_id, "disconnected");
                break;
            }
            s.pay_pending = true;
            s.pay_req_id = req_id;
            s.pay_item_id = item_id;
            s.pay_deadline = now + CONFIG_COMM_RESPONSE_TIMEOUT_MS;
        }

        if (s.pay_pending && now > s.pay_deadline) {
            ESP_LOGE(TAG, "Timeout esperando respuesta de pago req %" PRIu32, s.pay_req_id);
            s.pay_pending = false;
            post_payment_result(false, s.pay_req_id, s.pay_item_id, "timeout");
        }

        // Heartbeat: si no hubo tráfico saliente en un tiempo, enviar ping
        if (s.pong_deadline == 0 && now - s.last_tx >= CONFIG_COMM_PING_INTERVAL_MS) {
            if (session_send(&s, tx, comm_proto_build_ping(tx, sizeof(tx))) != ESP_OK) {
                break;
            }
            s.pong_deadline = now + CONFIG_COMM_RESPONSE_TIMEOUT_MS;
        }
        if (s.pong_deadline != 0 && now > s.pong_deadline) {
            ESP_LOGE(TAG, "El servidor no respondió al ping.");
            break;
        }
    }

    if (s.pay_pending) {
        post_payment_result(false, s.pay_req_id, s.pay_item_id, "disconnected");
    }
}

// Tarea en segundo plano: conecta, mantiene la sesión y reconecta si se cae
static void comm_task(void *pvParameters) {
    while (1) {
        ESP_LOGI(TAG, "Conectando con el servidor %s:%d...", CONFIG_COMM_HOST, CONFIG_COMM_PORT);
        if (comm_transport_connect(POLL_INTERVAL_MS) == ESP_OK) {
            run_session();
            comm_transport_close();
        }

        if (s_connected) {
            s_connected = false;
            esp_event_post_to(comm_loop_handle, COMM_EVENT_BASE, COMM_EVENT_DISCONNECTED, NULL, 0, portMAX_DELAY);
        }

        // Pedidos que quedaron en la cola durante la desconexión
        uint32_t item_id;
        while (xQueueReceive(payment_queue, &item_id, 0) == pdTRUE) {
            post_payment_result(false, 0, item_id, "disconnected");
        }

        vTaskDelay(pdMS_TO_TICKS(CONFIG_COMM_RECONNECT_DELAY_MS));
    }
}

void comm_init(esp_event_loop_handle_t loop_handle) {
    comm_loop_handle = loop_handle;

    // Cola para 1 sola petición a la vez
    payment_queue = xQueueCreate(1, sizeof(uint32_t));

    // Crear la tarea en segundo plano (Stack: 4096 bytes, Prioridad: 5)
    xTaskCreate(comm_task, "comm_task", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "Módulo de comunicaciones inicializado.");
}

bool comm_is_connected(void) {
    return s_connected;
}

esp_err_t comm_start_payment_by_item(uint32_t item_id) {
    if (payment_queue == NULL) {
        ESP_LOGE(TAG, "El módulo COMM no está inicializado.");
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_connected) {
        ESP_LOGW(TAG, "Sin conexión con el servidor. Se ignora item: %" PRIu32, item_id);
        return ESP_ERR_INVALID_STATE;
    }

    // Si la cola está llena (ya hay un pago en curso), no espera (0) y rechaza.
    if (xQueueSend(payment_queue, &item_id, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Ocupado. Ya hay una transacción en curso. Se ignora item: %" PRIu32, item_id);
        return ESP_ERR_TIMEOUT;
    }
    ESP_LOGI(TAG, "Petición para item %" PRIu32 " agregada a la cola.", item_id);
    return ESP_OK;
}
