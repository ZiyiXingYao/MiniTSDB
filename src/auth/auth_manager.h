#pragma once

#include "common/types.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

namespace minitsdb {

// 用户角色
enum class UserRole : uint8_t {
    ADMIN = 0,      // 全部权限
    OPERATOR = 1,   // 读写权限
    VIEWER = 2      // 只读权限
};

// 用户数据
struct User {
    std::string name;
    std::string password_hash;  // SHA-256(salt + password) 十六进制
    std::string salt;           // 随机 salt（16 字节 hex 编码 = 32 字符）
    UserRole role = UserRole::VIEWER;
    Timestamp created_at = 0;
    bool active = true;
};

// 会话 Token
struct SessionToken {
    std::string token;
    std::string username;
    UserRole role;
    Timestamp expires_at = 0;
};

// 认证管理器
class AuthManager {
public:
    AuthManager();

    // 初始化（加载用户数据）
    bool Init(const std::string& data_path);

    // 登录验证
    // 成功返回 token，失败返回空字符串
    std::string Login(const std::string& username, const std::string& password);

    // 管理员内部登录（无需密码，供 Executor 内部创建用户使用）
    std::string AdminLogin();

    // 验证 Token
    // 返回用户信息，无效返回 nullptr
    const User* ValidateToken(const std::string& token);

    // 创建用户（需要 admin 权限）
    bool CreateUser(const std::string& requester_token,
                    const std::string& username,
                    const std::string& password,
                    UserRole role);

    // 检查权限
    bool CheckPermission(const std::string& token,
                         const std::string& operation);

    // 删除用户（仅 admin 可执行）
    bool DropUser(const std::string& requester_token,
                  const std::string& username);

    // 修改用户属性（仅 admin 可执行）
    bool AlterUser(const std::string& requester_token,
                   const std::string& username,
                   const std::string& property,
                   const std::string& value);

    // 获取用户列表
    std::vector<User> GetUsers(const std::string& requester_token);

    // 持久化
    bool Save();
    bool Load();

    // Token 有效期（秒）
    void SetTokenExpiry(int64_t seconds) { token_expiry_sec_ = seconds; }

    // 生成随机 salt（32 字符 hex）
    static std::string GenerateSalt();

    // SHA-256 hex 摘要（用于测试/验证）
    static std::string Sha256Hex(const std::string& input);

private:
    std::string data_path_;
    std::unordered_map<std::string, User> users_;
    std::unordered_map<std::string, SessionToken> tokens_;
    int64_t token_expiry_sec_ = 28800;  // 默认 8 小时

    // 计算 SHA-256(salt + password)
    static std::string HashPassword(const std::string& password, const std::string& salt);

    // 生成随机 Token
    static std::string GenerateToken();

    // 清理过期 Token
    void CleanupExpiredTokens();
};

} // namespace minitsdb
