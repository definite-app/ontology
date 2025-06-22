cmake_minimum_required(VERSION 3.5)

# Set extension name here
set(TARGET_NAME quack)

# DuckDB's extension distribution supports vcpkg. As such, dependencies can be added in ./vcpkg.json and then
# used in cmake with find_package. Feel free to remove or replace with other dependencies.
# Note that it should also be removed from vcpkg.json to prevent needlessly installing it..
find_package(OpenSSL REQUIRED)

# Try to find nlohmann_json, but make it optional for tidy-check and other environments
find_package(nlohmann_json CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
    # Fallback: try to find it as a system package
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(nlohmann_json QUIET nlohmann_json)
    endif()
    
    # If still not found, check for header-only installation
    if(NOT nlohmann_json_FOUND)
        find_path(NLOHMANN_JSON_INCLUDE_DIR 
            NAMES nlohmann/json.hpp
            PATHS /usr/include /usr/local/include
        )
        if(NLOHMANN_JSON_INCLUDE_DIR)
            set(nlohmann_json_FOUND TRUE)
            add_library(nlohmann_json_header_only INTERFACE)
            target_include_directories(nlohmann_json_header_only INTERFACE ${NLOHMANN_JSON_INCLUDE_DIR})
            add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json_header_only)
        endif()
    endif()
endif()

set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(${TARGET_NAME})
include_directories(src/include)

# Include DuckDB extension configuration
include(${CMAKE_CURRENT_SOURCE_DIR}/duckdb/extension/extension_config.cmake)

set(EXTENSION_SOURCES src/quack_extension.cpp)

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

# Link OpenSSL and nlohmann_json in both the static library as the loadable extension
target_link_libraries(${EXTENSION_NAME} OpenSSL::SSL OpenSSL::Crypto)
target_link_libraries(${LOADABLE_EXTENSION_NAME} OpenSSL::SSL OpenSSL::Crypto)

# Link nlohmann_json if found
if(nlohmann_json_FOUND)
    target_link_libraries(${EXTENSION_NAME} nlohmann_json::nlohmann_json)
    target_link_libraries(${LOADABLE_EXTENSION_NAME} nlohmann_json::nlohmann_json)
    target_compile_definitions(${EXTENSION_NAME} PRIVATE HAVE_NLOHMANN_JSON)
    target_compile_definitions(${LOADABLE_EXTENSION_NAME} PRIVATE HAVE_NLOHMANN_JSON)
else()
    message(WARNING "nlohmann_json not found. Semantic query functionality will be disabled.")
endif()

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
