#include "comm_transport.h"
#include <string.h>
#include <errno.h>
#include "esp_log.h"
#include "sdkconfig.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

static const char *TAG = "COMM_TRANSPORT";

#ifdef CONFIG_COMM_USE_TLS
    #include "esp_tls.h"
    static esp_tls_t *tls_conn = NULL;

    // Declaración de los símbolos del archivo server.crt incrustado mediante CMake
    extern const uint8_t server_crt_start[] asm("_binary_server_crt_start");
    extern const uint8_t server_crt_end[]   asm("_binary_server_crt_end");
#else
    static int tcp_sock = -1;
#endif

static void set_recv_timeout(int sock, uint32_t timeout_ms) {
    struct timeval tv = {
        .tv_sec = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000,
    };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

esp_err_t comm_transport_connect(uint32_t recv_timeout_ms) {
    comm_transport_close();

#ifdef CONFIG_COMM_USE_TLS
    // Validación del servidor mediante server.crt
    esp_tls_cfg_t cfg = {
        .cacert_buf = server_crt_start,
        .cacert_bytes = server_crt_end - server_crt_start,
        .skip_common_name = true, // Necesario en red local para ignorar si la IP no coincide con el CN del certificado
        .timeout_ms = CONFIG_COMM_RESPONSE_TIMEOUT_MS,
    };

    tls_conn = esp_tls_init();
    if (!tls_conn) {
        ESP_LOGE(TAG, "Fallo al inicializar esp_tls");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Iniciando Handshake TLS hacia %s:%d...", CONFIG_COMM_HOST, CONFIG_COMM_PORT);
    int res = esp_tls_conn_new_sync(CONFIG_COMM_HOST, strlen(CONFIG_COMM_HOST), CONFIG_COMM_PORT, &cfg, tls_conn);
    if (res != 1) {
        ESP_LOGE(TAG, "Fallo al conectar por TLS a %s:%d", CONFIG_COMM_HOST, CONFIG_COMM_PORT);
        comm_transport_close();
        return ESP_FAIL;
    }

    int sock = -1;
    esp_tls_get_conn_sockfd(tls_conn, &sock);
    set_recv_timeout(sock, recv_timeout_ms);

    ESP_LOGI(TAG, "Conexión TLS establecida con %s:%d", CONFIG_COMM_HOST, CONFIG_COMM_PORT);
    return ESP_OK;

#else
    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;
    char port_str[6];

    snprintf(port_str, sizeof(port_str), "%d", CONFIG_COMM_PORT);

    if (getaddrinfo(CONFIG_COMM_HOST, port_str, &hints, &res) != 0 || res == NULL) {
        ESP_LOGE(TAG, "DNS lookup falló para %s", CONFIG_COMM_HOST);
        return ESP_FAIL;
    }

    tcp_sock = socket(res->ai_family, res->ai_socktype, 0);
    if (tcp_sock < 0) {
        ESP_LOGE(TAG, "No se pudo crear el socket: errno %d", errno);
        freeaddrinfo(res);
        return ESP_FAIL;
    }

    // Timeout de envío para no bloquear indefinidamente si el servidor no lee
    struct timeval tv = { .tv_sec = CONFIG_COMM_RESPONSE_TIMEOUT_MS / 1000, .tv_usec = 0 };
    setsockopt(tcp_sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(tcp_sock, res->ai_addr, res->ai_addrlen) != 0) {
        ESP_LOGE(TAG, "Fallo al conectar TCP a %s:%d (errno %d)", CONFIG_COMM_HOST, CONFIG_COMM_PORT, errno);
        freeaddrinfo(res);
        comm_transport_close();
        return ESP_FAIL;
    }
    freeaddrinfo(res);

    set_recv_timeout(tcp_sock, recv_timeout_ms);

    ESP_LOGI(TAG, "Conectado por TCP plano a %s:%d", CONFIG_COMM_HOST, CONFIG_COMM_PORT);
    return ESP_OK;
#endif
}

void comm_transport_close(void) {
#ifdef CONFIG_COMM_USE_TLS
    if (tls_conn) {
        esp_tls_conn_destroy(tls_conn);
        tls_conn = NULL;
    }
#else
    if (tcp_sock >= 0) {
        shutdown(tcp_sock, SHUT_RDWR);
        close(tcp_sock);
        tcp_sock = -1;
    }
#endif
}

esp_err_t comm_transport_send(const uint8_t *buffer, size_t len) {
    size_t sent = 0;
    while (sent < len) {
#ifdef CONFIG_COMM_USE_TLS
        if (!tls_conn) return ESP_FAIL;
        int written = esp_tls_conn_write(tls_conn, buffer + sent, len - sent);
        if (written == ESP_TLS_ERR_SSL_WANT_READ || written == ESP_TLS_ERR_SSL_WANT_WRITE) continue;
#else
        if (tcp_sock < 0) return ESP_FAIL;
        int written = send(tcp_sock, buffer + sent, len - sent, 0);
#endif
        if (written <= 0) {
            ESP_LOGE(TAG, "Error enviando datos");
            return ESP_FAIL;
        }
        sent += written;
    }
    return ESP_OK;
}

int comm_transport_recv(uint8_t *buffer, size_t len) {
#ifdef CONFIG_COMM_USE_TLS
    if (!tls_conn) return -1;
    int read = esp_tls_conn_read(tls_conn, buffer, len);
    if (read > 0) return read;
    if (read == ESP_TLS_ERR_SSL_WANT_READ || read == ESP_TLS_ERR_SSL_WANT_WRITE) return 0;
    return -1;  // 0 = cierre ordenado, <0 = error
#else
    if (tcp_sock < 0) return -1;
    int read = recv(tcp_sock, buffer, len, 0);
    if (read > 0) return read;
    if (read < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;  // timeout
    return -1;  // 0 = cierre ordenado, <0 = error
#endif
}
