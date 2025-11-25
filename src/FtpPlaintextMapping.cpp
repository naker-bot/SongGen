#include "FtpPlaintextMapping.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <sys/stat.h>

namespace fs = std::filesystem;

std::map<std::string, std::string> loadFtpPlaintextMapping(const std::string& credsDir) {
    std::map<std::string, std::string> map;
    std::string mappingPath = credsDir + "/mapping.json";
    if (!fs::exists(mappingPath)) return map;
    try {
        std::ifstream f(mappingPath);
        if (!f.is_open()) return map;
        nlohmann::json j;
        f >> j;
        if (!j.is_object()) return map;
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (it.value().is_string()) map[it.key()] = it.value().get<std::string>();
        }
        f.close();
    } catch (...) {}
    return map;
}

bool saveFtpPlaintextMapping(const std::string& credsDir, const std::map<std::string, std::string>& mapping) {
    try {
        fs::create_directories(credsDir);
        nlohmann::json j = nlohmann::json::object();
        for (auto &kv : mapping) j[kv.first] = kv.second;
        std::string mappingPath = credsDir + "/mapping.json";
        std::ofstream f(mappingPath);
        if (!f.is_open()) return false;
        f << j.dump(4);
        f.close();
        chmod(mappingPath.c_str(), 0600);
        return true;
    } catch (...) { return false; }
}
