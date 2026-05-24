#pragma once

#include <string>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace minitsdb {

// 配置管理
class Config {
public:
    Config() = default;

    bool Load(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) return false;

        std::string section;
        std::string line;
        while (std::getline(file, line)) {
            // 去除首尾空白
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            line.erase(line.find_last_not_of(" \t\r\n") + 1);
            if (line.empty() || line[0] == '#') continue;

            // 节标题 [section]
            if (line[0] == '[' && line.back() == ']') {
                section = line.substr(1, line.size() - 2);
                continue;
            }

            auto pos = line.find('=');
            if (pos == std::string::npos) continue;

            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);
            // 去除首尾空格
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);

            // 去掉值两端的引号
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
                value = value.substr(1, value.size() - 2);
            if (value.size() >= 2 && value.front() == '\'' && value.back() == '\'')
                value = value.substr(1, value.size() - 2);

            std::string full_key = section.empty() ? key : section + "." + key;
            config_[full_key] = value;
        }
        return true;
    }

    std::string Get(const std::string& key, const std::string& default_val = "") const {
        auto it = config_.find(key);
        if (it != config_.end()) return it->second;
        return default_val;
    }

    int GetInt(const std::string& key, int default_val = 0) const {
        auto it = config_.find(key);
        if (it != config_.end()) {
            try { return std::stoi(it->second); } catch (...) {}
        }
        return default_val;
    }

private:
    std::unordered_map<std::string, std::string> config_;
};

} // namespace minitsdb
