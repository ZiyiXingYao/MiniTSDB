#include "auth/auth_manager.h"
#include <sstream>
#include <iomanip>
#include <random>
#include <cstring>
#include <chrono>
#include <fstream>
#include <filesystem>

namespace minitsdb {

namespace {

// 简易 SHA-256 实现（无外部依赖）
class SHA256 {
public:
    SHA256() { Init(); }

    void Update(const uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; i++) {
            data_[count_++] = data[i];
            if (count_ == 64) {
                ProcessBlock();
                count_ = 0;
            }
        }
    }

    void Update(const std::string& data) {
        Update(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    }

    std::string Digest() {
        uint64_t bits = (total_bits_ + count_ * 8);
        // Padding
        data_[count_++] = 0x80;
        while (count_ != 56) {
            if (count_ == 64) { ProcessBlock(); count_ = 0; }
            data_[count_++] = 0;
        }
        // Append length
        for (int i = 7; i >= 0; i--) {
            data_[count_++] = static_cast<uint8_t>((bits >> (i * 8)) & 0xFF);
        }
        ProcessBlock();

        std::ostringstream oss;
        for (int i = 0; i < 8; i++) {
            oss << std::hex << std::setfill('0') << std::setw(8) << h_[i];
        }
        return oss.str();
    }

private:
    uint32_t h_[8];
    uint8_t data_[64];
    size_t count_ = 0;
    uint64_t total_bits_ = 0;

    static const uint32_t K[64];

    void Init() {
        h_[0] = 0x6a09e667; h_[1] = 0xbb67ae85;
        h_[2] = 0x3c6ef372; h_[3] = 0xa54ff53a;
        h_[4] = 0x510e527f; h_[5] = 0x9b05688c;
        h_[6] = 0x1f83d9ab; h_[7] = 0x5be0cd19;
        count_ = 0;
        total_bits_ = 0;
    }

    uint32_t RotR(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void ProcessBlock() {
        total_bits_ += 512;
        uint32_t w[64];
        for (int i = 0; i < 16; i++) {
            w[i] = (data_[i*4] << 24) | (data_[i*4+1] << 16) |
                   (data_[i*4+2] << 8) | data_[i*4+3];
        }
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = RotR(w[i-15], 7) ^ RotR(w[i-15], 18) ^ (w[i-15] >> 3);
            uint32_t s1 = RotR(w[i-2], 17) ^ RotR(w[i-2], 19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }

        uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3];
        uint32_t e = h_[4], f = h_[5], g = h_[6], h = h_[7];

        for (int i = 0; i < 64; i++) {
            uint32_t S1 = RotR(e, 6) ^ RotR(e, 11) ^ RotR(e, 25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t temp1 = h + S1 + ch + K[i] + w[i];
            uint32_t S0 = RotR(a, 2) ^ RotR(a, 13) ^ RotR(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t temp2 = S0 + maj;

            h = g; g = f; f = e; e = d + temp1;
            d = c; c = b; b = a; a = temp1 + temp2;
        }

        h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d;
        h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += h;
    }
};

const uint32_t SHA256::K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

}  // anonymous namespace

// ============================================================
// AuthManager implementation
// ============================================================
AuthManager::AuthManager() {
    // 创建默认管理员
    User admin;
    admin.name = "admin";
    admin.role = UserRole::ADMIN;
    admin.password_hash = HashPassword("admin123");
    admin.created_at = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    users_["admin"] = admin;
}

bool AuthManager::Init(const std::string& data_path) {
    data_path_ = data_path;
    if (!Load()) {
        // 首次运行，保存默认用户
        return Save();
    }
    return true;
}

std::string AuthManager::Login(const std::string& username,
                                const std::string& password) {
    auto it = users_.find(username);
    if (it == users_.end() || !it->second.active) return "";

    if (it->second.password_hash != HashPassword(password)) return "";

    CleanupExpiredTokens();

    // 生成 Token
    SessionToken token;
    token.token = GenerateToken();
    token.username = username;
    token.role = it->second.role;
    token.expires_at = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count()
        + token_expiry_sec_;

    tokens_[token.token] = token;
    return token.token;
}

const User* AuthManager::ValidateToken(const std::string& token) {
    CleanupExpiredTokens();

    auto it = tokens_.find(token);
    if (it == tokens_.end()) return nullptr;

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (it->second.expires_at < now) {
        tokens_.erase(it);
        return nullptr;
    }

    auto user_it = users_.find(it->second.username);
    if (user_it == users_.end()) return nullptr;

    return &user_it->second;
}

bool AuthManager::CreateUser(const std::string& requester_token,
                              const std::string& username,
                              const std::string& password,
                              UserRole role) {
    auto* requester = ValidateToken(requester_token);
    if (!requester || requester->role != UserRole::ADMIN) return false;

    if (users_.find(username) != users_.end()) return false;

    User user;
    user.name = username;
    user.role = role;
    user.password_hash = HashPassword(password);
    user.created_at = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    users_[username] = user;
    return Save();
}

bool AuthManager::CheckPermission(const std::string& token,
                                   const std::string& operation) {
    auto* user = ValidateToken(token);
    if (!user) return false;

    if (user->role == UserRole::ADMIN) return true;
    if (user->role == UserRole::OPERATOR) {
        // operator 不能执行管理操作
        if (operation.find("CREATE TAG") == 0 ||
            operation.find("CREATE USER") == 0 ||
            operation.find("ALTER SYSTEM") == 0) {
            return false;
        }
        return true;
    }
    // viewer: 只能 SELECT
    if (operation.find("SELECT") == 0) return true;
    return false;
}

std::vector<User> AuthManager::GetUsers(const std::string& requester_token) {
    auto* requester = ValidateToken(requester_token);
    if (!requester || requester->role != UserRole::ADMIN) return {};

    std::vector<User> result;
    for (const auto& [name, user] : users_) {
        result.push_back(user);
    }
    return result;
}

bool AuthManager::Save() {
    try {
        std::filesystem::create_directories(data_path_ + "/meta");
        std::string filepath = data_path_ + "/meta/users.db";

        std::ofstream file(filepath, std::ios::binary);
        if (!file.is_open()) return false;

        uint32_t count = static_cast<uint32_t>(users_.size());
        file.write(reinterpret_cast<const char*>(&count), sizeof(count));

        for (const auto& [name, user] : users_) {
            uint16_t name_len = static_cast<uint16_t>(name.size());
            file.write(reinterpret_cast<const char*>(&name_len), sizeof(name_len));
            file.write(name.data(), name_len);

            uint16_t hash_len = static_cast<uint16_t>(user.password_hash.size());
            file.write(reinterpret_cast<const char*>(&hash_len), sizeof(hash_len));
            file.write(user.password_hash.data(), hash_len);

            uint8_t role = static_cast<uint8_t>(user.role);
            file.write(reinterpret_cast<const char*>(&role), sizeof(role));
            file.write(reinterpret_cast<const char*>(&user.created_at), sizeof(Timestamp));
            uint8_t active = user.active ? 1 : 0;
            file.write(reinterpret_cast<const char*>(&active), sizeof(active));
        }

        return true;
    } catch (...) {
        return false;
    }
}

bool AuthManager::Load() {
    try {
        std::string filepath = data_path_ + "/meta/users.db";
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) return false;

        uint32_t count;
        file.read(reinterpret_cast<char*>(&count), sizeof(count));
        if (count > 1000) return false;  // 防损坏

        users_.clear();
        for (uint32_t i = 0; i < count; i++) {
            User user;

            uint16_t name_len;
            file.read(reinterpret_cast<char*>(&name_len), sizeof(name_len));
            std::vector<char> name_buf(name_len);
            file.read(name_buf.data(), name_len);
            user.name = std::string(name_buf.data(), name_len);

            uint16_t hash_len;
            file.read(reinterpret_cast<char*>(&hash_len), sizeof(hash_len));
            std::vector<char> hash_buf(hash_len);
            file.read(hash_buf.data(), hash_len);
            user.password_hash = std::string(hash_buf.data(), hash_len);

            uint8_t role;
            file.read(reinterpret_cast<char*>(&role), sizeof(role));
            user.role = static_cast<UserRole>(role);

            file.read(reinterpret_cast<char*>(&user.created_at), sizeof(Timestamp));

            uint8_t active;
            file.read(reinterpret_cast<char*>(&active), sizeof(active));
            user.active = (active != 0);

            users_[user.name] = user;
        }
        return true;
    } catch (...) {
        return false;
    }
}

std::string AuthManager::HashPassword(const std::string& password) {
    SHA256 sha;
    sha.Update(password);
    return sha.Digest();
}

std::string AuthManager::GenerateToken() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;

    std::ostringstream oss;
    oss << std::hex << dis(gen) << dis(gen) << dis(gen) << dis(gen);
    return oss.str();
}

void AuthManager::CleanupExpiredTokens() {
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    for (auto it = tokens_.begin(); it != tokens_.end();) {
        if (it->second.expires_at < now) {
            it = tokens_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace minitsdb
