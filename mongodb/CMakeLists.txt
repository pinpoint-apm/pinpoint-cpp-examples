# mongocxx + bsoncxx. The mongo-cxx-driver depends on mongo-c-driver and is
# heavy to bootstrap from source; we prefer system installation.

find_package(mongocxx QUIET CONFIG)
find_package(bsoncxx  QUIET CONFIG)

if(NOT mongocxx_FOUND OR NOT bsoncxx_FOUND)
    include(FetchContent)
    set(BUILD_VERSION             "3.11.0" CACHE STRING "" FORCE)
    set(ENABLE_TESTS              OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING             OFF CACHE BOOL "" FORCE)
    set(ENABLE_EXAMPLES           OFF CACHE BOOL "" FORCE)
    set(MONGOCXX_OVERRIDE_DEFAULT_INSTALL_PREFIX OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        mongo_cxx_driver
        GIT_REPOSITORY https://github.com/mongodb/mongo-cxx-driver.git
        GIT_TAG r3.11.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(mongo_cxx_driver)
endif()

set(_mongo_target "")
set(_bson_target  "")
foreach(_t IN ITEMS mongo::mongocxx_shared mongocxx_shared mongo::mongocxx_static mongocxx_static)
    if(TARGET ${_t})
        set(_mongo_target ${_t})
        break()
    endif()
endforeach()
foreach(_t IN ITEMS mongo::bsoncxx_shared bsoncxx_shared mongo::bsoncxx_static bsoncxx_static)
    if(TARGET ${_t})
        set(_bson_target ${_t})
        break()
    endif()
endforeach()

if(NOT _mongo_target OR NOT _bson_target)
    message(WARNING "mongocxx/bsoncxx targets not resolved; skipping mongodb example")
    return()
endif()

add_executable(mongo_example mongo_example.cpp)
target_link_libraries(mongo_example PRIVATE
    pinpoint_cpp
    ${_mongo_target}
    ${_bson_target}
)
