# ESP-IDF top-level project CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_dj_console)

# Auto-build frontend with npm
set(FRONTEND_DIR ${CMAKE_CURRENT_SOURCE_DIR}/frontend)
set(FRONTEND_BUILD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/build/www)

# Custom target to build frontend
add_custom_target(frontend_build
    COMMAND ${CMAKE_COMMAND} -E echo "Building frontend..."
    COMMAND npm install --prefix ${FRONTEND_DIR}
    COMMAND npm run build --prefix ${FRONTEND_DIR}
    WORKING_DIRECTORY ${FRONTEND_DIR}
    COMMENT "Building Svelte frontend"
)

# Generate SPIFFS image from frontend build output for the 'www' partition.
spiffs_create_partition_image(www ${FRONTEND_BUILD_DIR} FLASH_IN_PROJECT DEPENDS frontend_build)
