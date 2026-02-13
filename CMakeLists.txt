# ESP-IDF top-level project CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_dj_console)

# Generate SPIFFS image from frontend build output for the 'www' partition.
# Run 'npm run build' in frontend/ first, then 'idf.py build' picks it up.
spiffs_create_partition_image(www ${CMAKE_CURRENT_SOURCE_DIR}/build/www FLASH_IN_PROJECT)
