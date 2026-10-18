idf_component_register(SRCS "vending_machine.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_event nvs_flash esp_netif vm_ui comm )
