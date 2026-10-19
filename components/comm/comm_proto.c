#include "comm_proto.h"
#include <stdio.h>
#include <string.h>
#include "cJSON.h"

// Serializa el objeto en buf agregando '\n' y lo libera.
static int finish(cJSON *root, char *buf, size_t len) {
    int written = -1;
    // Se reserva un byte para el '\n'
    if (root && cJSON_PrintPreallocated(root, buf, (int)len - 1, false)) {
        written = strlen(buf);
        buf[written++] = '\n';
        buf[written] = '\0';
    }
    cJSON_Delete(root);
    return written;
}

int comm_proto_build_hello(char *buf, size_t len, const char *device_id) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "hello");
    cJSON_AddStringToObject(root, "device_id", device_id);
    return finish(root, buf, len);
}

int comm_proto_build_ping(char *buf, size_t len) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "ping");
    return finish(root, buf, len);
}

int comm_proto_build_pay_req(char *buf, size_t len, uint32_t req_id, uint32_t item_id) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "pay_req");
    cJSON_AddNumberToObject(root, "req_id", req_id);
    cJSON_AddNumberToObject(root, "item_id", item_id);
    return finish(root, buf, len);
}

static void copy_reason(cJSON *root, comm_msg_t *out) {
    const cJSON *reason = cJSON_GetObjectItemCaseSensitive(root, "reason");
    if (cJSON_IsString(reason)) {
        strlcpy(out->reason, reason->valuestring, sizeof(out->reason));
    }
}

bool comm_proto_parse(const char *line, comm_msg_t *out) {
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_Parse(line);
    if (root == NULL) {
        return false;
    }

    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (cJSON_IsString(type)) {
        if (strcmp(type->valuestring, "hello_ack") == 0) {
            out->type = COMM_MSG_HELLO_ACK;
        } else if (strcmp(type->valuestring, "pong") == 0) {
            out->type = COMM_MSG_PONG;
        } else if (strcmp(type->valuestring, "pay_resp") == 0) {
            const cJSON *req_id = cJSON_GetObjectItemCaseSensitive(root, "req_id");
            const cJSON *status = cJSON_GetObjectItemCaseSensitive(root, "status");
            if (cJSON_IsNumber(req_id) && cJSON_IsString(status)) {
                out->type = COMM_MSG_PAY_RESP;
                out->req_id = (uint32_t)req_id->valuedouble;
                out->ok = strcmp(status->valuestring, "ok") == 0;
                copy_reason(root, out);
            }
        } else if (strcmp(type->valuestring, "error") == 0) {
            out->type = COMM_MSG_ERROR;
            copy_reason(root, out);
        }
    }

    cJSON_Delete(root);
    return true;
}
