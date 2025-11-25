#include <iostream>
#include <fstream>
#include <filesystem>
#include <unistd.h>
#include "FtpPlaintextMapping.h"

int main() {
    // Clear test environment
    // Create a temporary HOME to isolate test environment
    std::string testHome = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") + "/songgen_test_home_" + std::to_string(getpid());
    setenv("HOME", testHome.c_str(), 1);
    std::filesystem::path credsDir = std::filesystem::path(testHome) / ".songgen/ftp";
    std::filesystem::create_directories(credsDir);
    // Write a settings.json to force plaintext-only credential storage for the test
    std::string settingsDir = std::string(testHome) + "/.songgen";
    std::string settingsPath = settingsDir + "/settings.json";
    std::filesystem::create_directories(settingsDir);
    {
        std::ofstream s(settingsPath);
        s << "{\n  \"credentialStoreMode\": 2\n}\n";
        s.close();
    }
    try { std::filesystem::create_directories(credsDir); } catch(...) {}
    auto mappingFile = credsDir / "mapping.json";
    if (std::filesystem::exists(mappingFile)) std::filesystem::remove(mappingFile);

    // Simulate saving a mapping entry without using ImGuiRenderer (integration tests handled separately)
    std::map<std::string, std::string> m;
    std::string safeName = "test_example_com_21";
    m[safeName] = "test.example.com:21";
    bool ok = saveFtpPlaintextMapping((std::string(testHome) + "/.songgen/ftp"), m);
    if (!ok) {
        std::cerr << "Failed to save plaintext ftp credentials" << std::endl;
        return 2;
    }

    auto mapping = loadFtpPlaintextMapping((std::string(testHome) + "/.songgen/ftp"));
    if (mapping.empty()) {
        std::cerr << "No mapping found after saving plaintext credential, test failed" << std::endl;
        return 3;
    }
    std::cout << "Mapping entries:" << std::endl;
    for (auto &kv : mapping) {
        std::cout << kv.first << " -> " << kv.second << std::endl;
    }

    // Validate the mapping contains our sanitized name
    std::string expectedSafe = "test_example_com_21";
    if (mapping.find(expectedSafe) == mapping.end()) {
        std::cerr << "Expected mapping key not found: " << expectedSafe << std::endl;
        return 4;
    }

    std::cout << "Test mapping successful." << std::endl;
    return 0;
}
