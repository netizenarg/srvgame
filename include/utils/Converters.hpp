#pragma once

inline bool SafeStringToInt64(const char* str, int64_t& result) {
    if (!str || str[0] == '\0') return false;

    char* endptr = nullptr;
    errno = 0;
    long long val = strtoll(str, &endptr, 10);

    if (errno == ERANGE || val < std::numeric_limits<int64_t>::min() ||
        val > std::numeric_limits<int64_t>::max()) {
        return false;
        }

        if (endptr == str || *endptr != '\0') {
            return false;
        }

        result = static_cast<int64_t>(val);
    return true;
}

inline bool SafeStringToInt(const char* str, int& result) {
    int64_t temp;
    if (!SafeStringToInt64(str, temp)) return false;

    if (temp < std::numeric_limits<int>::min() ||
        temp > std::numeric_limits<int>::max()) {
        return false;
        }

        result = static_cast<int>(temp);
    return true;
}
