#pragma once
#include <string>
#include <map>

// Load mapping from credsDir/mapping.json (where credsDir is typically ~/.songgen/ftp)
std::map<std::string, std::string> loadFtpPlaintextMapping(const std::string& credsDir);
// Save mapping to credsDir/mapping.json
bool saveFtpPlaintextMapping(const std::string& credsDir, const std::map<std::string, std::string>& mapping);
