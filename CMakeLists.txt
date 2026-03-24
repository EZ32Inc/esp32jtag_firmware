cmake_minimum_required(VERSION 3.5)

# set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

if(${IDF_VERSION_MAJOR} LESS 5)
    message(FATAL_ERROR "ESP-IDF v${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR} is not supported! "
        "Please use v5.0 or newer.")
endif()

project(esp32jtag)
idf_build_set_property(COMPILE_OPTIONS "-DPC_HOSTED=0" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-DENABLE_DEBUG=1" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wno-error=format" APPEND)
#   target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-const-variable -Wno-format -Wno-format-truncation -Wno-incompatible-pointer-types)
