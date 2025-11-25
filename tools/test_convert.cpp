#include <iostream>
#include <filesystem>
#include <vector>
#include "SIDLibConverter.h"

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    if (argc < 5) {
        std::cout << "Usage: test_convert <sid_dir> <wav_dir> <threads> <count>\n";
        return 1;
    }
    std::string sidDir = argv[1];
    std::string wavDir = argv[2];
    int threads = std::stoi(argv[3]);
    int count = std::stoi(argv[4]);

    std::vector<std::string> sidFiles;
    for (const auto& entry : fs::recursive_directory_iterator(sidDir)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension().string();
        if (ext == ".sid" || ext == ".SID") {
            sidFiles.push_back(entry.path().string());
            if ((int)sidFiles.size() >= count) break;
        }
    }

    if (sidFiles.empty()) {
        std::cerr << "No SID files found in " << sidDir << "\n";
        return 1;
    }

    SIDLibConverter conv;
    auto result = conv.convertBatchParallel(sidFiles, wavDir, threads, 60, [](int done, int total) {
        std::cout << "Progress: " << done << "/" << total << "\r" << std::flush;
    });

    std::cout << "\nConverted " << result.size() << " WAV files; saved in: " << wavDir << "\n";
    for (const auto& f : result) {
        std::cout << " - " << f << "\n";
    }

    return 0;
}
