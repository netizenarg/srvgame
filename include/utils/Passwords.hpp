#pragma once

#include <string>
#include <random>
#include <cstring>
#include <crypt.h>

namespace Passwords {

    inline std::string generate_bcrypt_salt(int cost = 10) {
        static const char base64_chars[] = "./ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 63);
        std::string salt = "$2b$" + std::to_string(cost) + "$";
        for (int i = 0; i < 22; ++i) {
            salt += base64_chars[dis(gen)];
        }
        return salt;
    }

    inline bool VerifyPassword(const std::string& plain, const std::string& hash) {
        if (hash.empty() || hash[0] == '*') {
            return plain.empty();
        }
        char* encrypted = crypt(plain.c_str(), hash.c_str());
        if (!encrypted) return false;
        return hash == encrypted;
    }

    inline std::string HashPassword(const std::string& plain) {
        std::string salt = generate_bcrypt_salt();
        char* hash = crypt(plain.c_str(), salt.c_str());
        if (!hash) {
            // fallback to avoid storing an error marker
            static const char fallback_salt[] = "$2b$10$.......................";
            hash = crypt(plain.c_str(), fallback_salt);
            if (!hash) return "";
        }
        return std::string(hash);
    }

} // namespace Passwords
