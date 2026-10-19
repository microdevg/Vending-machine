// Protocolo privado de COMM: un objeto JSON por linea terminado en '\n'.
// Debe coincidir con server/app/protocol.py
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define COMM_PROTO_REASON_LEN 24

typedef enum {
    COMM_MSG_UNKNOWN = 0,
    COMM_MSG_HELLO_ACK,
    COMM_MSG_PONG,
    COMM_MSG_PAY_RESP,
    COMM_MSG_ERROR,
} comm_msg_type_t;

typedef struct {
    comm_msg_type_t type;
    uint32_t req_id;                       // solo PAY_RESP
    bool ok;                               // solo PAY_RESP: status == "ok"
    char reason[COMM_PROTO_REASON_LEN];    // PAY_RESP rechazado o ERROR
} comm_msg_t;

// Construyen el mensaje (incluido '\n') en buf. Devuelven la longitud o -1.
int comm_proto_build_hello(char *buf, size_t len, const char *device_id);
int comm_proto_build_ping(char *buf, size_t len);
int comm_proto_build_pay_req(char *buf, size_t len, uint32_t req_id, uint32_t item_id);

// Parsea una linea (sin '\n'). Devuelve false si no es JSON valido.
bool comm_proto_parse(const char *line, comm_msg_t *out);
