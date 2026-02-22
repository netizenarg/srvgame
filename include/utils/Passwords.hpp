#pragma once

#include <string>
#include <unistd.h>
#include <crypt.h>

namespace Passwords {

inline bool VerifyPassword(const std::string& plain, const std::string& hash) {
    // crypt() returns a string starting with the salt (first two characters of hash)
    // We assume the hash includes the salt (e.g., "$2y$...")
    std::string salt = hash.substr(0, 2);   // Not correct for bcrypt – adjust as needed
    // Actually, for crypt() the salt is the entire hash up to the last '$'
    // Better: use a library that understands the hash format.
    // This is a placeholder – you must adapt to your actual hash format.
    char* encrypted = crypt(plain.c_str(), hash.c_str());
    return encrypted && hash == encrypted;
}

inline std::string HashPassword(const std::string& plain) {
    // Generate a random salt (simplified – use a proper random source)
    std::string salt = "$2y$10$" + std::to_string(rand()) + std::to_string(rand());
    char* hash = crypt(plain.c_str(), salt.c_str());
    return hash ? std::string(hash) : "";
}

} // namespace Passwords