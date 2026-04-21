#include "database/SQLProvider.hpp"

bool SQLProvider::LoadFromFile(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        std::cerr << "Failed to open SQL file: " << filePath << std::endl;
        return false;
    }

    std::string line, currentKey, currentQuery;
    while (std::getline(file, line)) {
        // Check for section marker: -- [key]
        if (line.size() > 5 && line[0] == '-' && line[1] == '-' && line[2] == ' ' && line[3] == '[') {
            // Save previous query if any
            if (!currentKey.empty() && !currentQuery.empty()) {
                queries_[currentKey] = currentQuery;
            }
            // Extract key
            size_t endBracket = line.find(']', 4);
            if (endBracket != std::string::npos) {
                currentKey = line.substr(4, endBracket - 4);
                currentQuery.clear();
            } else {
                currentKey.clear();
            }
        } else if (!currentKey.empty()) {
            // Append line to current query (preserve newlines)
            currentQuery += line + "\n";
        }
    }
    // Save last query
    if (!currentKey.empty() && !currentQuery.empty()) {
        queries_[currentKey] = currentQuery;
    }

    file.close();
    return true;
}

std::string SQLProvider::GetQuery(const std::string& key) const {
    auto it = queries_.find(key);
    if (it == queries_.end()) {
        return "";
    }
    return it->second;
}
