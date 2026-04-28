# librdkafka — both producer and consumer link against the C++ binding (rdkafka++)

set(_rdkafka_target "")

find_package(RdKafka QUIET CONFIG)
if(TARGET RdKafka::rdkafka++)
    set(_rdkafka_target RdKafka::rdkafka++)
endif()

if(NOT _rdkafka_target AND PkgConfig_FOUND)
    pkg_check_modules(RDKAFKACPP QUIET IMPORTED_TARGET rdkafka++)
    if(TARGET PkgConfig::RDKAFKACPP)
        set(_rdkafka_target PkgConfig::RDKAFKACPP)
    endif()
endif()

if(NOT _rdkafka_target)
    include(FetchContent)
    set(RDKAFKA_BUILD_STATIC   OFF CACHE BOOL "" FORCE)
    set(RDKAFKA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(RDKAFKA_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(WITH_SSL               OFF CACHE BOOL "" FORCE)
    set(WITH_SASL              OFF CACHE BOOL "" FORCE)
    set(WITH_ZLIB              OFF CACHE BOOL "" FORCE)
    set(WITH_ZSTD              OFF CACHE BOOL "" FORCE)
    set(WITH_CURL              OFF CACHE BOOL "" FORCE)
    set(ENABLE_LZ4_EXT         OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        librdkafka
        GIT_REPOSITORY https://github.com/confluentinc/librdkafka.git
        GIT_TAG v2.5.3
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(librdkafka)
    if(TARGET rdkafka++)
        set(_rdkafka_target rdkafka++)
    endif()
endif()

if(NOT _rdkafka_target)
    message(WARNING "librdkafka C++ binding not resolved; skipping kafka example")
    return()
endif()

add_executable(kafka_web_producer kafka_web_producer.cpp)
target_include_directories(kafka_web_producer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/cpptrace
)
target_link_libraries(kafka_web_producer PRIVATE
    pinpoint_cpp
    ${_rdkafka_target}
    httplib::httplib
)

add_executable(kafka_consumer kafka_consumer.cpp)
target_include_directories(kafka_consumer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(kafka_consumer PRIVATE
    pinpoint_cpp
    ${_rdkafka_target}
)
