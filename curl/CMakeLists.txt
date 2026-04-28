find_package(CURL QUIET)
if(NOT CURL_FOUND)
    include(FetchContent)
    set(BUILD_CURL_EXE        OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING         OFF CACHE BOOL "" FORCE)
    set(CURL_DISABLE_TESTS    ON  CACHE BOOL "" FORCE)
    set(CURL_USE_LIBPSL       OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        curl
        GIT_REPOSITORY https://github.com/curl/curl.git
        GIT_TAG curl-8_10_1
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(curl)
endif()

add_executable(curl_web_example curl_web_example.cpp)
# http_trace_context.h lives in the cpptrace example dir
target_include_directories(curl_web_example PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/cpptrace
)
target_link_libraries(curl_web_example PRIVATE
    pinpoint_cpp
    CURL::libcurl
    httplib::httplib
)
