#include <string>
#include <vector>
#include <memory>
#include <cwchar>
#include <clocale>

static void set_locale() {
    static bool initialized = []{
        const char* locales[] = {".UTF-8", "en_US.UTF-8", "C.UTF-8"};
        for (const char* loc : locales) {
            if (std::setlocale(LC_ALL, loc)) {
                return true;
            }
        }
        return false;
    }();
    (void)initialized;
}

inline std::vector<std::string> split_add_prefix(const std::string& str, const std::string& delimiter, const std::string& prefix) {
    std::string str_item;
    std::vector<std::string> str_items;
    size_t start = 0;
    size_t end = str.find(delimiter);
    while (end != std::string::npos) {
        str_item = str.substr(start, end - start);
        if (!str_item.empty() && str_item.find(':') == std::string::npos && str_item.front() != '/')
            str_items.push_back(prefix + str_item);
        else
            str_items.push_back(str_item);
        start = end + delimiter.length();
        end = str.find(delimiter, start);
    }
    str_item = str.substr(start);
    if (!str_item.empty() && str_item.find(':') == std::string::npos && str_item.front() != '/')
        str_items.push_back(prefix + str_item);
    else
        str_items.push_back(str_item);
    return str_items;
}

inline std::wstring string_to_wstring(const std::string& str) {
    if (str.empty()) return L"";
    set_locale();
    size_t len = std::mbstowcs(nullptr, str.c_str(), 0);
    if (len == static_cast<size_t>(-1)) {
        return L"";
    }
    std::wstring result(len, L'\0');
    std::mbstowcs(result.data(), str.c_str(), len + 1);
    return result;
}

inline std::string wstring_to_string(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    set_locale();
    size_t len = std::wcstombs(nullptr, wstr.c_str(), 0);
    if (len == static_cast<size_t>(-1)) {
        return "";
    }
    std::string result(len, '\0');
    std::wcstombs(result.data(), wstr.c_str(), len + 1);
    return result;
}

inline std::vector<std::wstring> split_wstring(const std::wstring& wstr, wchar_t delim) {
    std::vector<std::wstring> wstr_items;
    size_t start = 0, end;
    while ((end = wstr.find(delim, start)) != std::wstring::npos) {
        if (end > start)
            wstr_items.push_back(wstr.substr(start, end - start));
        start = end + 1;
    }
    if (start < wstr.size())
        wstr_items.push_back(wstr.substr(start));
    return wstr_items;
}

class WArray {
public:
    explicit WArray(const std::string& paths, const std::string& delimiter, const std::string& prefix)
        : ptr_(nullptr)
    {
        std::vector<std::string> items = split_add_prefix(paths, delimiter, prefix);
        std::vector<std::wstring> witems;
        for(auto it : items)
        {
            witems.push_back(string_to_wstring(it));
        }
        ptr_ = new wchar_t*[witems.size() + 1];
        for (size_t i = 0; i < witems.size(); ++i) {
            size_t len = witems[i].size() + 1;
            ptr_[i] = new wchar_t[len];
            std::wcscpy(ptr_[i], witems[i].c_str());
        }
        ptr_[witems.size()] = nullptr;
    }

    explicit WArray(const std::string& paths, wchar_t delim = L';')
        : ptr_(nullptr)
    {
        std::wstring wide = string_to_wstring(paths);
        std::vector<std::wstring> wstr_items = split_wstring(wide, delim);
        ptr_ = new wchar_t*[wstr_items.size() + 1];
        for (size_t i = 0; i < wstr_items.size(); ++i) {
            size_t len = wstr_items[i].size() + 1;
            ptr_[i] = new wchar_t[len];
            std::wcscpy(ptr_[i], wstr_items[i].c_str());
        }
        ptr_[wstr_items.size()] = nullptr;
    }

    ~WArray() {
        if (ptr_) {
            for (size_t i = 0; ptr_[i]; ++i)
                delete[] ptr_[i];
            delete[] ptr_;
        }
    }

    WArray(const WArray&) = delete;
    WArray& operator=(const WArray&) = delete;
    WArray(WArray&& other) noexcept : ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }
    WArray& operator=(WArray&& other) noexcept {
        if (this != &other) {
            cleanup();
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    wchar_t** get() const noexcept { return ptr_; }
    size_t size() const noexcept {
        size_t count = 0;
        if (ptr_) {
            while (ptr_[count]) ++count;
        }
        return count;
    }

private:
    void cleanup() {
        if (ptr_) {
            for (size_t i = 0; ptr_[i]; ++i)
                delete[] ptr_[i];
            delete[] ptr_;
        }
    }
    wchar_t** ptr_;
};
