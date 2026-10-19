// Capa de transporte privada de COMM (basada en el componente conn de ejemplo).
// Abstrae TCP plano o TLS segun CONFIG_COMM_USE_TLS.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

// Conecta a CONFIG_COMM_HOST:CONFIG_COMM_PORT.
// recv_timeout_ms: tiempo maximo que bloquea comm_transport_recv.
esp_err_t comm_transport_connect(uint32_t recv_timeout_ms);

void comm_transport_close(void);

// Envia todo el buffer. Devuelve ESP_OK o ESP_FAIL.
esp_err_t comm_transport_send(const uint8_t *buffer, size_t len);

// Devuelve: >0 bytes leidos, 0 si vencio el timeout sin datos, -1 si hubo error
// o el servidor cerro la conexion.
int comm_transport_recv(uint8_t *buffer, size_t len);
