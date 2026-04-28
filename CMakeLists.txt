cmake_minimum_required(VERSION 3.21)
project(pinpoint-cpp-examples LANGUAGES C CXX)

if(POLICY CMP0077)
    cmake_policy(SET CMP0077 NEW)
endif()
if(POLICY CMP0126)
    cmake_policy(SET CMP0126 NEW)
endif()
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(FetchContent)
find_package(PkgConfig QUIET)

# ---------------------------------------------------------------------------
# pinpoint-cpp-agent (fetched from main, built shared-only with tests/examples
# disabled to keep the configure step lean)
# ---------------------------------------------------------------------------
set(BUILD_TESTING     OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES    OFF CACHE BOOL "" FORCE)
set(USE_CCACHE        OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS ON  CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    pinpoint_cpp
    GIT_REPOSITORY https://github.com/pinpoint-apm/pinpoint-cpp-agent.git
    GIT_TAG main
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(pinpoint_cpp)

# ---------------------------------------------------------------------------
# Shared header-only deps used by several examples
# ---------------------------------------------------------------------------
include(CppHttplib)

# ---------------------------------------------------------------------------
# Per-example toggles
# ---------------------------------------------------------------------------
option(BUILD_CIVETWEB_EXAMPLE "Build the civetweb example"  ON)
option(BUILD_CPPTRACE_EXAMPLE "Build the cpptrace example"  ON)
option(BUILD_CURL_EXAMPLE     "Build the libcurl example"   ON)
option(BUILD_GRPC_EXAMPLE     "Build the gRPC example"      ON)
option(BUILD_KAFKA_EXAMPLE    "Build the librdkafka example" ON)
option(BUILD_MONGODB_EXAMPLE  "Build the mongocxx example"  ON)
option(BUILD_MYSQL_EXAMPLE    "Build the MySQL example"     ON)
option(BUILD_REDIS_EXAMPLE    "Build the hiredis example"   ON)

if(BUILD_CIVETWEB_EXAMPLE)
    add_subdirectory(civetweb)
endif()
if(BUILD_CPPTRACE_EXAMPLE)
    add_subdirectory(cpptrace)
endif()
if(BUILD_CURL_EXAMPLE)
    add_subdirectory(curl)
endif()
if(BUILD_GRPC_EXAMPLE)
    add_subdirectory(grpc)
endif()
if(BUILD_KAFKA_EXAMPLE)
    add_subdirectory(kafka)
endif()
if(BUILD_MONGODB_EXAMPLE)
    add_subdirectory(mongodb)
endif()
if(BUILD_MYSQL_EXAMPLE)
    add_subdirectory(mysql)
endif()
if(BUILD_REDIS_EXAMPLE)
    add_subdirectory(redis)
endif()
