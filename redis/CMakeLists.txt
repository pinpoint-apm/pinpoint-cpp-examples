# hiredis. The example does `#include <hiredis.h>` (not <hiredis/hiredis.h>),
# so the include path must point directly at the hiredis header dir.

set(_hiredis_target "")
set(_hiredis_includes "")

find_package(hiredis QUIET CONFIG)
if(TARGET hiredis::hiredis)
    set(_hiredis_target hiredis::hiredis)
endif()

if(NOT _hiredis_target AND PkgConfig_FOUND)
    pkg_check_modules(HIREDIS QUIET IMPORTED_TARGET hiredis)
    if(TARGET PkgConfig::HIREDIS)
        set(_hiredis_target PkgConfig::HIREDIS)
        set(_hiredis_includes ${HIREDIS_INCLUDE_DIRS})
    endif()
endif()

if(NOT _hiredis_target)
    include(FetchContent)
    set(DISABLE_TESTS    ON  CACHE BOOL "" FORCE)
    set(ENABLE_SSL       OFF CACHE BOOL "" FORCE)
    set(ENABLE_EXAMPLES  OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        hiredis
        GIT_REPOSITORY https://github.com/redis/hiredis.git
        GIT_TAG v1.2.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(hiredis)
    if(TARGET hiredis::hiredis)
        set(_hiredis_target hiredis::hiredis)
    elseif(TARGET hiredis)
        set(_hiredis_target hiredis)
        # FetchContent build does not export an include dir that exposes
        # `<hiredis.h>` directly — add the source root explicitly.
        set(_hiredis_includes ${hiredis_SOURCE_DIR})
    endif()
endif()

if(NOT _hiredis_target)
    message(WARNING "hiredis target not resolved; skipping redis example")
    return()
endif()

add_executable(redis_example redis_example.cpp)
target_link_libraries(redis_example PRIVATE
    pinpoint_cpp
    ${_hiredis_target}
)
if(_hiredis_includes)
    target_include_directories(redis_example PRIVATE ${_hiredis_includes})
endif()
