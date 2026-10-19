set(embed_files "")
if(CONFIG_COMM_USE_TLS)
    # Certificado del servidor para validar la conexión TLS (ver readme)
    set(embed_files "certs/server.crt")
endif()

idf_component_register(SRCS "comm.c" "comm_proto.c" "comm_transport.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_event
                    PRIV_REQUIRES esp-tls esp_timer lwip
                    EMBED_TXTFILES ${embed_files})
