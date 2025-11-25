#ifndef HVSCDOWNLOADER_H
#define HVSCDOWNLOADER_H

#include <string>
#include <vector>
#include <functional>
#include <atomic>

/**
 * HVSCDownloader - HVSC-Archiv Download und Management
 * 
 * Features:
 * - Download von HVSC aus dem Internet (hvsc.de Mirror)
 * - Multi-Mirror-Unterstützung mit Geschwindigkeitstest
 * - Automatische SID-Extraktion mit SIDLibConverter
 * - Fortschritts-Tracking
 * - Resume-Support
 */
class HVSCDownloader {
public:
    HVSCDownloader();
    ~HVSCDownloader();
    
    /**
     * Lädt HVSC-Archiv herunter
     * @param targetDir Ziel-Verzeichnis (Standard: ~/.songgen/hvsc/)
     * @param progressCallback Optional: callback(bytesDownloaded, totalBytes, currentSpeed)
     * @param autoConvertAndImport Automatisch SIDs konvertieren und importieren
     * @return true bei Erfolg
     */
    bool downloadHVSC(
        const std::string& targetDir = "~/.songgen/hvsc/",
        std::function<void(size_t, size_t, float)> progressCallback = nullptr,
        bool autoConvertAndImport = true,
        std::atomic<bool>* stopFlag = nullptr);
    
    /**
     * Testet Mirror-Geschwindigkeiten und wählt schnellsten
     * @return URL des schnellsten Mirrors
     */
    std::string findFastestMirror();
    
    /**
     * Extrahiert alle SIDs zu WAV mit SIDLibConverter
     * @param sidDir Verzeichnis mit SID-Dateien
     * @param wavDir Ausgabe-Verzeichnis für WAVs
     * @param threads Anzahl paralleler Threads
     * @param progressCallback Optional: callback(completed, total)
     * @return Anzahl erfolgreich konvertierter Dateien
     */
    size_t extractSIDsToWAV(
        const std::string& sidDir,
        const std::string& wavDir,
        int threads = 200,
        std::function<void(size_t, size_t)> progressCallback = nullptr,
        std::atomic<bool>* stopFlag = nullptr
    );
    
    /**
     * Fügt extrahierte WAVs zur Datenbank hinzu
     * @param wavDir Verzeichnis mit WAV-Dateien
     * @param db MediaDatabase-Instanz
     * @param analyzeImmediately Sofort analysieren oder später
     * @return Anzahl hinzugefügter Dateien
     */
    size_t addToDatabase(
        const std::string& wavDir,
        class MediaDatabase& db,
        bool analyzeImmediately = false
    );
    
    // Status
    bool isDownloading() const { return isDownloading_; }
    void cancel();
    
    // Automatische Datei-Erkennung
    std::string findLatestHVSCFile(const std::string& mirrorUrl);
    
private:
    bool isDownloading_ = false;
    bool cancelRequested_ = false;
    
    std::vector<std::string> mirrors_ = {
        "https://www.hvsc.c64.org/downloads/",
        "https://kohina.duckdns.org/HVSC/",
        "http://www.c64.sk/hvsc/",
    };
    
    std::string cachedFastestMirror_;
    std::string mirrorCacheFile_ = "~/.songgen/mirror_cache.txt";
    
    float testMirrorSpeed(const std::string& mirrorUrl);
    bool downloadFile(const std::string& url, const std::string& targetPath,
                      std::function<void(size_t, size_t, float)> progressCallback);
    bool extractArchive(const std::string& archivePath, const std::string& targetDir);
    
    void saveFastestMirror(const std::string& mirror);
    std::string loadCachedMirror();
};

#endif // HVSCDOWNLOADER_H
