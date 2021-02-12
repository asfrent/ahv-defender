cmake_minimum_required(VERSION 3.16)

project(AHVDefender
        VERSION 1.0
        DESCRIPTION "AHV Defender blocks messages containing AHV strings."
        LANGUAGES CXX
)

list(APPEND CMAKE_PREFIX_PATH "/opt/grpc" "/opt/protoc")

find_package(Protobuf REQUIRED)
find_package(gRPC REQUIRED)

include_directories("/opt/grpc/include")

execute_process(COMMAND which grpc_cpp_plugin OUTPUT_VARIABLE GRPC_CPP_PLUGIN)
string(STRIP ${GRPC_CPP_PLUGIN} GRPC_CPP_PLUGIN)
message(STATUS ${GRPC_CPP_PLUGIN})

file(MAKE_DIRECTORY "protos/gen")

include_directories("protos/gen")

add_custom_command(
    OUTPUT ../protos/gen/ahvdefender.grpc.pb.h ../protos/gen/ahvdefender.grpc.pb.cc
    COMMAND "protoc"
    ARGS "--plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}" "--grpc_out=gen" "ahvdefender.proto"
    DEPENDS "protos/ahvdefender.proto"
    WORKING_DIRECTORY "../protos"
)

add_custom_command(
    OUTPUT ../protos/gen/ahvdefender.pb.h ../protos/gen/ahvdefender.pb.cc
    COMMAND "protoc"
    ARGS "--cpp_out=gen" "ahvdefender.proto"
    DEPENDS "protos/ahvdefender.proto"
    WORKING_DIRECTORY "../protos"
)

add_executable(email-analyzer
    email-analyzer/email-analyzer.cc
    protos/gen/ahvdefender.grpc.pb.h
    protos/gen/ahvdefender.grpc.pb.cc
    protos/gen/ahvdefender.pb.h
    protos/gen/ahvdefender.pb.cc
)

target_link_libraries(email-analyzer
    gRPC::grpc++_reflection
    protobuf::libprotobuf
)

add_executable(lookup-server
    lookup-server/lookup-server.cc
    protos/gen/ahvdefender.grpc.pb.h
    protos/gen/ahvdefender.grpc.pb.cc
    protos/gen/ahvdefender.pb.h
    protos/gen/ahvdefender.pb.cc
)

target_link_libraries(lookup-server
    gRPC::grpc++_reflection
    protobuf::libprotobuf
)