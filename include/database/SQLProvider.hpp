#pragma once

#include <string>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <iostream>

class SQLProvider {
public:
    bool LoadFromFile(const std::string& filePath);

    std::string GetQuery(const std::string& key) const;

private:
    std::unordered_map<std::string, std::string> queries_;
};
