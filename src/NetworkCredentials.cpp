#include "../include/ImGuiRenderer.h"
#include <fstream>
#include <sstream>
#include <iostream>

// Simple INI-style credential storage
// Format: [path]
//         username=user
//         password=pass

std::string ImGuiRenderer::getCredentialsFilePath() {
    std::string homeDir = std::getenv("HOME");
    return homeDir + "/.songgen/network_credentials.ini";
}

bool ImGuiRenderer::saveSmbCredentials(const std::string& smbPath, const std::string& username, const std::string& password) {
    std::string credFile = getCredentialsFilePath();
    std::map<std::string, std::pair<std::string, std::string>> allCreds;
    
    // Load existing credentials
    std::ifstream inFile(credFile);
    if (inFile.is_open()) {
        std::string line, currentPath;
        while (std::getline(inFile, line)) {
            if (line.empty() || line[0] == '#') continue;
            
            if (line[0] == '[' && line.back() == ']') {
                currentPath = line.substr(1, line.length() - 2);
            } else if (!currentPath.empty()) {
                size_t pos = line.find('=');
                if (pos != std::string::npos) {
                    std::string key = line.substr(0, pos);
                    std::string value = line.substr(pos + 1);
                    
                    if (key == "username") {
                        allCreds[currentPath].first = value;
                    } else if (key == "password") {
                        allCreds[currentPath].second = value;
                    }
                }
            }
        }
        inFile.close();
    }
    
    // Update credentials for this path
    allCreds[smbPath] = {username, password};
    
    // Write all credentials
    std::ofstream outFile(credFile);
    if (!outFile.is_open()) {
        std::cerr << "❌ Fehler beim Speichern der Credentials: " << credFile << std::endl;
        return false;
    }
    
    outFile << "# SongGen Network Credentials\n";
    outFile << "# Format: [path] followed by username= and password=\n\n";
    
    for (const auto& [path, creds] : allCreds) {
        outFile << "[" << path << "]\n";
        outFile << "username=" << creds.first << "\n";
        outFile << "password=" << creds.second << "\n\n";
    }
    
    outFile.close();
    std::cout << "✅ Credentials gespeichert für: " << smbPath << std::endl;
    return true;
}

bool ImGuiRenderer::loadSmbCredentials(const std::string& smbPath, std::string& username, std::string& password) {
    std::string credFile = getCredentialsFilePath();
    std::ifstream inFile(credFile);
    
    if (!inFile.is_open()) {
        return false;
    }
    
    std::string line, currentPath;
    bool foundPath = false;
    
    while (std::getline(inFile, line)) {
        if (line.empty() || line[0] == '#') continue;
        
        if (line[0] == '[' && line.back() == ']') {
            currentPath = line.substr(1, line.length() - 2);
            foundPath = (currentPath == smbPath);
        } else if (foundPath) {
            size_t pos = line.find('=');
            if (pos != std::string::npos) {
                std::string key = line.substr(0, pos);
                std::string value = line.substr(pos + 1);
                
                if (key == "username") {
                    username = value;
                } else if (key == "password") {
                    password = value;
                }
            }
        }
    }
    
    inFile.close();
    return foundPath && !username.empty();
}

bool ImGuiRenderer::saveFtpCredentials(const std::string& ftpPath, const std::string& username, const std::string& password) {
    return saveSmbCredentials(ftpPath, username, password);  // Same format
}

bool ImGuiRenderer::loadFtpCredentials(const std::string& ftpPath, std::string& username, std::string& password) {
    return loadSmbCredentials(ftpPath, username, password);  // Same format
}
