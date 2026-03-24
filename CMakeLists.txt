cmake_minimum_required(VERSION 3.5)

# set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

if(${IDF_VERSION_MAJOR} LESS 5)
    message(FATAL_ERROR "ESP-IDF v${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR} is not supported! "
        "Please use v5.0 or newer.")
endif()

# Firmware release version — update here for each release
set(PROJECT_VER "0.1.0")

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
