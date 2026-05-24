#pragma once

#include "common/types.h"
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

namespace minitsdb {

// 报警规则
struct AlarmRule {
    std::string name;           // 规则名称
    std::string tag_name;       // 监控的 Tag
    std::string condition;      // 条件表达式，如 "value > 1400.0"
    double threshold = 0;       // 解析后的阈值
    std::string op;             // 比较操作符: >, <, >=, <=, ==
    std::vector<std::string> actions;  // 动作列表: "log", "notify"

    // 评估条件是否满足
    bool Evaluate(double value) const;
};

// 报警事件
struct AlarmEvent {
    std::string alarm_name;
    std::string tag_name;
    double value = 0;
    Timestamp ts = 0;
    std::string condition;
};

// 报警引擎
class AlarmEngine {
public:
    AlarmEngine();

    // 注册报警规则
    bool AddRule(const AlarmRule& rule);

    // 删除报警规则
    bool RemoveRule(const std::string& name);

    // 获取所有规则
    std::vector<AlarmRule> GetRules() const;

    // 评估写入的数据点，触发符合条件的报警
    void Evaluate(const std::string& tag, const DataPoint& point);

    // 查询报警事件
    std::vector<AlarmEvent> QueryEvents(const std::string& tag,
                                         Timestamp start, Timestamp end);

    // 报警触发回调
    using AlarmCallback = std::function<void(const AlarmEvent&)>;
    void SetOnAlarm(AlarmCallback cb) { on_alarm_ = std::move(cb); }

    // 获取事件数量
    size_t EventCount() const { return events_.size(); }

private:
    std::vector<AlarmRule> rules_;
    std::vector<AlarmEvent> events_;
    AlarmCallback on_alarm_;

    // 解析条件表达式
    bool ParseCondition(const std::string& condition,
                        std::string& op, double& threshold);
};

} // namespace minitsdb
