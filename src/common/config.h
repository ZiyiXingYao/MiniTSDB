#pragma once

#include <string>
#include <unordered_map>

namespace minitsdb {

// 配置管理
class Config {
public:
    Config() = default;

    bool Load(const std::string& path);

    std::string Get(const std::string& key, const std::string& default_val = "") const;

    int GetInt(const std::string& key, int default_val = 0) const;

private:
    std::unordered_map<std::string, std::string> config_;
};

} // namespace minitsdb
