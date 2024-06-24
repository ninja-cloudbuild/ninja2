project(share_build_executor)

file(GLOB SRCS *.cc)

find_package(OpenSSL REQUIRED)
set(OPENSSL_TARGET OpenSSL::Crypto)

find_package(Protobuf)
if(Protobuf_FOUND)
  if(Protobuf_VERSION VERSION_LESS 3.5)
    message(FATAL_ERROR "Version of Protobuf too old (${Protobuf_VERSION}), should be >=3.5.")
  elseif(NOT TARGET protobuf::libprotobuf)
    message(AUTHOR_WARNING "Cmake too old to define protobuf::libprotobuf, will try PkgConfig instead.")
  else()
    set(PROTOBUF_TARGET protobuf::libprotobuf)
    set(PROTOBUF_INCLUDE_DIRS ${Protobuf_INCLUDE_DIRS})
  endif()
endif()

if(NOT DEFINED PROTOBUF_TARGET)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(protobuf REQUIRED IMPORTED_TARGET protobuf>=3.5)
  set(PROTOBUF_TARGET PkgConfig::protobuf)
  set(PROTOBUF_INCLUDE_DIRS ${Protobuf_INCLUDE_DIRS})
endif()

# Try to find gRPC with find_package and fall back to pkg_config
find_package(gRPC)
if(gRPC_FOUND)
  set(GRPC_TARGET gRPC::grpc++)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(grpc++ REQUIRED IMPORTED_TARGET grpc++>=1.10)
  set(GRPC_TARGET PkgConfig::grpc++)
endif()

add_subdirectory(protos)

set_source_files_properties(${PROTO_GENERATED_SRCS} PROPERTIES COMPILE_FLAGS "-Wno-all -Wno-error -Wno-extra -Wno-conversion")

add_library(share_build_executor STATIC ${SRCS} ${PROTO_GENERATED_SRCS})
target_link_libraries(share_build_executor ${PROTOBUF_TARGET} ${GRPC_TARGET} ${OPENSSL_TARGET})

if(NOT APPLE)
  # macOS includes UUID generation functionality in libc, but on other platforms
  # it's a separate library.
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(uuid REQUIRED IMPORTED_TARGET uuid)
  target_link_libraries(share_build_executor PkgConfig::uuid)
endif()

set_source_files_properties(${PROTO_GENERATED_SRCS} PROPERTIES GENERATED 1)
add_dependencies(share_build_executor sharebuild_generate_protobufs)
target_include_directories(share_build_executor PUBLIC ${PROTO_GEN_DIR})
