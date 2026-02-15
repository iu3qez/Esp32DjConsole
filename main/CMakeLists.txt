idf_component_register(
    SRCS
        "main.c"
        "wifi_manager.c"
        "status_led.c"
        "usb_dj_host.c"
        "usb_debug.c"
        "cat_client.c"
        "config_store.c"
        "mapping_engine.c"
        "dj_led.c"
        "http_server.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        nvs_flash
        esp_wifi
        esp_netif
        esp_event
        mdns
        usb
        esp_http_server
        json
        led_strip
        spiffs
        esp_timer
)

# Auto-generate CAT command database from Thetis CATCommands.cs
find_package(Python3 REQUIRED)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/cmd_db_generated.inc
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_DIR}/scripts/extract_cat_commands.py
        ${PROJECT_DIR}/reference/CATCommands.cs
        --generate-c
        --overrides ${PROJECT_DIR}/reference/cmd_overrides.json
        -o ${CMAKE_CURRENT_SOURCE_DIR}/cmd_db_generated.inc
    DEPENDS ${PROJECT_DIR}/reference/CATCommands.cs
            ${PROJECT_DIR}/reference/cmd_overrides.json
            ${PROJECT_DIR}/scripts/extract_cat_commands.py
    COMMENT "Generating CAT command database from CATCommands.cs"
)
add_custom_target(generate_cmd_db DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cmd_db_generated.inc)
add_dependencies(${COMPONENT_LIB} generate_cmd_db)
