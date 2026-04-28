# gRPC + Protobuf are pulled in transitively by pinpoint-cpp-agent (it either
# finds them on the system or fetches them itself). We just need protoc and
# grpc_cpp_plugin to generate code from testapp.proto.

find_package(Protobuf CONFIG QUIET)
find_package(gRPC CONFIG QUIET)

if(NOT TARGET gRPC::grpc++)
    message(WARNING "gRPC::grpc++ target not available; skipping grpc example")
    return()
endif()
if(NOT TARGET protobuf::protoc)
    message(WARNING "protobuf::protoc target not available; skipping grpc example")
    return()
endif()
if(NOT TARGET gRPC::grpc_cpp_plugin)
    message(WARNING "gRPC::grpc_cpp_plugin target not available; skipping grpc example")
    return()
endif()

set(_proto_src ${CMAKE_CURRENT_SOURCE_DIR}/testapp.proto)
set(_gen_dir   ${CMAKE_CURRENT_BINARY_DIR}/gen)
file(MAKE_DIRECTORY ${_gen_dir})

set(_pb_cc   ${_gen_dir}/testapp.pb.cc)
set(_pb_h    ${_gen_dir}/testapp.pb.h)
set(_grpc_cc ${_gen_dir}/testapp.grpc.pb.cc)
set(_grpc_h  ${_gen_dir}/testapp.grpc.pb.h)

add_custom_command(
    OUTPUT ${_pb_cc} ${_pb_h} ${_grpc_cc} ${_grpc_h}
    COMMAND $<TARGET_FILE:protobuf::protoc>
    ARGS
        --cpp_out=${_gen_dir}
        --grpc_out=${_gen_dir}
        --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
        -I ${CMAKE_CURRENT_SOURCE_DIR}
        ${_proto_src}
    DEPENDS ${_proto_src} protobuf::protoc gRPC::grpc_cpp_plugin
    VERBATIM
)

add_library(testapp_grpc_proto STATIC ${_pb_cc} ${_grpc_cc})
target_include_directories(testapp_grpc_proto PUBLIC ${_gen_dir})
target_link_libraries(testapp_grpc_proto PUBLIC
    gRPC::grpc++
    protobuf::libprotobuf
)

add_executable(grpc_client grpc_client.cpp)
target_include_directories(grpc_client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(grpc_client PRIVATE
    pinpoint_cpp
    testapp_grpc_proto
)

add_executable(grpc_server grpc_server.cpp)
target_include_directories(grpc_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(grpc_server PRIVATE
    pinpoint_cpp
    testapp_grpc_proto
    gRPC::grpc++_reflection
)
