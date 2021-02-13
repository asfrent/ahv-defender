cmake_minimum_required(VERSION 3.16)

project(AHVDefender
  VERSION 1.0
  DESCRIPTION "AHV Defender blocks messages containing AHV strings."
  LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

list(APPEND CMAKE_PREFIX_PATH "/opt/grpc" "/opt/protoc")

find_package(Protobuf REQUIRED)
find_package(gRPC REQUIRED)

execute_process(COMMAND which grpc_cpp_plugin OUTPUT_VARIABLE GRPC_CPP_PLUGIN)
string(STRIP ${GRPC_CPP_PLUGIN} GRPC_CPP_PLUGIN)
message(STATUS ${GRPC_CPP_PLUGIN})

file(MAKE_DIRECTORY "protos/gen")

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

include_directories("crypt_blowfish-1.3")
include_directories("protos/gen")
include_directories("/opt/grpc/include")
include_directories("lib")

set(BCRYPT_SRCFILES
	${CMAKE_CURRENT_SOURCE_DIR}/crypt_blowfish-1.3/crypt_blowfish.c
	${CMAKE_CURRENT_SOURCE_DIR}/crypt_blowfish-1.3/crypt_gensalt.c
	${CMAKE_CURRENT_SOURCE_DIR}/crypt_blowfish-1.3/wrapper.c
	${CMAKE_CURRENT_SOURCE_DIR}/crypt_blowfish-1.3/x86.S
)

set(GRPC_AND_PROTO_SRCFILES
  ${CMAKE_CURRENT_SOURCE_DIR}/protos/gen/ahvdefender.grpc.pb.h
  ${CMAKE_CURRENT_SOURCE_DIR}/protos/gen/ahvdefender.pb.h
  ${CMAKE_CURRENT_SOURCE_DIR}/protos/gen/ahvdefender.grpc.pb.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/protos/gen/ahvdefender.pb.cc
)

add_library(bcrypt
  ${BCRYPT_SRCFILES}
)

add_executable(email-analyzer
  email-analyzer/email-analyzer.cc
  lib/AHVUtil.hpp
  lib/AHVDatabaseClient.hpp
  ${GRPC_AND_PROTO_SRCFILES}
)

target_link_libraries(email-analyzer
  gRPC::grpc++_reflection
  protobuf::libprotobuf
)

add_executable(lookup-server
  lookup-server/lookup-server.cc
  lib/BCryptHasher.hpp
  ${GRPC_AND_PROTO_SRCFILES}
)

target_link_libraries(lookup-server
  gRPC::grpc++_reflection
  protobuf::libprotobuf
  bcrypt
)

add_executable(cli
  lookup-server/cli.cc
  lib/AHVDatabaseClient.hpp
  ${GRPC_AND_PROTO_SRCFILES}
)

target_link_libraries(cli
  gRPC::grpc++_reflection
  protobuf::libprotobuf
)

add_executable(ahv-gen
  tools/ahv-gen.cc
)

add_executable(db-gen
  tools/db-gen.cc
  lib/BCryptHasher.hpp
)

target_link_libraries(db-gen
  "bcrypt"
  pthread
)
