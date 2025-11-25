#include <iostream>
#include <fstream>
#include <filesystem>
#include <unistd.h>
#include <sys/stat.h>
#include <cstdlib>
#include "FtpPlaintextMapping.h"

int main() {
    std::string tmp = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp");
    std::string testHome = tmp + "/songgen_migrate_test_" + std::to_string(getpid());
    setenv("HOME", testHome.c_str(), 1);
    std::filesystem::path credsDir = std::filesystem::path(testHome) / ".songgen/ftp";
    std::filesystem::create_directories(credsDir);

    // create plaintext file
    std::string safeName = "test_example_com_21";
    std::string fpath = (credsDir / (safeName + ".txt")).string();
    std::ofstream f(fpath);
    f << "migrate_user\n" << "migrate_pass\n";
    f.close();
    chmod(fpath.c_str(), 0600);

    // create mapping
    std::map<std::string, std::string> mapping;
    mapping[safeName] = "test.example.com:21";
    saveFtpPlaintextMapping(credsDir.string(), mapping);

    // Invoke the headless migration mode by running the songgen binary (integration test)
    int rc = system("./songgen --migrate");
    if (rc != 0) {
        std::cerr << "songgen --migrate returned exit code: " << rc << std::endl;
        return 6;
    }
    // Confirm the plaintext file has been removed after running the migration
    // ensure file has been deleted
    if (std::filesystem::exists(fpath)) {
        std::cerr << "File still exists after migration: " << fpath << std::endl;
        return 4;
    }
    // ensure mapping entry removed
    auto mapping2 = loadFtpPlaintextMapping(credsDir.string());
    if (mapping2.find(safeName) != mapping2.end()) {
        std::cerr << "Mapping entry still exists after migration" << std::endl;
        return 5;
    }

    std::cout << "Migration test passed." << std::endl;
    return 0;
}
