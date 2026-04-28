find_package(cpptrace QUIET CONFIG)
if(NOT cpptrace_FOUND)
    include(FetchContent)
    set(CPPTRACE_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        cpptrace
        GIT_REPOSITORY https://github.com/jeremy-rifkin/cpptrace.git
        GIT_TAG v0.7.5
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(cpptrace)
endif()

add_executable(cpptrace_example cpptrace_example.cpp)
target_include_directories(cpptrace_example PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cpptrace_example PRIVATE
    pinpoint_cpp
    cpptrace::cpptrace
    httplib::httplib
)
