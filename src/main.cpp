#include "SIDLibConverter.h"
#include <iostream>
#include <string>
#include <filesystem>
#include <fstream>
#include <csignal>
#include <unistd.h>
#include <sys/wait.h>

namespace fs = std::filesystem;

std::string pidFilePath;

void cleanupPidFile() {
    if (!pidFilePath.empty() && fs::exists(pidFilePath)) {
        fs::remove(pidFilePath);
    }
}

void signalHandler(int signum) {
    std::cout << "\n🛑 Signal empfangen (" << signum << "), beende sauber...\n";
    cleanupPidFile();
    exit(signum);
}

void setupSignalHandlers() {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    signal(SIGQUIT, signalHandler);
}

void writePidFile() {
    pid_t pid = getpid();
    const char* home = getenv("HOME");
    if (home) {
        std::string songgenDir = std::string(home) + "/.songgen";
        fs::create_directories(songgenDir);
        pidFilePath = songgenDir + "/songgen.pid";
        
        std::ofstream pidFile(pidFilePath);
        if (pidFile.is_open()) {
            pidFile << pid;
            pidFile.close();
            std::cout << "📋 PID: " << pid << " gespeichert in " << pidFilePath << "\n";
        }
    }
}

int main(int argc, char** argv) {
    setupSignalHandlers();
    writePidFile();
    atexit(cleanupPidFile);
    std::cout << "SongGen - SID to WAV Converter\n";
    std::cout << "==============================\n\n";
    
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <input.sid> <output.wav>\n";
        std::cout << "   or: " << argv[0] << " <sid_directory> <wav_directory>\n";
        return 1;
    }
    
    std::string input = argv[1];
    std::string output = argv[2];
    
    SIDLibConverter converter;
    
    if (fs::is_directory(input)) {
        std::cout << "Batch conversion mode\n";
        std::cout << "Input directory: " << input << "\n";
        std::cout << "Output directory: " << output << "\n\n";
        
        fs::create_directories(output);
        
        std::vector<std::string> sidFiles;
        for (const auto& entry : fs::recursive_directory_iterator(input)) {
            if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                if (ext == ".sid" || ext == ".SID") {
                    sidFiles.push_back(entry.path().string());
                }
            }
        }
        
        std::cout << "Found " << sidFiles.size() << " SID files\n";
        std::cout << "Starting parallel conversion...\n\n";
        
        auto results = converter.convertBatchParallel(sidFiles, output, 200);
        
        size_t successful = 0;
        for (const auto& [file, success] : results) {
            if (success) successful++;
        }
        
        std::cout << "\n=================================\n";
        std::cout << "Conversion complete!\n";
        std::cout << "Successful: " << successful << "/" << sidFiles.size() << "\n";
        std::cout << "=================================\n";
        
    } else {
        std::cout << "Single file conversion\n";
        std::cout << "Input: " << input << "\n";
        std::cout << "Output: " << output << "\n\n";
        
        if (converter.convertToWAV(input, output)) {
            std::cout << "✅ Conversion successful!\n";
            cleanupPidFile();
            return 0;
        } else {
            std::cerr << "❌ Conversion failed!\n";
            cleanupPidFile();
            return 1;
        }
    }
    
    cleanupPidFile();
    return 0;
}
