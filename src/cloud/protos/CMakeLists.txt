find_program(PROTOC protoc)
if (NOT PROTOC)
  message(FATAL_ERROR "protoc not found")
endif()

find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin)
if (NOT GRPC_CPP_PLUGIN)
  message(FATAL_ERROR "grpc_cpp_plugin not found")
endif()

if (NOT DEFINED PROTOBUF_INCLUDE_DIRS)
  message(FATAL_ERROR "PROTOBUF_INCLUDE_DIRS not defined")
endif()

set(PROTO_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(PROTO_SOURCE_DIR .)

set(PROTOS
    execute1
    ninjaRegister
    ninjaUnregister
)
set(PROTO_GENERATED_SRCS "")

foreach(proto ${PROTOS})
  add_custom_command(OUTPUT ${PROTO_GEN_DIR}/${proto}.pb.cc ${PROTO_GEN_DIR}/${proto}.pb.h
    COMMAND ${PROTOC} --proto_path=${CMAKE_CURRENT_SOURCE_DIR} --proto_path=${PROTOBUF_INCLUDE_DIRS} --cpp_out=${PROTO_GEN_DIR} ${proto}.proto
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${proto}.proto)
  add_custom_command(OUTPUT ${PROTO_GEN_DIR}/${proto}.grpc.pb.cc ${PROTO_GEN_DIR}/${proto}.grpc.pb.h ${PROTO_GEN_DIR}/${proto}_mock.grpc.pb.h
    COMMAND ${PROTOC} --proto_path=${CMAKE_CURRENT_SOURCE_DIR} --proto_path=${PROTOBUF_INCLUDE_DIRS}
      --grpc_out=generate_mock_code=true:${PROTO_GEN_DIR}
      --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN} ${proto}.proto
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${proto}.proto)
  list(APPEND PROTO_GENERATED_SRCS ${PROTO_GEN_DIR}/${proto}.pb.cc ${PROTO_GEN_DIR}/${proto}.grpc.pb.cc)
endforeach()

add_custom_target(sharebuild_generate_protobufs DEPENDS ${PROTO_GENERATED_SRCS})
set(PROTO_GEN_DIR ${PROTO_GEN_DIR} PARENT_SCOPE)
set(PROTO_GENERATED_SRCS ${PROTO_GENERATED_SRCS} PARENT_SCOPE)
set(PROTO_SOURCE_DIR ${PROTO_SOURCE_DIR} PARENT_SCOPE)
