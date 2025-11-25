#include "ImGuiRenderer.h"
#include "SIDLibConverter.h"
#include "HVSCDownloader.h"
#include <iostream>
#include <fstream>
#include <csignal>
#include <unistd.h>
#include <filesystem>

namespace fs = std::filesystem;

std::string pidFilePath;

void cleanupPidFile() {
    if (!pidFilePath.empty() && fs::exists(pidFilePath)) {
        fs::remove(pidFilePath);
        std::cout << "🗑️ PID-File entfernt\n";
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
            std::cout << "📋 PID: " << pid << " gespeichert\n";
        }
    }
}

int main(int argc, char** argv) {
    setupSignalHandlers();
    writePidFile();
    atexit(cleanupPidFile);
    
    std::cout << "🎵 SongGen - KI-gestützter Song-Generator\n";
    std::cout << "=========================================\n\n";
    
    // Headless mode für automatische HVSC-Konvertierung
    if (argc > 1 && std::string(argv[1]) == "--convert-hvsc") {
        std::cout << "🎵 Headless Mode: HVSC Konvertierung\n";
        HVSCDownloader downloader;
        std::string hvscPath = (argc > 2) ? argv[2] : "~/.songgen/hvsc/";
        
        downloader.downloadHVSC(hvscPath, [](size_t current, size_t total, float progress) {
            std::cout << "\r[" << current << "/" << total << "] " << (int)(progress * 100) << "%   " << std::flush;
        });
        
        cleanupPidFile();
        std::cout << "\n✅ Konvertierung abgeschlossen\n";
        return 0;
    }
    
    ImGuiRenderer renderer;
    
    if (!renderer.initialize()) {
        std::cerr << "❌ Failed to initialize renderer\n";
        cleanupPidFile();
        return 1;
    }
    
    renderer.run();
    renderer.shutdown();
    
    cleanupPidFile();
    std::cout << "\n✅ SongGen beendet\n";
    return 0;
}
