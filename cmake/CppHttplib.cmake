# cpp-httplib — header-only HTTP server/client used by several examples.
# Try the system package first, fall back to FetchContent.
if(NOT TARGET httplib::httplib)
    find_package(httplib QUIET CONFIG)
    if(NOT httplib_FOUND)
        include(FetchContent)
        set(HTTPLIB_COMPILE OFF CACHE BOOL "" FORCE)
        set(HTTPLIB_REQUIRE_OPENSSL OFF CACHE BOOL "" FORCE)
        set(HTTPLIB_REQUIRE_ZLIB OFF CACHE BOOL "" FORCE)
        set(HTTPLIB_REQUIRE_BROTLI OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(
            cpp_httplib
            GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
            GIT_TAG v0.23.1
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(cpp_httplib)
    endif()
endif()

# cpp-httplib >= 0.21 calls CFHost APIs on macOS for async DNS but doesn't add
# the required Apple frameworks to its public link interface — patch the
# underlying (non-alias) target. find_package may also expose only the
# imported alias; in that case fall back to a plain interface library.
if(APPLE)
    if(TARGET httplib)
        target_link_libraries(httplib INTERFACE
            "-framework CFNetwork"
            "-framework CoreFoundation"
        )
    elseif(TARGET httplib::httplib AND NOT TARGET httplib_apple_frameworks)
        add_library(httplib_apple_frameworks INTERFACE)
        target_link_libraries(httplib_apple_frameworks INTERFACE
            "-framework CFNetwork"
            "-framework CoreFoundation"
        )
        target_link_libraries(httplib::httplib INTERFACE httplib_apple_frameworks)
    endif()
endif()
