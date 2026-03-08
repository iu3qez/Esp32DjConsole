idf_component_register(SRCS "main.c" "usb_descriptors.c" "midi_output.c" "midi_input.c"
                            "usb_dj_host.c" "event_engine.c"
                            "config_store.c" "wifi_manager.c"
                            "http_server.c"
                    INCLUDE_DIRS "."
                    REQUIRES boards tinyusb_src
                    PRIV_REQUIRES usb nvs_flash esp_wifi esp_netif esp_event lwip
                                  esp_http_server json spiffs)
