#include "alarm/alarm_engine.h"
#include <sstream>
#include <algorithm>
#include <chrono>
#include <cctype>

namespace minitsdb {

AlarmEngine::AlarmEngine() = default;

bool AlarmRule::Evaluate(double value) const {
    if (op == ">") return value > threshold;
    if (op == "<") return value < threshold;
    if (op == ">=") return value >= threshold;
    if (op == "<=") return value <= threshold;
    if (op == "==") return value == threshold;
    if (op == "!=") return value != threshold;
    return false;
}

bool AlarmEngine::AddRule(const AlarmRule& rule) {

    // 检查是否已存在同名规则
    for (const auto& r : rules_) {
        if (r.name == rule.name) return false;
    }

    // 解析条件表达式
    std::string op;
    double threshold;
    if (!ParseCondition(rule.condition, op, threshold)) return false;

    AlarmRule parsed = rule;
    parsed.op = op;
    parsed.threshold = threshold;
    rules_.push_back(std::move(parsed));
    return true;
}

bool AlarmEngine::RemoveRule(const std::string& name) {
    auto it = std::remove_if(rules_.begin(), rules_.end(),
        [&](const AlarmRule& r) { return r.name == name; });
    if (it == rules_.end()) return false;
    rules_.erase(it, rules_.end());
    return true;
}

std::vector<AlarmRule> AlarmEngine::GetRules() const {
    return rules_;
}

bool AlarmEngine::AlterRule(const std::string& name, const std::string& property,
                             const std::string& value) {
    for (auto& r : rules_) {
        if (r.name == name) {
            if (property == "condition") {
                std::string op;
                double threshold;
                if (!ParseCondition(value, op, threshold)) return false;
                r.condition = value;
                r.op = op;
                r.threshold = threshold;
                return true;
            } else if (property == "action") {
                r.actions.clear();
                std::string remaining = value;
                size_t start = 0, end;
                while ((end = remaining.find(',', start)) != std::string::npos) {
                    auto token = remaining.substr(start, end - start);
                    // Trim whitespace
                    auto first = token.find_first_not_of(' ');
                    auto last = token.find_last_not_of(' ');
                    if (first != std::string::npos)
                        token = token.substr(first, last - first + 1);
                    else token.clear();
                    if (!token.empty()) r.actions.push_back(token);
                    start = end + 1;
                }
                auto token = remaining.substr(start);
                auto first = token.find_first_not_of(' ');
                auto last = token.find_last_not_of(' ');
                if (first != std::string::npos) {
                    token = token.substr(first, last - first + 1);
                    if (!token.empty()) r.actions.push_back(token);
                }
                return true;
            }
            return false;  // 不支持其他属性
        }
    }
    return false;  // 规则不存在
}

int AlarmEngine::RemoveRulesByTag(const std::string& tag_name) {
    auto it = std::remove_if(rules_.begin(), rules_.end(),
        [&](const AlarmRule& r) { return r.tag_name == tag_name; });
    int count = static_cast<int>(std::distance(it, rules_.end()));
    rules_.erase(it, rules_.end());
    return count;
}

void AlarmEngine::Evaluate(const std::string& tag, const DataPoint& point) {
    if (!std::holds_alternative<double>(point.value)) return;

    double val = std::get<double>(point.value);

    for (const auto& rule : rules_) {
        if (rule.tag_name != tag) continue;

        if (rule.Evaluate(val)) {
            // 去重：同一规则 60 秒内不重复触发
            auto it = last_trigger_time_.find(rule.name);
            if (it != last_trigger_time_.end() &&
                point.ts - it->second < 60000) {
                continue;
            }
            last_trigger_time_[rule.name] = point.ts;

            AlarmEvent event;
            event.alarm_name = rule.name;
            event.tag_name = tag;
            event.value = val;
            event.ts = point.ts;
            event.condition = rule.condition;

            // 限制 events_ 大小，最多 10000 条
            if (events_.size() < 10000) {
                events_.push_back(event);
            }

            if (on_alarm_) {
                on_alarm_(event);
            }
        }
    }
}

std::vector<AlarmEvent> AlarmEngine::QueryEvents(const std::string& tag,
                                                   Timestamp start,
                                                   Timestamp end) {
    std::vector<AlarmEvent> result;
    for (const auto& e : events_) {
        if (!tag.empty() && e.tag_name != tag) continue;
        if (e.ts < start || e.ts > end) continue;
        result.push_back(e);
    }
    return result;
}

bool AlarmEngine::ParseCondition(const std::string& condition,
                                  std::string& op, double& threshold) {
    std::string expr = condition;
    // 移除空白
    expr.erase(std::remove_if(expr.begin(), expr.end(), ::isspace), expr.end());

    // 查找操作符
    struct OpInfo { const char* str; size_t len; };
    const OpInfo ops[] = {
        {">=", 2}, {"<=", 2}, {"!=", 2}, {"==", 2},
        {">", 1}, {"<", 1}
    };

    for (const auto& op_info : ops) {
        auto pos = expr.find(op_info.str);
        if (pos != std::string::npos) {
            op = op_info.str;
            std::string val_str = expr.substr(pos + op_info.len);
            try {
                threshold = std::stod(val_str);
                return true;
            } catch (...) {
                return false;
            }
        }
    }

    return false;
}

} // namespace minitsdb
