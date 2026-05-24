#include <gtest/gtest.h>
#include "auth/auth_manager.h"
#include <filesystem>
#include <system_error>

using namespace minitsdb;

class AuthTest : public ::testing::Test {
protected:
    AuthManager auth;
};

TEST_F(AuthTest, DefaultAdmin) {
    // 默认管理员 admin/admin123
    std::string token = auth.Login("admin", "admin123");
    EXPECT_FALSE(token.empty());

    const User* user = auth.ValidateToken(token);
    ASSERT_NE(user, nullptr);
    EXPECT_EQ(user->name, "admin");
    EXPECT_EQ(user->role, UserRole::ADMIN);
}

TEST_F(AuthTest, WrongPassword) {
    std::string token = auth.Login("admin", "wrong");
    EXPECT_TRUE(token.empty());
}

TEST_F(AuthTest, UnknownUser) {
    std::string token = auth.Login("nobody", "pass");
    EXPECT_TRUE(token.empty());
}

TEST_F(AuthTest, PermissionCheck) {
    std::string admin_token = auth.Login("admin", "admin123");
    ASSERT_FALSE(admin_token.empty());

    // admin 可以做任何事
    EXPECT_TRUE(auth.CheckPermission(admin_token, "INSERT"));
    EXPECT_TRUE(auth.CheckPermission(admin_token, "SELECT"));
    EXPECT_TRUE(auth.CheckPermission(admin_token, "CREATE TAG"));
}

TEST_F(AuthTest, InvalidToken) {
    EXPECT_FALSE(auth.CheckPermission("invalid-token", "SELECT"));
}

TEST_F(AuthTest, SaltGeneration) {
    // 验证每次生成的 salt 不同
    std::string salt1 = AuthManager::GenerateSalt();
    std::string salt2 = AuthManager::GenerateSalt();
    EXPECT_EQ(salt1.size(), 32);
    EXPECT_EQ(salt2.size(), 32);
    EXPECT_NE(salt1, salt2);
    // 验证只包含 hex 字符
    for (char c : salt1) {
        EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }
}

TEST_F(AuthTest, SaltPersistence) {
    // 验证 Save/Load 后 salt 保持正确
    AuthManager auth2;
    ASSERT_TRUE(auth2.Init("./test_auth_salt_data"));

    // 创建用户
    std::string admin_token = auth2.Login("admin", "admin123");
    ASSERT_FALSE(admin_token.empty());
    ASSERT_TRUE(auth2.CreateUser(admin_token, "testuser", "mypass", UserRole::VIEWER));

    // 登录验证带 salt 的密码
    std::string user_token = auth2.Login("testuser", "mypass");
    EXPECT_FALSE(user_token.empty());

    // 重新加载验证
    AuthManager auth3;
    ASSERT_TRUE(auth3.Init("./test_auth_salt_data"));
    std::string reloaded_token = auth3.Login("testuser", "mypass");
    EXPECT_FALSE(reloaded_token.empty());
    // 错误密码仍应失败
    EXPECT_TRUE(auth3.Login("testuser", "wrong").empty());

    // 清理
    std::error_code ec;
    std::filesystem::remove_all("./test_auth_salt_data", ec);
}
