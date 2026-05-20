cmake_minimum_required(VERSION 3.20)
project(wemo_matter_bridge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(WEMO_BRIDGE_USE_OPENWEMO_CORE "Enable openwemo-bridge-core integration" OFF)
set(OPENWEMO_BRIDGE_CORE_ROOT "${CMAKE_SOURCE_DIR}/../openwemo-bridge-core" CACHE PATH "Path to openwemo-bridge-core checkout")

add_executable(wemo-bridge-app
    src/main.cpp
    src/matter/endpoint_registry.cpp
    src/adapters/wemo/wemo_adapter_stub.cpp
    src/adapters/wemo/wemo_adapter_openwemo.cpp
)

target_include_directories(wemo-bridge-app
    PRIVATE
        include
)

find_package(SQLite3 REQUIRED)
target_link_libraries(wemo-bridge-app PRIVATE SQLite::SQLite3)

if(WEMO_BRIDGE_USE_OPENWEMO_CORE)
    set(OPENWEMO_ENGINE_INCLUDE "${OPENWEMO_BRIDGE_CORE_ROOT}/wemo_engine")
    find_library(OPENWEMO_ENGINE_LIB
        NAMES wemoengine libwemoengine
        PATHS "${OPENWEMO_BRIDGE_CORE_ROOT}/wemo_engine"
        NO_DEFAULT_PATH
    )
    find_library(IXML_LIB NAMES ixml libixml PATHS /usr/local/lib /usr/lib /usr/lib/x86_64-linux-gnu)
    find_library(UPNP_LIB NAMES upnp libupnp PATHS /usr/local/lib /usr/lib /usr/lib/x86_64-linux-gnu)

    if(NOT EXISTS "${OPENWEMO_ENGINE_INCLUDE}/wemo_engine.h")
        message(FATAL_ERROR "wemo_engine.h not found at ${OPENWEMO_ENGINE_INCLUDE}")
    endif()
    if(NOT OPENWEMO_ENGINE_LIB)
        message(FATAL_ERROR "libwemoengine not found under ${OPENWEMO_BRIDGE_CORE_ROOT}/wemo_engine")
    endif()
    if(NOT IXML_LIB)
        message(FATAL_ERROR "libixml not found (required by libwemoengine)")
    endif()
    if(NOT UPNP_LIB)
        message(FATAL_ERROR "libupnp not found (required by libwemoengine)")
    endif()

    target_compile_definitions(wemo-bridge-app PRIVATE HAVE_OPENWEMO_ENGINE=1)
    target_include_directories(wemo-bridge-app PRIVATE "${OPENWEMO_ENGINE_INCLUDE}")
    target_link_libraries(wemo-bridge-app PRIVATE "${OPENWEMO_ENGINE_LIB}" "${IXML_LIB}" "${UPNP_LIB}" pthread)
else()
    target_compile_definitions(wemo-bridge-app PRIVATE HAVE_OPENWEMO_ENGINE=0)
endif()

# Integration points:
# - linked CHIP targets from third_party/connectedhomeip
# - linked openwemo-bridge-core targets
# Keep this file small and explicit; add linkage when dependency wiring is finalized.
