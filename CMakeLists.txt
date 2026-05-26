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

# vcpkg 安装路径（全部 Clang 编译，在本机用 x64-windows-llvm triplet）
set(VCPKG_LLVM_DIR "D:/Code/vcpkg/installed/x64-windows-llvm")

# ============ gtest ============
set(GTEST_INCLUDE "${VCPKG_LLVM_DIR}/include")
set(GTEST_LIB "${VCPKG_LLVM_DIR}/lib/gtest.lib")
set(GTEST_MAIN_LIB "${VCPKG_LLVM_DIR}/lib/gtest_main.lib")

if(EXISTS "${GTEST_LIB}")
    message(STATUS "Using vcpkg x64-windows-llvm gtest")
    set(GTest_FOUND TRUE)

    # gtest_main 可能不存在（vcpkg 只编了 gtest.lib）
    set(GTEST_MAIN_LIB "${VCPKG_LLVM_DIR}/lib/gtest_main.lib")
    if(EXISTS "${GTEST_MAIN_LIB}")
        add_library(GTest::gtest_main STATIC IMPORTED)
        set_target_properties(GTest::gtest_main PROPERTIES
            IMPORTED_LOCATION "${GTEST_MAIN_LIB}"
            INTERFACE_INCLUDE_DIRECTORIES "${GTEST_INCLUDE}"
        )
    else()
        # 用 gtest.lib 代替 gtest_main
        add_library(GTest::gtest_main STATIC IMPORTED)
        set_target_properties(GTest::gtest_main PROPERTIES
            IMPORTED_LOCATION "${GTEST_LIB}"
            INTERFACE_INCLUDE_DIRECTORIES "${GTEST_INCLUDE}"
        )
    endif()
else()
    message(STATUS "vcpkg gtest not found, trying find_package")
    find_package(GTest CONFIG QUIET)
endif()

# ============ protobuf ============
set(PROTOC_EXEC "${VCPKG_LLVM_DIR}/tools/protobuf/protoc.exe")

# 使用 find_package 找到 protobuf 及其依赖的 abseil
list(PREPEND CMAKE_PREFIX_PATH "${VCPKG_LLVM_DIR}")
find_package(absl CONFIG QUIET)
find_package(utf8_range CONFIG QUIET)

# 直接链接 protobuf 静态库（cmake config 会错误开启 DLL 模式）
set(PROTOBUF_LIB "${VCPKG_LLVM_DIR}/lib/libprotobuf.lib")
add_library(protobuf::libprotobuf UNKNOWN IMPORTED)
set_target_properties(protobuf::libprotobuf PROPERTIES
    IMPORTED_LOCATION "${PROTOBUF_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${VCPKG_LLVM_DIR}/include"
)
set(Protobuf_FOUND TRUE)
message(STATUS "Using direct protobuf static lib: ${PROTOBUF_LIB}")

# ============ gRPC ============
set(GRPC_INCLUDE "${VCPKG_LLVM_DIR}/include")
set(GRPC_CPP_LIB "${VCPKG_LLVM_DIR}/lib/grpc++.lib")
set(GRPC_C_LIB "${VCPKG_LLVM_DIR}/lib/grpc.lib")
set(GRPC_GPR_LIB "${VCPKG_LLVM_DIR}/lib/gpr.lib")
# grpc_cpp_plugin 是原生工具，在 MSVC triplet 下
set(GRPC_PLUGIN_PATH "D:/Code/vcpkg/installed/x64-windows/tools/grpc/grpc_cpp_plugin.exe")

if(EXISTS "${GRPC_CPP_LIB}")
    message(STATUS "Using vcpkg x64-windows-llvm gRPC")
    set(gRPC_FOUND TRUE)

    # gRPC C++ 库
    add_library(gRPC::grpc++ STATIC IMPORTED)
    set_target_properties(gRPC::grpc++ PROPERTIES
        IMPORTED_LOCATION "${GRPC_CPP_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${GRPC_INCLUDE}"
    )
    # gRPC C 库
    add_library(gRPC::grpc STATIC IMPORTED)
    set_target_properties(gRPC::grpc PROPERTIES
        IMPORTED_LOCATION "${GRPC_C_LIB}"
    )
    # gRPC GPR 库
    add_library(gRPC::gpr STATIC IMPORTED)
    set_target_properties(gRPC::gpr PROPERTIES
        IMPORTED_LOCATION "${GRPC_GPR_LIB}"
    )
    # gRPC C++ 插件（代码生成工具）
    if(EXISTS "${GRPC_PLUGIN_PATH}")
        add_executable(gRPC::grpc_cpp_plugin IMPORTED)
        set_target_properties(gRPC::grpc_cpp_plugin PROPERTIES
            IMPORTED_LOCATION "${GRPC_PLUGIN_PATH}"
        )
    endif()
else()
    find_package(gRPC CONFIG QUIET)
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
    src/snapshot/*.cpp
    src/sdk/minitsdb_sdk.cpp
)

add_library(minitsdb_core STATIC ${CORE_SOURCES})

target_include_directories(minitsdb_core PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
    ${CMAKE_CURRENT_BINARY_DIR}
    "${VCPKG_LLVM_DIR}/include"
)

target_compile_options(minitsdb_core PRIVATE ${COMMON_FLAGS})

# C SDK 源码在 core 中编译，定义导出标志避免 __declspec(dllimport)
target_compile_definitions(minitsdb_core PRIVATE minitsdb_c_EXPORTS)

# protobuf 静态库 + 全量 abseil（protobuf 依赖的 absl 多且分散）
file(GLOB ABSL_LIBS "${VCPKG_LLVM_DIR}/lib/absl_*.lib")
target_link_libraries(minitsdb_core PUBLIC "${PROTOBUF_LIB}" ${ABSL_LIBS})

target_link_libraries(minitsdb_core PUBLIC
    Threads::Threads
    "${VCPKG_LLVM_DIR}/lib/zlib.lib"
)

# ---------- Protobuf 代码生成 ----------
if(gRPC_FOUND AND Protobuf_FOUND)
    set(PROTO_FILES ${CMAKE_SOURCE_DIR}/protos/minitsdb.proto)
    set(PROTO_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/proto_gen)
    file(MAKE_DIRECTORY ${PROTO_GEN_DIR})

    find_program(PROTOC_EXEC protoc.exe PATHS "${VCPKG_LLVM_DIR}/tools/protobuf" NO_DEFAULT_PATH)
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
            --plugin=protoc-gen-grpc=${GRPC_PLUGIN_PATH}
            ${PROTO_FILES}
        DEPENDS ${PROTO_FILES}
        COMMENT "Generating protobuf/grpc code"
    )

    target_include_directories(minitsdb_core PUBLIC ${PROTO_GEN_DIR})
    target_sources(minitsdb_core PRIVATE
        ${PROTO_GEN_DIR}/minitsdb.pb.cc
        ${PROTO_GEN_DIR}/minitsdb.grpc.pb.cc
    )
    target_include_directories(minitsdb_core PUBLIC
        "${VCPKG_LLVM_DIR}/include"
    )
    target_link_libraries(minitsdb_core PUBLIC
        gRPC::grpc++
        gRPC::grpc
        "${VCPKG_LLVM_DIR}/lib/gpr.lib"
        "${VCPKG_LLVM_DIR}/lib/address_sorting.lib"
        "${VCPKG_LLVM_DIR}/lib/grpc_plugin_support.lib"
        "${VCPKG_LLVM_DIR}/lib/libupb.lib"
        "${VCPKG_LLVM_DIR}/lib/upb_base_lib.lib"
        "${VCPKG_LLVM_DIR}/lib/upb_hash_lib.lib"
        "${VCPKG_LLVM_DIR}/lib/upb_json_lib.lib"
        "${VCPKG_LLVM_DIR}/lib/upb_lex_lib.lib"
        "${VCPKG_LLVM_DIR}/lib/upb_mem_lib.lib"
        "${VCPKG_LLVM_DIR}/lib/upb_message_lib.lib"
        "${VCPKG_LLVM_DIR}/lib/upb_mini_descriptor_lib.lib"
        "${VCPKG_LLVM_DIR}/lib/upb_mini_table_lib.lib"
        "${VCPKG_LLVM_DIR}/lib/upb_reflection_lib.lib"
        "${VCPKG_LLVM_DIR}/lib/upb_textformat_lib.lib"
        "${VCPKG_LLVM_DIR}/lib/upb_wire_lib.lib"
        "${VCPKG_LLVM_DIR}/lib/utf8_range.lib"
        "${VCPKG_LLVM_DIR}/lib/utf8_validity.lib"
        "${VCPKG_LLVM_DIR}/lib/re2.lib"
        "${VCPKG_LLVM_DIR}/lib/absl_synchronization.lib"
        "${VCPKG_LLVM_DIR}/lib/absl_time.lib"
        "${VCPKG_LLVM_DIR}/lib/absl_cord.lib"
        "${VCPKG_LLVM_DIR}/lib/absl_strings.lib"
        "${VCPKG_LLVM_DIR}/lib/absl_hash.lib"
        "${VCPKG_LLVM_DIR}/lib/absl_status.lib"
        "${VCPKG_LLVM_DIR}/lib/cares.lib"
        "${VCPKG_LLVM_DIR}/lib/libcrypto.lib"
        "${VCPKG_LLVM_DIR}/lib/libssl.lib"
        "${VCPKG_LLVM_DIR}/lib/zlib.lib"
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
add_library(minitsdb_c INTERFACE)
target_link_libraries(minitsdb_c INTERFACE minitsdb_core)
target_include_directories(minitsdb_c INTERFACE
    ${CMAKE_SOURCE_DIR}/src/sdk
    ${CMAKE_CURRENT_BINARY_DIR}
)

# ---------- 测试 (Google Test) ----------
enable_testing()

# 测试服务端辅助库
add_library(test_server_lib STATIC tests/test_server.cpp)
target_include_directories(test_server_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/tests
)
target_link_libraries(test_server_lib PRIVATE
    Threads::Threads
)

if(GTest_FOUND)
    function(add_minitsdb_gtest TEST_NAME TEST_FILE)
        add_executable(${TEST_NAME} ${TEST_FILE}
            "D:/Code/vcpkg/buildtrees/gtest/src/v1.17.0-0c449efaff.clean/googletest/src/gtest_main.cc"
        )
        target_link_libraries(${TEST_NAME} PRIVATE
            minitsdb_core
            "${GTEST_LIB}"
        )
        target_include_directories(${TEST_NAME} PRIVATE "${GTEST_INCLUDE}")
        add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    endfunction()

    add_minitsdb_gtest(test_compressor tests/test_compressor.cpp)
    add_minitsdb_gtest(test_os_file tests/test_os_file.cpp)
    add_minitsdb_gtest(test_wal tests/test_wal.cpp)
    add_minitsdb_gtest(test_sql_parser tests/test_sql_parser.cpp)
    add_minitsdb_gtest(test_alarm tests/test_alarm.cpp)
    add_minitsdb_gtest(test_auth tests/test_auth.cpp)
    add_minitsdb_gtest(test_storage tests/test_storage.cpp)
    add_minitsdb_gtest(test_grpc_server tests/test_grpc_server.cpp)
    add_minitsdb_gtest(test_memtable tests/test_memtable.cpp)
    add_minitsdb_gtest(test_cache tests/test_cache.cpp)
    add_minitsdb_gtest(test_config tests/test_config.cpp)
    set_tests_properties(test_config PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    add_minitsdb_gtest(test_executor tests/test_executor.cpp)
    add_minitsdb_gtest(test_sdk tests/test_sdk.cpp)
    add_minitsdb_gtest(test_snapshot_store tests/test_snapshot_store.cpp)
    set_tests_properties(test_snapshot_store PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    add_minitsdb_gtest(test_snapshot_sql tests/test_snapshot_sql.cpp)
    set_tests_properties(test_snapshot_sql PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    add_minitsdb_gtest(test_sql_ddl tests/test_sql_ddl.cpp)
    set_tests_properties(test_sql_ddl PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    target_compile_definitions(test_sdk PRIVATE MINITSDB_SDK_BUILD)

    # test_cli 需要 test_server_lib
    add_executable(test_cli tests/test_cli.cpp
        "D:/Code/vcpkg/buildtrees/gtest/src/v1.17.0-0c449efaff.clean/googletest/src/gtest_main.cc"
    )
    target_link_libraries(test_cli PRIVATE
        minitsdb_core
        test_server_lib
        "${GTEST_LIB}"
    )
    target_include_directories(test_cli PRIVATE ${CMAKE_SOURCE_DIR}/tests)
    add_test(NAME test_cli COMMAND test_cli)
else()
    # 回退：无 gtest 时使用简易测试
    function(add_minitsdb_test TEST_NAME TEST_FILE)
        add_executable(${TEST_NAME} ${TEST_FILE})
        target_link_libraries(${TEST_NAME} PRIVATE minitsdb_core)
        add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    endfunction()

    add_minitsdb_test(test_compressor tests/test_compressor.cpp)
    add_minitsdb_test(test_os_file tests/test_os_file.cpp)
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
        ${CMAKE_SOURCE_DIR}/tests
    )
    target_compile_options(${TEST_NAME} PRIVATE -D_DLL -D_MT)
    target_compile_definitions(${TEST_NAME} PRIVATE MINITSDB_SDK_BUILD)
    target_link_libraries(${TEST_NAME} PRIVATE test_server_lib)
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

add_minitsdb_c_test(test_c_sdk tests/test_c_sdk.c)
