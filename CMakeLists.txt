cmake_minimum_required(VERSION 3.16)

set(IDF_TARGET "esp32p4")

# TinyUSB fork (adjust path as needed)
set(TUSB_ROOT ${CMAKE_CURRENT_LIST_DIR}/../tinyusb)
set(TUSB_BSP ${TUSB_ROOT}/hw/bsp/espressif)

set(BOARD espressif_p4_function_ev)
include(${TUSB_BSP}/boards/${BOARD}/board.cmake)

# RHPort: port 0 = FS device (MIDI), port 1 = HS host (DJ Console)
set(RHPORT_DEVICE 0)
set(RHPORT_DEVICE_SPEED OPT_MODE_FULL_SPEED)
set(RHPORT_HOST 1)
set(RHPORT_HOST_SPEED OPT_MODE_HIGH_SPEED)

set(EXTRA_COMPONENT_DIRS
  "src"
  "${TUSB_BSP}/boards"
  "${TUSB_BSP}/components"
)

# Exclude the S3 main/ component (it stays on the master branch)
set(EXCLUDE_COMPONENTS "main")

# sdkconfig
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.esp32p4")
set(SDKCONFIG ${CMAKE_BINARY_DIR}/sdkconfig)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32p4_midi_bridge)
