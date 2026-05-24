---
name: project-overview
description: MiniTSDB 项目定位、技术栈、构建系统、版本状态
type: project
---

# MiniTSDB 项目概述

## 定位
面向工业 SIS（厂级监控信息系统）场景的轻量级时序数据库，单机部署，通过 SQL 接口提供数据写入、查询和管理。旨在替代 PI（OSIsoft PI System）等传统工业实时数据库。

**Why**：工业 SIS 场景需要替代传统昂贵的实时数据库，降低成本。
**How to apply**：所有建议应优先考虑单机部署、工业场景、SQL 全接口的设计方向。

## 版本与许可
- 版本：0.1.0，许可：MIT

## 技术栈
- 语言：C++20（std::variant, shared_mutex, visit, format）
- 编译器：Clang 22（x64-windows-llvm triplet）
- 构建：CMake 3.20+ + Ninja generator
- 依赖管理：vcpkg（gtest, grpc, protobuf, zlib）
- 通信：gRPC + Protobuf
- 测试：Google Test，ctest 一键执行
- 日志：spdlog + 滚动文件 + gzip 压缩
- 其他：abseil, re2, OpenSSL（gRPC 依赖）
- 构建预设：debug, release, full, vcpkg, grpc-release
- 构建目录：build/{preset-name}

## 开发状态
所有核心功能已实现，14 次提交，~7000+ 行 C++。测试 15 目标 ~84 用例全部通过。
