#include <gtest/gtest.h>
#include "auth/auth_manager.h"

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
