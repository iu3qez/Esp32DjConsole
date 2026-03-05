idf_component_register(SRCS "main.c" "usb_descriptors.c" "midi_output.c" "usb_dj_host.c" "event_engine.c"
                    INCLUDE_DIRS "."
                    REQUIRES boards tinyusb_src
                    PRIV_REQUIRES usb nvs_flash)
