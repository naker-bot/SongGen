#include "HVSCDownloader.h"
#include "SIDLibConverter.h"
#include "MediaDatabase.h"
#include "AudioAnalyzer.h"
#include "ExtractConfig.h"
#include <curl/curl.h>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <filesystem>
#include <algorithm>
#include <vector>
#include <thread>
#include <atomic>
#include <memory>

namespace fs = std::filesystem;

HVSCDownloader::HVSCDownloader() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

HVSCDownloader::~HVSCDownloader() {
    curl_global_cleanup();
}

std::string HVSCDownloader::findFastestMirror() {
    // Prüfe Cache
    std::string cached = loadCachedMirror();
    if (!cached.empty()) {
        std::cout << "⚡ Verwende gecachten Mirror: " << cached << "\n";
        return cached;
    }
    
    std::cout << "🌐 Testing mirror speeds...\n";
    
    std::string fastestMirror = mirrors_[0];
    float fastestSpeed = 0.0f;
    
    for (const auto& mirror : mirrors_) {
        float speed = testMirrorSpeed(mirror);
        std::cout << "  " << mirror << ": " << speed << " KB/s\n";
        
        if (speed > fastestSpeed) {
            fastestSpeed = speed;
            fastestMirror = mirror;
        }
    }
    
    std::cout << "✅ Fastest mirror: " << fastestMirror << " (" << fastestSpeed << " KB/s)\n";
    saveFastestMirror(fastestMirror);
    return fastestMirror;
}

float HVSCDownloader::testMirrorSpeed(const std::string& mirrorUrl) {
    CURL* curl = curl_easy_init();
    if (!curl) return 0.0f;
    
    // Download ersten 100KB zum Testen
    std::string testUrl = mirrorUrl + "C64Music.zip";
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Callback der Download verwirft
    auto writeCallback = [](void* ptr, size_t size, size_t nmemb, void* data) -> size_t {
        return size * nmemb;  // Verwerfe Daten
    };
    
    curl_easy_setopt(curl, CURLOPT_URL, testUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +writeCallback);
    curl_easy_setopt(curl, CURLOPT_RANGE, "0-102400");  // Nur 100KB
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    
    CURLcode res = curl_easy_perform(curl);
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK || duration.count() == 0) {
        return 0.0f;
    }
    
    // KB/s berechnen
    float speed = 100.0f / (duration.count() / 1000.0f);
    return speed;
}

bool HVSCDownloader::downloadHVSC(
    const std::string& targetDir,
    std::function<void(size_t, size_t, float)> progressCallback,
    bool autoConvertAndImport,
    std::atomic<bool>* stopFlag) {
    
    std::cout << "📥 Downloading HVSC...\n";
    isDownloading_ = true;
    cancelRequested_ = false;
    
    // Expandiere Tilde im Pfad
    std::string expandedDir = targetDir;
    if (!expandedDir.empty() && expandedDir[0] == '~') {
        const char* home = getenv("HOME");
        if (home) {
            expandedDir = std::string(home) + expandedDir.substr(1);
        }
    }
    
    std::string mirror = findFastestMirror();
    
    // Automatische Erkennung der neuesten Datei
    std::string hvscFile = findLatestHVSCFile(mirror);
    if (hvscFile.empty()) {
        std::cerr << "❌ Konnte keine HVSC-Datei finden\n";
        hvscFile = "C64Music.zip";  // Fallback
    }
    
    std::string hvscUrl = mirror + hvscFile;
    std::string targetPath = expandedDir + "/" + hvscFile;
    
    std::cout << "📦 Download: " << hvscFile << "\n";
    std::cout << "📁 Ziel: " << expandedDir << "\n";
    
    // Erstelle Verzeichnis
    fs::create_directories(expandedDir);
    
    // Prüfe ob bereits heruntergeladen
    bool success = true;
    if (fs::exists(targetPath)) {
        std::cout << "⏭️ HVSC bereits vorhanden, überspringe Download\n";
        if (progressCallback) {
            progressCallback(1, 1, 0.0f);  // 100% Progress
        }
    } else {
        // Download
        success = downloadFile(hvscUrl, targetPath, progressCallback);
    }
    
    if (success && !cancelRequested_) {
        std::cout << "📦 Extracting HVSC archive...\n";
        success = extractArchive(targetPath, expandedDir);
        
        // Automatische Konvertierung und Import
        if (success && autoConvertAndImport) {
            std::cout << "🎵 Starte automatische SID-Konvertierung...\n";
            
            // Expandiere Pfad für verschachteltes Archiv
            std::string expandedDir = targetDir;
            if (!expandedDir.empty() && expandedDir[0] == '~') {
                const char* home = getenv("HOME");
                if (home) {
                    expandedDir = std::string(home) + expandedDir.substr(1);
                }
            }
            
            // Prüfe ob verschachteltes Archiv (C64Music.zip im Verzeichnis)
            std::string nestedArchive = expandedDir + "/C64Music.zip";
            if (fs::exists(nestedArchive)) {
                std::cout << "  Entpacke verschachteltes Archiv: C64Music.zip\n";
                extractArchive(nestedArchive, expandedDir);
            }
            
            // Finde SID-Verzeichnis rekursiv
            std::string foundSidDir;
            
            std::cout << "  🔍 Durchsuche: " << expandedDir << "\n";
            
            try {
                for (const auto& entry : fs::recursive_directory_iterator(expandedDir)) {
                    if (entry.is_directory()) {
                        std::string dirName = entry.path().filename().string();
                        // Suche nach typischen HVSC-Verzeichnissen
                        if (dirName == "C64Music" || dirName == "MUSICIANS" || 
                            dirName == "DEMOS" || dirName == "GAMES") {
                            foundSidDir = entry.path().string();
                            std::cout << "  ✅ SID-Verzeichnis gefunden: " << foundSidDir << "\n";
                            break;
                        }
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "❌ Fehler beim Durchsuchen: " << e.what() << "\n";
            }
            
            if (foundSidDir.empty()) {
                std::cerr << "⚠️ Konnte SID-Verzeichnis nicht finden. Überspringe Konvertierung.\n";
                std::cerr << "   Tipp: Nutze den 'SIDs extrahieren' Button im HVSC-Tab\n";
                return success;
            }
        
        std::string wavDir = expandedDir + "/mp3";

        // Hardware-abhängige Auto-Konfiguration für Threads & Timeout
        ExtractConfig cfg = autoTuneExtractConfig();

        // Konvertiere SIDs zu WAV (mit Progress-Callback)
        int optimalThreads = cfg.threads;
        size_t converted = extractSIDsToWAV(foundSidDir, wavDir, optimalThreads,
            [progressCallback](size_t current, size_t total) {
                if (progressCallback) {
                    progressCallback(current, total, 0.0f);
                }
            },
            stopFlag
        );
        
            std::cout << "✅ " << converted << " SID-Dateien konvertiert\n";
        }
    }
    
    isDownloading_ = false;
    return success;
}

struct DownloadProgress {
    std::function<void(size_t, size_t, float)> callback;
    std::chrono::high_resolution_clock::time_point startTime;
    size_t lastBytes = 0;
};

static int progressCallbackStatic(void* clientp, curl_off_t dltotal, curl_off_t dlnow, 
                                   curl_off_t ultotal, curl_off_t ulnow) {
    DownloadProgress* progress = static_cast<DownloadProgress*>(clientp);
    
    if (progress->callback && dltotal > 0) {
        auto now = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - progress->startTime);
        
        float speed = 0.0f;
        if (duration.count() > 0) {
            speed = (dlnow - progress->lastBytes) / (duration.count() / 1000.0f) / 1024.0f;  // KB/s
            progress->lastBytes = dlnow;
            progress->startTime = now;
        }
        
        progress->callback(dlnow, dltotal, speed);
    }
    
    return 0;
}

bool HVSCDownloader::downloadFile(
    const std::string& url,
    const std::string& targetPath,
    std::function<void(size_t, size_t, float)> progressCallback) {
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Failed to initialize CURL" << std::endl;
        return false;
    }
    
    FILE* fp = fopen(targetPath.c_str(), "wb");
    if (!fp) {
        curl_easy_cleanup(curl);
        return false;
    }
    
    DownloadProgress progress;
    progress.callback = progressCallback;
    progress.startTime = std::chrono::high_resolution_clock::now();
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallbackStatic);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress);
    
    CURLcode res = curl_easy_perform(curl);
    
    fclose(fp);
    curl_easy_cleanup(curl);
    
    return res == CURLE_OK;
}

bool HVSCDownloader::extractArchive(const std::string& archivePath, const std::string& targetDir) {
    std::cout << "📦 Extrahiere Archiv (optimiert)...\n";
    
    // Prüfe Archiv-Größe
    auto fileSize = fs::file_size(archivePath);
    std::cout << "  Archiv-Größe: " << (fileSize / (1024*1024)) << " MB\n";
    
    // Zähle CPU-Kerne für Multi-Threading
    unsigned int threads = std::thread::hardware_concurrency();
    if (threads == 0) threads = 4;  // Fallback
    
    // Nutze 7z mit Multi-Threading (-mmt) und Progress (-bsp1)
    std::string cmd = "7z x -y -mmt" + std::to_string(threads) + 
                      " -bsp1 -o\"" + targetDir + "\" \"" + archivePath + "\" 2>&1";
    
    std::cout << "  Threads: " << threads << "\n";
    std::cout << "  Starte Extraktion...\n";
    
    auto startTime = std::chrono::high_resolution_clock::now();
    int result = system(cmd.c_str());
    auto endTime = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime);
    
    if (result != 0) {
        std::cerr << "❌ 7z Extraktion fehlgeschlagen (Code: " << result << "), versuche unzip...\n";
        // Fallback zu unzip (nur für .zip)
        cmd = "unzip -q -o \"" + archivePath + "\" -d \"" + targetDir + "\" 2>&1";
        startTime = std::chrono::high_resolution_clock::now();
        result = system(cmd.c_str());
        endTime = std::chrono::high_resolution_clock::now();
        duration = std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime);
    }
    
    if (result == 0) {
        std::cout << "✅ Extraktion abgeschlossen in " << duration.count() << " Sekunden\n";
        
        // Validiere Output
        size_t fileCount = 0;
        size_t totalSize = 0;
        for (const auto& entry : fs::recursive_directory_iterator(targetDir)) {
            if (entry.is_regular_file()) {
                fileCount++;
                totalSize += entry.file_size();
            }
        }
        
        std::cout << "✅ Validierung: " << fileCount << " Dateien, " 
                  << (totalSize / (1024*1024)) << " MB extrahiert\n";
        
        if (fileCount == 0) {
            std::cerr << "❌ Warnung: Keine Dateien extrahiert!\n";
            return false;
        }
        
        // Update Songlengths.md5 Zeitstempel für automatisches Reload
        std::string songlengthsPath = targetDir + "/C64Music/DOCUMENTS/Songlengths.md5";
        if (fs::exists(songlengthsPath)) {
            // Setze Zeitstempel auf "jetzt" damit SIDLibConverter weiß dass neu geladen werden muss
            auto now = std::chrono::system_clock::now();
            auto now_time_t = std::chrono::system_clock::to_time_t(now);
            std::filesystem::last_write_time(songlengthsPath, 
                std::filesystem::file_time_type::clock::now());
            std::cout << "✅ Songlengths.md5 aktualisiert\n";
        }
    } else {
        std::cerr << "❌ Extraktion fehlgeschlagen nach " << duration.count() << " Sekunden\n";
    }
    
    return result == 0;
}

void HVSCDownloader::saveFastestMirror(const std::string& mirror) {
    std::string cacheFile = mirrorCacheFile_;
    if (cacheFile[0] == '~') {
        const char* home = getenv("HOME");
        if (home) {
            cacheFile = std::string(home) + cacheFile.substr(1);
        }
    }
    
    // Erstelle Verzeichnis
    fs::path path(cacheFile);
    fs::create_directories(path.parent_path());
    
    std::ofstream file(cacheFile);
    if (file.is_open()) {
        file << mirror << "\n";
        file.close();
        std::cout << "💾 Mirror gespeichert: " << cacheFile << "\n";
    }
}

std::string HVSCDownloader::loadCachedMirror() {
    std::string cacheFile = mirrorCacheFile_;
    if (cacheFile[0] == '~') {
        const char* home = getenv("HOME");
        if (home) {
            cacheFile = std::string(home) + cacheFile.substr(1);
        }
    }
    
    std::ifstream file(cacheFile);
    if (file.is_open()) {
        std::string mirror;
        std::getline(file, mirror);
        file.close();
        
        // Validiere ob Mirror noch in Liste
        for (const auto& m : mirrors_) {
            if (m == mirror) {
                return mirror;
            }
        }
    }
    return "";
}

std::string HVSCDownloader::findLatestHVSCFile(const std::string& mirrorUrl) {
    CURL* curl = curl_easy_init();
    if (!curl) return "";
    
    std::string htmlContent;
    
    auto writeCallback = [](void* ptr, size_t size, size_t nmemb, void* data) -> size_t {
        std::string* content = static_cast<std::string*>(data);
        content->append(static_cast<char*>(ptr), size * nmemb);
        return size * nmemb;
    };
    
    curl_easy_setopt(curl, CURLOPT_URL, mirrorUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &htmlContent);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        std::cerr << "❌ Konnte Mirror-Seite nicht laden\n";
        return "";
    }
    
    // Parse HTML nur für .zip Dateien (kein .7z wegen unzip-Limitation)
    std::vector<std::pair<std::string, size_t>> files;
    
    size_t pos = 0;
    while ((pos = htmlContent.find("href=\"", pos)) != std::string::npos) {
        pos += 6;
        size_t endPos = htmlContent.find("\"", pos);
        if (endPos == std::string::npos) break;
        
        std::string filename = htmlContent.substr(pos, endPos - pos);
        
        // Prüfe auf HVSC-Archive (NUR *.zip, kein .7z, kein Update)
        if (filename.find(".zip") != std::string::npos &&
            filename.find(".7z") == std::string::npos &&
            filename.find("Update") == std::string::npos &&
            filename.find("update") == std::string::npos &&
            (filename.find("HVSC") != std::string::npos || filename.find("C64") != std::string::npos ||
             filename.find("Music") != std::string::npos)) {
            
            // Versuche Größe zu finden (typisch in KB/MB nach filename)
            size_t size = 0;
            size_t sizePos = htmlContent.find(filename, pos);
            if (sizePos != std::string::npos) {
                // Suche nach Zahlen mit M/G nach dem Dateinamen
                std::string sizeStr = htmlContent.substr(sizePos, 200);
                size_t mPos = sizeStr.find('M');
                size_t gPos = sizeStr.find('G');
                
                if (gPos != std::string::npos && gPos < 100) {
                    // GB -> sehr groß
                    size = 1000000;  // Priorität für GB-Dateien
                } else if (mPos != std::string::npos && mPos < 100) {
                    // Parse MB
                    size = 1000;  // Annahme: größere Dateien sind neuer
                }
            }
            
            files.push_back({filename, size});
        }
        
        pos = endPos;
    }
    
    if (files.empty()) {
        std::cout << "⚠️ Keine HVSC-Dateien gefunden, verwende Standard\n";
        return "C64Music.zip";
    }
    
    // Sortiere nach Größe (größte zuerst)
    std::sort(files.begin(), files.end(), 
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    std::cout << "✅ Erkannte Datei: " << files[0].first << "\n";
    return files[0].first;
}

size_t HVSCDownloader::extractSIDsToWAV(
    const std::string& sidDir,
    const std::string& wavDir,
    int threads,
    std::function<void(size_t, size_t)> progressCallback,
    std::atomic<bool>* stopFlag) {
    
    std::cout << "🎵 Extracting SIDs to WAV...\n";
    
    // Expandiere Pfade (Tilde-Support)
    auto expandPath = [](const std::string& path) -> std::string {
        if (!path.empty() && path[0] == '~') {
            const char* home = getenv("HOME");
            if (home) {
                return std::string(home) + path.substr(1);
            }
        }
        return path;
    };
    
    std::string expandedSidDir = expandPath(sidDir);
    std::string expandedWavDir = expandPath(wavDir);
    
    std::cout << "  SID-Verzeichnis: " << expandedSidDir << "\n";
    std::cout << "  WAV-Ausgabe: " << expandedWavDir << "\n";
    
    // Sammle alle SID-Dateien (überspringe bereits konvertierte)
    std::vector<std::string> sidFiles;
    int skipped = 0;
    for (const auto& entry : fs::recursive_directory_iterator(expandedSidDir)) {
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            if (ext == ".sid" || ext == ".SID") {
                // Prüfe ob WAV oder MP3 bereits existiert
                std::string baseName = entry.path().stem().string();
                std::string wavPath = expandedWavDir + "/" + baseName + ".wav";
                std::string mp3Path = expandedWavDir + "/" + baseName + ".mp3";
                if (fs::exists(wavPath) || fs::exists(mp3Path)) {
                    skipped++;
                    continue;
                }
                sidFiles.push_back(entry.path().string());
            }
        }
    }
    
    if (skipped > 0) {
        std::cout << "⏭️ Überspringe " << skipped << " bereits konvertierte SIDs\n";
    }
    
    std::cout << "Found " << sidFiles.size() << " SID files\n";
    
    if (sidFiles.empty()) {
        std::cout << "⚠️ Keine neuen SIDs zum Konvertieren\n";
        return 0;
    }
    
    // Erstelle Ausgabe-Verzeichnis
    fs::create_directories(expandedWavDir);
    
    // SCHRITT 1: Scanne alle SIDs und erkenne Subtunes
    std::cout << "  🔍 Scanne Subtunes..." << std::flush;
    
    struct ConversionTask {
        std::string sidPath;
        std::string outputPath;  // Kann .wav oder .mp3 sein
        int subtune;
    };
    
    // Konfiguration: MP3 oder WAV?
    const bool USE_MP3 = true;  // MP3 spart ~90% Speicher!
    const int MP3_BITRATE = 192;  // kbps
    const std::string extension = USE_MP3 ? ".mp3" : ".wav";
    
    std::vector<ConversionTask> tasks;
    int skippedTasks = 0;
    for (const auto& sidPath : sidFiles) {
        int subtuneCount = SIDLibConverter::getSubtuneCount(sidPath);
        std::string baseName = fs::path(sidPath).stem().string();
        
        for (int sub = 1; sub <= subtuneCount; ++sub) {
            std::string fileName = baseName;
            if (subtuneCount > 1) {
                // Format: filename_01.mp3, filename_02.mp3
                char suffix[8];
                snprintf(suffix, sizeof(suffix), "_%02d", sub);
                fileName += suffix;
            }
            fileName += extension;
            
            std::string outputPath = (fs::path(expandedWavDir) / fileName).string();
            
            // Überspringe bereits konvertierte Subtunes
            if (fs::exists(outputPath)) {
                skippedTasks++;
                continue;
            }
            
            tasks.push_back({
                sidPath,
                outputPath,
                sub
            });
        }
    }
    
    if (skippedTasks > 0) {
        std::cout << "  ⏭️ Überspringe " << skippedTasks << " bereits konvertierte Tracks\n";
    }
    
    std::cout << "\r  ✅ Gefunden: " << tasks.size() << " Tracks aus " << sidFiles.size() << " SIDs\n";
    std::cout << "  📦 Format: " << (USE_MP3 ? "MP3" : "WAV") << " (" << (USE_MP3 ? std::to_string(MP3_BITRATE) + " kbps" : "16-bit PCM") << ")\n";
    
    std::cout << "\r  ✅ Gefunden: " << tasks.size() << " Tracks aus " << sidFiles.size() << " SIDs\n";
    
    // SCHRITT 2: Parallel-Konvertierung aller Tracks
    std::atomic<size_t> completed{0};
    std::atomic<size_t> lastReported{0};
    size_t total = tasks.size();
    std::vector<std::string> results;
    std::mutex resultMutex;
    
    auto liveProgress = [total, &lastReported](size_t current) {
        size_t last = lastReported.load(std::memory_order_relaxed);
        if (current - last >= 100 || current == total) {
            if (lastReported.compare_exchange_strong(last, current, std::memory_order_relaxed)) {
                float percent = (current * 100.0f) / total;
                std::cout << "\r  🎵 Konvertierung: " << current << "/" << total 
                          << " (" << std::fixed << std::setprecision(1) << percent << "%)     " << std::flush;
            }
        }
    };
    
    // Worker-Thread-Funktion
    // Auto-Tuning für Timeout bei der Konvertierung (hardwareabhängig)
    ExtractConfig cfg = autoTuneExtractConfig();

    auto worker = [&, USE_MP3, MP3_BITRATE](size_t start, size_t end) {
        SIDLibConverter converter;
        // AudioAnalyzer nur für WAV-Trimming nötig, nicht für MP3
        std::unique_ptr<AudioAnalyzer> analyzer;
        if (!USE_MP3) {
            analyzer = std::make_unique<AudioAnalyzer>();
        }
        const int MAX_RETRIES = 3;
        const size_t MIN_VALID_SIZE = USE_MP3 ? 1000 : 10000; // MP3: 1KB, WAV: 10KB minimum
        
        for (size_t i = start; i < end && i < tasks.size(); ++i) {
            const auto& task = tasks[i];
            bool success = false;
            
            // Lese tatsächliche Track-Länge aus SID-Header (falls vorhanden)
            int trackLength = SIDLibConverter::getTrackLength(task.sidPath, task.subtune);
            int actualTimeout = (trackLength > 0) ? trackLength : cfg.timeoutSec;
            
            // SCHRITT 2: Volle Konvertierung nur wenn hörbar
            for (int retry = 0; retry < MAX_RETRIES && !success; ++retry) {
                if (retry > 0) {
                    // Lösche fehlerhafte Datei vor Wiederholung
                    try {
                        if (fs::exists(task.outputPath)) {
                            fs::remove(task.outputPath);
                        }
                    } catch (...) {}
                }
                
                // Konvertierung durchführen (MP3 oder WAV) mit exakter Länge
                
                bool convSuccess = false;
                if (USE_MP3) {
                    convSuccess = converter.convertToMP3(task.sidPath, task.outputPath, actualTimeout, task.subtune, MP3_BITRATE);
                } else {
                    convSuccess = converter.convertToWAV(task.sidPath, task.outputPath, actualTimeout, task.subtune);
                }
                
                if (!convSuccess) {
                    continue; // Nächster Versuch
                }
                
                // Validiere: Datei muss existieren und > 0 Bytes haben
                std::error_code ec;
                if (!fs::exists(task.outputPath, ec) || ec) {
                    std::cerr << "[Fehler:0] Datei nicht erstellt: " << task.outputPath << "\n";
                    continue;
                }
                
                auto fileSize = fs::file_size(task.outputPath, ec);
                if (ec || fileSize < MIN_VALID_SIZE) {
                    try {
                        fs::remove(task.outputPath);
                    } catch (...) {}
                    continue;
                }
                
                // Stille-/Fehlererkennung nur für WAV (MP3 ist bereits komprimiert)
                if (!USE_MP3 && analyzer) {
                    if (!analyzer->detectSilenceAndTrimWav(task.outputPath, task.outputPath)) {
                        // Fehlerhaft → Datei verwerfen
                        try {
                            if (fs::exists(task.outputPath)) {
                                fs::remove(task.outputPath);
                            }
                        } catch (...) {}
                        success = false;
                        break; // Keine Wiederholung
                    }
                }
                
                // Erfolg!
                std::cerr << "[✓] Track erfolgreich extrahiert!\n";
                std::lock_guard<std::mutex> lock(resultMutex);
                results.push_back(task.outputPath);
                success = true;
            }
            
            if (!success) {
                std::cerr << "[Fehler:0] Konvertierung fehlgeschlagen nach " << MAX_RETRIES << " Versuchen: " << task.sidPath << "\n";
            }
            
            liveProgress(++completed);
            
            // Check Stop-Flag
            if (stopFlag && stopFlag->load()) {
                std::cerr << "\n⏹️ Extraktion gestoppt durch Benutzer\n";
                return;
            }
        }
    };
    
    // Starte Worker-Threads
    std::vector<std::thread> workers;
    size_t tasksPerThread = (tasks.size() + threads - 1) / threads;
    
    for (int t = 0; t < threads; ++t) {
        size_t start = t * tasksPerThread;
        size_t end = std::min(start + tasksPerThread, tasks.size());
        if (start < tasks.size()) {
            workers.emplace_back(worker, start, end);
        }
    }
    
    // Warte auf Completion
    for (auto& w : workers) {
        if (w.joinable()) w.join();
    }
    
    std::cout << "\n✅ Extracted " << results.size() << " Tracks\n";
    return results.size();
}

size_t HVSCDownloader::addToDatabase(
    const std::string& wavDir,
    MediaDatabase& db,
    bool analyzeImmediately) {
    
    std::cout << "💾 Füge WAVs zur Datenbank hinzu...\n";
    std::cout << "📁 Durchsuche Verzeichnis: " << wavDir << "\n";
    
    if (!fs::exists(wavDir)) {
        std::cerr << "❌ Verzeichnis existiert nicht: " << wavDir << "\n";
        return 0;
    }
    
    if (!fs::is_directory(wavDir)) {
        std::cerr << "❌ Kein Verzeichnis: " << wavDir << "\n";
        return 0;
    }
    
    size_t count = 0;
    size_t total = 0;
    
    for (const auto& entry : fs::recursive_directory_iterator(wavDir)) {
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            // Akzeptiere sowohl WAV als auch MP3
            if (ext == ".wav" || ext == ".mp3") {
                total++;
                MediaMetadata meta;
                meta.filepath = entry.path().string();
                meta.title = entry.path().stem().string();
                meta.genre = "SID";  // HVSC SIDs
                meta.artist = "C64";
                meta.analyzed = false;
                meta.addedTimestamp = std::time(nullptr);
                
                if (db.addMedia(meta)) {
                    count++;
                    if (count % 100 == 0) {
                        std::cout << "  Hinzugefügt: " << count << " / " << total << "\n";
                    }
                } else {
                    // Fehler beim Hinzufügen (z.B. Duplikat) - ignorieren
                }
            }
        }
    }
    
    std::cout << "✅ " << count << " von " << total << " Audio-Dateien zur Datenbank hinzugefügt\n";
    return count;
}

void HVSCDownloader::cancel() {
    cancelRequested_ = true;
}
