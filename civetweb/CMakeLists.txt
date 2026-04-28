# civetweb (C library). The example links Pinpoint's C wrapper which is
# compiled into the same `pinpoint_cpp` shared library.

set(_civetweb_target "")

if(NOT TARGET civetweb::civetweb-c-library AND NOT TARGET civetweb::civetweb)
    find_package(civetweb QUIET CONFIG)
endif()

if(TARGET civetweb::civetweb-c-library)
    set(_civetweb_target civetweb::civetweb-c-library)
elseif(TARGET civetweb::civetweb)
    set(_civetweb_target civetweb::civetweb)
else()
    include(FetchContent)
    set(CIVETWEB_BUILD_TESTING            OFF CACHE BOOL "" FORCE)
    set(CIVETWEB_ENABLE_SERVER_EXECUTABLE OFF CACHE BOOL "" FORCE)
    set(CIVETWEB_ENABLE_CXX               OFF CACHE BOOL "" FORCE)
    set(CIVETWEB_ENABLE_SSL               OFF CACHE BOOL "" FORCE)
    set(CIVETWEB_INSTALL_EXECUTABLE       OFF CACHE BOOL "" FORCE)
    set(CIVETWEB_BUILD_SHARED_LIBRARY     ON  CACHE BOOL "" FORCE)
    FetchContent_Declare(
        civetweb
        GIT_REPOSITORY https://github.com/civetweb/civetweb.git
        GIT_TAG v1.16
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(civetweb)
    if(TARGET civetweb-c-library)
        set(_civetweb_target civetweb-c-library)
    elseif(TARGET civetweb)
        set(_civetweb_target civetweb)
    endif()
endif()

if(NOT _civetweb_target)
    message(WARNING "civetweb library target not resolved; skipping civetweb example")
    return()
endif()

add_executable(civetweb_example civetweb_example.c)
target_link_libraries(civetweb_example PRIVATE
    pinpoint_cpp
    ${_civetweb_target}
)
