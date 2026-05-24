cmake_minimum_required(VERSION 3.20)
project(MiniTSDB VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---------- vcpkg 集成 ----------
# 使用方式:
#   cmake --preset debug -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
# 或设置 CMAKE_TOOLCHAIN_FILE 环境变量

# ---------- 第三方依赖 ----------
find_package(Threads REQUIRED)

# 优先使用 Clang 编译的 gtest
set(GTEST_CLANG_INCLUDE "D:/Code/vcpkg/buildtrees/gtest/src/v1.17.0-0c449efaff.clean/googletest/include")
set(GTEST_CLANG_LIB "D:/Code/googletest_build/lib/libgtest.a")
set(GTEST_CLANG_MAIN_LIB "D:/Code/googletest_build/lib/libgtest_main.a")

if(EXISTS "${GTEST_CLANG_LIB}")
    message(STATUS "Using Clang-compiled gtest")
    set(GTest_FOUND TRUE)
    add_library(GTest::gtest STATIC IMPORTED)
    set_target_properties(GTest::gtest PROPERTIES
        IMPORTED_LOCATION "${GTEST_CLANG_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${GTEST_CLANG_INCLUDE}"
    )
    add_library(GTest::gtest_main STATIC IMPORTED)
    set_target_properties(GTest::gtest_main PROPERTIES
        IMPORTED_LOCATION "${GTEST_CLANG_MAIN_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${GTEST_CLANG_INCLUDE}"
    )
else()
    find_package(GTest CONFIG QUIET)
endif()

# Clang 编译的 gRPC 依赖
set(GRPC_CLANG_DIR "D:/Code/grpc_clang_build")
set(GRPC_CLANG_INSTALL "${GRPC_CLANG_DIR}/install")

if(EXISTS "${GRPC_CLANG_DIR}/build_grpc/grpc++.lib")
    message(STATUS "Using Clang-compiled gRPC")
    set(gRPC_FOUND TRUE)
    set(Protobuf_FOUND TRUE)

    # protobuf
    add_library(protobuf::libprotobuf STATIC IMPORTED)
    set_target_properties(protobuf::libprotobuf PROPERTIES
        IMPORTED_LOCATION "${GRPC_CLANG_DIR}/build_protobuf/protobuf.lib"
        INTERFACE_INCLUDE_DIRECTORIES "D:/Code/vcpkg/buildtrees/protobuf/src/v33.4-f8b43d71cd.clean/src;${GRPC_CLANG_INSTALL}/include"
    )
    add_library(protobuf::libprotobuf-lite STATIC IMPORTED)
    set_target_properties(protobuf::libprotobuf-lite PROPERTIES
        IMPORTED_LOCATION "${GRPC_CLANG_DIR}/build_protobuf/protobuf-lite.lib"
        INTERFACE_INCLUDE_DIRECTORIES "D:/Code/vcpkg/buildtrees/protobuf/src/v33.4-f8b43d71cd.clean/src;${GRPC_CLANG_INSTALL}/include"
    )

    # gRPC
    add_library(gRPC::grpc++ STATIC IMPORTED)
    set_target_properties(gRPC::grpc++ PROPERTIES
        IMPORTED_LOCATION "${GRPC_CLANG_DIR}/build_grpc/grpc++.lib"
        INTERFACE_INCLUDE_DIRECTORIES "D:/Code/vcpkg/buildtrees/grpc/src/v1.76.0-c85157dbaa.clean/include"
    )
    add_library(gRPC::grpc STATIC IMPORTED)
    set_target_properties(gRPC::grpc PROPERTIES
        IMPORTED_LOCATION "${GRPC_CLANG_DIR}/build_grpc/grpc.lib"
    )
    add_library(gRPC::gpr STATIC IMPORTED)
    set_target_properties(gRPC::gpr PROPERTIES
        IMPORTED_LOCATION "${GRPC_CLANG_DIR}/build_grpc/gpr.lib"
    )
    add_library(gRPC::grpc_plugin_support STATIC IMPORTED)
    set_target_properties(gRPC::grpc_plugin_support PROPERTIES
        IMPORTED_LOCATION "${GRPC_CLANG_DIR}/build_grpc/grpc_plugin_support.lib"
    )
    add_library(gRPC::address_sorting STATIC IMPORTED)
    set_target_properties(gRPC::address_sorting PROPERTIES
        IMPORTED_LOCATION "${GRPC_CLANG_DIR}/build_grpc/address_sorting.lib"
    )
    add_library(gRPC::upb STATIC IMPORTED)
    set_target_properties(gRPC::upb PROPERTIES
        IMPORTED_LOCATION "${GRPC_CLANG_DIR}/build_grpc/upb_base_lib.lib"
    )
    add_executable(gRPC::grpc_cpp_plugin IMPORTED)
    set_target_properties(gRPC::grpc_cpp_plugin PROPERTIES
        IMPORTED_LOCATION "${GRPC_CLANG_DIR}/build_grpc/grpc_cpp_plugin.exe"
    )
else()
    find_package(gRPC CONFIG QUIET)
    find_package(Protobuf CONFIG QUIET)
endif()

# ---------- 编译选项 (Google C++ 规范 + Clang) ----------
if(MSVC)
    set(COMMON_FLAGS /W4 /utf-8)
else()
    set(COMMON_FLAGS -Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

# ---------- 核心库 ----------
file(GLOB_RECURSE CORE_SOURCES
    src/common/*.cpp
    src/storage/*.cpp
    src/cache/*.cpp
    src/sql/*.cpp
    src/server/*.cpp
    src/alarm/*.cpp
    src/auth/*.cpp
)

add_library(minitsdb_core STATIC ${CORE_SOURCES})

target_include_directories(minitsdb_core PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
    ${CMAKE_CURRENT_BINARY_DIR}
    "D:/Code/vcpkg/installed/x64-windows/include"
)

target_compile_options(minitsdb_core PRIVATE ${COMMON_FLAGS})

target_link_libraries(minitsdb_core PUBLIC
    Threads::Threads
)

# ---------- Protobuf 代码生成 ----------
if(gRPC_FOUND AND Protobuf_FOUND)
    set(PROTO_FILES ${CMAKE_SOURCE_DIR}/protos/minitsdb.proto)
    set(PROTO_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/proto_gen)
    file(MAKE_DIRECTORY ${PROTO_GEN_DIR})

    find_program(PROTOC_EXEC protoc.exe PATHS "D:/Code/vcpkg/packages/protobuf_x64-windows/tools/protobuf" NO_DEFAULT_PATH)
    if(NOT PROTOC_EXEC)
        find_program(PROTOC_EXEC protoc)
    endif()

    add_custom_command(
        OUTPUT
            ${PROTO_GEN_DIR}/minitsdb.pb.h
            ${PROTO_GEN_DIR}/minitsdb.pb.cc
            ${PROTO_GEN_DIR}/minitsdb.grpc.pb.h
            ${PROTO_GEN_DIR}/minitsdb.grpc.pb.cc
        COMMAND ${PROTOC_EXEC}
            --proto_path=${CMAKE_SOURCE_DIR}/protos
            --cpp_out=${PROTO_GEN_DIR}
            --grpc_out=${PROTO_GEN_DIR}
            --plugin=protoc-gen-grpc=${GRPC_CLANG_DIR}/build_grpc/grpc_cpp_plugin.exe
            ${CMAKE_SOURCE_DIR}/protos/minitsdb.proto
        DEPENDS ${CMAKE_SOURCE_DIR}/protos/minitsdb.proto
        COMMENT "Generating protobuf/grpc code for minitsdb.proto"
    )

    target_include_directories(minitsdb_core PUBLIC ${PROTO_GEN_DIR})
    target_sources(minitsdb_core PRIVATE
        ${PROTO_GEN_DIR}/minitsdb.pb.cc
        ${PROTO_GEN_DIR}/minitsdb.grpc.pb.cc
    )
    target_include_directories(minitsdb_core PUBLIC
        "${GRPC_CLANG_INSTALL}/include"
        "${GRPC_CLANG_DIR}/build_abseil"
        "D:/Code/vcpkg/buildtrees/protobuf/src/v33.4-f8b43d71cd.clean/third_party/utf8_range"
    )
    target_link_libraries(minitsdb_core PUBLIC
        gRPC::grpc++
        gRPC::grpc
        protobuf::libprotobuf
        "D:/Code/grpc_clang_build/install/lib/abseil_all.lib"
        "D:/Code/grpc_clang_build/install/lib/upb_all.lib"
        "D:/Code/grpc_clang_build/libutf8_range.a"
        "D:/Code/grpc_clang_build/build_cares/lib/cares.lib"
        "D:/Code/grpc_clang_build/build_re2/re2.lib"
        "D:/Code/vcpkg/packages/openssl_x64-windows/lib/libcrypto.lib"
        "D:/Code/vcpkg/packages/openssl_x64-windows/lib/libssl.lib"
        "D:/Code/vcpkg/packages/zlib_x64-windows/lib/zlib.lib"
        ws2_32
        crypt32
        iphlpapi
    )
endif()

# ---------- 主服务端目标 ----------
add_executable(minitsdb src/main.cpp)
target_link_libraries(minitsdb PRIVATE minitsdb_core)

# ---------- CLI 客户端 ----------
add_executable(minitsdb_cli src/cli/main.cpp)
target_link_libraries(minitsdb_cli PRIVATE minitsdb_core)
target_include_directories(minitsdb_cli PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

# ---------- C SDK ----------
add_library(minitsdb_c SHARED src/sdk/minitsdb_sdk.cpp)
target_link_libraries(minitsdb_c PRIVATE minitsdb_core)
target_include_directories(minitsdb_c PUBLIC
    ${CMAKE_SOURCE_DIR}/src/sdk
    ${CMAKE_CURRENT_BINARY_DIR}
)
set_target_properties(minitsdb_c PROPERTIES
    OUTPUT_NAME minitsdb
    PREFIX ""
)

# ---------- 测试 (Google Test) ----------
enable_testing()

if(GTest_FOUND)
    function(add_minitsdb_gtest TEST_NAME TEST_FILE)
        add_executable(${TEST_NAME} ${TEST_FILE})
        target_link_libraries(${TEST_NAME} PRIVATE
            minitsdb_core
            GTest::gtest
            GTest::gtest_main
        )
        add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    endfunction()

    add_minitsdb_gtest(test_compressor tests/test_compressor.cpp)
    add_minitsdb_gtest(test_wal tests/test_wal.cpp)
    add_minitsdb_gtest(test_sql_parser tests/test_sql_parser.cpp)
    add_minitsdb_gtest(test_alarm tests/test_alarm.cpp)
    add_minitsdb_gtest(test_auth tests/test_auth.cpp)
    add_minitsdb_gtest(test_storage tests/test_storage.cpp)
    add_minitsdb_gtest(test_grpc_server tests/test_grpc_server.cpp)
    add_minitsdb_gtest(test_memtable tests/test_memtable.cpp)
    add_minitsdb_gtest(test_cache tests/test_cache.cpp)
    add_minitsdb_gtest(test_config tests/test_config.cpp)
    add_minitsdb_gtest(test_executor tests/test_executor.cpp)
else()
    # 回退：无 gtest 时使用简易测试
    function(add_minitsdb_test TEST_NAME TEST_FILE)
        add_executable(${TEST_NAME} ${TEST_FILE})
        target_link_libraries(${TEST_NAME} PRIVATE minitsdb_core)
        add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    endfunction()

    add_minitsdb_test(test_compressor tests/test_compressor.cpp)
    add_minitsdb_test(test_wal tests/test_wal.cpp)
    add_minitsdb_test(test_sql_parser tests/test_sql_parser.cpp)
endif()

# ---------- C SDK 测试 ----------
function(add_minitsdb_c_test TEST_NAME TEST_FILE)
    add_executable(${TEST_NAME} ${TEST_FILE})
    target_link_libraries(${TEST_NAME} PRIVATE minitsdb_c)
    target_include_directories(${TEST_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/src/sdk
        ${CMAKE_CURRENT_BINARY_DIR}
    )
    target_compile_options(${TEST_NAME} PRIVATE -D_DLL -D_MT)
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

add_minitsdb_c_test(test_c_sdk tests/test_c_sdk.c)
