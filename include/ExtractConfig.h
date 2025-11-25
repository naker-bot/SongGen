#ifndef EXTRACTCONFIG_H
#define EXTRACTCONFIG_H

#include <thread>

struct ExtractConfig {
    int threads;
    int timeoutSec;
};

inline ExtractConfig autoTuneExtractConfig() {
    unsigned int cores = std::thread::hardware_concurrency();
    if (cores == 0) cores = 4;

    ExtractConfig cfg{};

    if (cores <= 4) {
        cfg.threads = static_cast<int>(cores);      // 1:1 auf kleiner CPU
        cfg.timeoutSec = 60;
    } else if (cores <= 8) {
        cfg.threads = static_cast<int>(cores);      // Nutze alle Kerne
        cfg.timeoutSec = 90;
    } else if (cores <= 16) {
        cfg.threads = 16;                           // Max 16 Threads für I/O-Balance
        cfg.timeoutSec = 120;
    } else {
        cfg.threads = 24;                           // Max 24 Threads bei vielen Kernen (I/O-Limit)
        cfg.timeoutSec = 180;
    }

    return cfg;
}

#endif // EXTRACTCONFIG_H
