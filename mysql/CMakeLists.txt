# MySQL Connector/C++ X DevAPI (mysqlcppconn8). Building from source has
# heavy/odd dependencies, so we look for it on the system; fall back to
# FetchContent only as a last resort.

set(_mysql_target "")
set(_mysql_includes "")
set(_mysql_libs "")

find_package(mysql-concpp QUIET CONFIG)
if(TARGET mysql::concpp-xdevapi)
    set(_mysql_target mysql::concpp-xdevapi)
elseif(TARGET mysql::concpp)
    set(_mysql_target mysql::concpp)
endif()

if(NOT _mysql_target AND PkgConfig_FOUND)
    pkg_check_modules(MYSQLX QUIET IMPORTED_TARGET mysqlcppconn8)
    if(TARGET PkgConfig::MYSQLX)
        set(_mysql_target PkgConfig::MYSQLX)
    endif()
endif()

# Homebrew on macOS installs to /opt/homebrew/opt/mysql-connector-c++
if(NOT _mysql_target)
    find_path(MYSQLX_INCLUDE_DIR
        NAMES mysqlx/xdevapi.h
        PATHS
            /opt/homebrew/opt/mysql-connector-c++/include
            /usr/local/opt/mysql-connector-c++/include
            /usr/local/include
            /usr/include
    )
    find_library(MYSQLX_LIBRARY
        NAMES mysqlcppconnx mysqlcppconn8
        PATHS
            /opt/homebrew/opt/mysql-connector-c++/lib
            /usr/local/opt/mysql-connector-c++/lib
            /usr/local/lib
            /usr/lib
    )
    if(MYSQLX_INCLUDE_DIR AND MYSQLX_LIBRARY)
        set(_mysql_includes ${MYSQLX_INCLUDE_DIR})
        set(_mysql_libs ${MYSQLX_LIBRARY})
    endif()
endif()

if(NOT _mysql_target AND NOT _mysql_libs)
    message(WARNING "MySQL Connector/C++ X DevAPI not found on the system; "
                    "skipping mysql example. Install via 'brew install mysql-connector-c++' "
                    "or your distro package manager.")
    return()
endif()

add_executable(mysql_example mysql_example.cpp)
target_include_directories(mysql_example PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/cpptrace
    ${_mysql_includes}
)
target_link_libraries(mysql_example PRIVATE
    pinpoint_cpp
    httplib::httplib
)
if(_mysql_target)
    target_link_libraries(mysql_example PRIVATE ${_mysql_target})
endif()
if(_mysql_libs)
    target_link_libraries(mysql_example PRIVATE ${_mysql_libs})
endif()
