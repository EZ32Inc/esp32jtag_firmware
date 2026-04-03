cmake_minimum_required(VERSION 3.5)

# set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

if(${IDF_VERSION_MAJOR} LESS 5)
    message(FATAL_ERROR "ESP-IDF v${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR} is not supported! "
        "Please use v5.0 or newer.")
endif()

# Firmware release version — update here for each release
set(PROJECT_VER "0.2.0")

# Capture git short commits at build time (baked into firmware image)
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE MAIN_GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT MAIN_GIT_COMMIT)
    set(MAIN_GIT_COMMIT "unknown")
endif()

project(esp32jtag_firmware)
idf_build_set_property(COMPILE_OPTIONS "-DPC_HOSTED=0" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-DENABLE_DEBUG=1" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wno-error=format" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-DMAIN_GIT_COMMIT=\"${MAIN_GIT_COMMIT}\"" APPEND)
#   target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-const-variable -Wno-format -Wno-format-truncation -Wno-incompatible-pointer-types)

# ---------------------------------------------------------------------------
# Post-build: generate versioned release binaries
#
#   esp32jtag_v<VER>_<DATE>_<GIT>_ota.bin   — app partition only (OTA upload)
#   esp32jtag_v<VER>_<DATE>_<GIT>_full.bin  — merged full flash  (esptool)
# ---------------------------------------------------------------------------
string(TIMESTAMP BUILD_TIMESTAMP "%Y%m%d_%H%M%S" UTC)
set(RELEASE_PREFIX "esp32jtag_v${PROJECT_VER}_${BUILD_TIMESTAMP}_${MAIN_GIT_COMMIT}")

idf_build_get_property(PYTHON python)

add_custom_command(TARGET app POST_BUILD
    # OTA binary — copy of app partition bin with versioned name
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_BINARY_DIR}/esp32jtag_firmware.bin"
        "${CMAKE_BINARY_DIR}/${RELEASE_PREFIX}_ota.bin"
    # Full merged binary — bootloader + partition table + ota_data + app
    COMMAND ${PYTHON} -m esptool --chip esp32s3 merge_bin
        --flash_mode dio --flash_freq 80m --flash_size 16MB
        -o "${CMAKE_BINARY_DIR}/${RELEASE_PREFIX}_full.bin"
        0x0    "${CMAKE_BINARY_DIR}/bootloader/bootloader.bin"
        0x8000 "${CMAKE_BINARY_DIR}/partition_table/partition-table.bin"
        0xf000 "${CMAKE_BINARY_DIR}/ota_data_initial.bin"
        0x20000 "${CMAKE_BINARY_DIR}/esp32jtag_firmware.bin"
    COMMENT "Release binaries → ${RELEASE_PREFIX}_{ota,full}.bin"
    VERBATIM
)
