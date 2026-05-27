#pragma once

#include <string>
#include <variant>
#include <vector>
#include <cstdint>
#include <chrono>

namespace minitsdb {

// 时间戳类型（微秒精度，Unix 纪元以来的微秒数）
using Timestamp = int64_t;

// 测点值类型
using Value = std::variant<double, int64_t, std::string>;

// 测点数据类型
enum class TagType : uint8_t {
    ANALOG = 0,       // 模拟量（浮点）
    DIGITAL = 1,      // 数字量/状态量（整数）
    STRING = 2,       // 字符串量
    ACCUMULATOR = 3   // 累加量（单调递增）
};

// 一个数据点
struct DataPoint {
    Timestamp ts;
    Value value;
};

// 测点元数据
struct TagMeta {
    std::string name;          // 测点名称，如 "BOILER-001"
    TagType type = TagType::ANALOG;
    std::string description;   // 描述
    std::string unit;          // 单位
    int32_t precision = 1;     // 小数位
    double min_value = 0.0;
    double max_value = 1500.0;
    int32_t collect_interval_ms = 1000;  // 采集间隔
};

// 数据点批次（批量写入用）
struct DataBatch {
    std::string tag_name;
    std::vector<DataPoint> points;
};

// 查询结果的一行
struct ResultRow {
    std::vector<std::string> columns;
};

// 存储层级
enum class StorageTier : uint8_t {
    HOT = 0,
    COLD = 1
};

// 时间范围
struct TimeRange {
    Timestamp start = 0;
    Timestamp end = 0;

    bool Contains(Timestamp ts) const {
        return ts >= start && ts <= end;
    }

    bool Overlaps(const TimeRange& other) const {
        return start <= other.end && end >= other.start;
    }
};

// 聚合类型
enum class AggType : uint8_t {
    NONE = 0,
    AVG = 1,
    MAX = 2,
    MIN = 3,
    SUM = 4,
    COUNT = 5,
    FIRST = 6,
    LAST = 7,
    STDDEV = 8
};

} // namespace minitsdb
