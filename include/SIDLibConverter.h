#ifndef SIDLIBCONVERTER_H
#define SIDLIBCONVERTER_H

#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <filesystem>
#include <sidplayfp/sidplayfp.h>
#include <sidplayfp/SidTune.h>
#include <sidplayfp/SidInfo.h>
#include <sidplayfp/SidConfig.h>
#include <sidplayfp/builders/residfp.h>

/**
 * SIDLibConverter - Schnelle C++ Library-basierte SID→WAV Konvertierung
 * 
 * VORTEILE gegenüber system("sidplayfp"):
 * - 10-50x schneller (kein Process-Overhead)
 * - Echte Parallelität (keine Process-Blockierung)
 * - Direkter Speicherzugriff
 * - Thread-Pool für maximale Performance
 */
class SIDLibConverter {
public:
    SIDLibConverter();
    ~SIDLibConverter();
    
    // SID-Timing Erkennung (PAL vs NTSC)
    enum Timing { PAL = 0, NTSC = 1, PAL_NTSC = 2, UNKNOWN = 3 };
    static Timing getTiming(const std::string& sidPath);
    
    /**
     * Ermittelt Anzahl der Subtunes in einer SID-Datei
     * @param sidPath Pfad zur SID-Datei
     * @return Anzahl der Subtunes (1 wenn nur ein Track)
     */
    static int getSubtuneCount(const std::string& sidPath);
    
    /**
     * Liest die Länge eines SID-Tracks aus (falls im Header angegeben)
     * @param sidPath Pfad zur SID-Datei
     * @param subtune Subtune-Nummer (1-basiert)
     * @return Länge in Sekunden (0 wenn nicht verfügbar, dann 120s Standard nutzen)
     */
    static int getTrackLength(const std::string& sidPath, int subtune = 1);
    
    /**
     * Misst die tatsächliche Länge eines Tracks durch Loop-Detection
     * @param sidPath Pfad zur SID-Datei
     * @param subtune Subtune-Nummer
     * @param maxSeconds Maximale Messzeit in Sekunden
     * @return Erkannte Loop-Position in Sekunden (0 wenn kein Loop erkannt)
     */
    int measureTrackLength(const std::string& sidPath, int subtune, int maxSeconds);
    
    /**
     * Testet ob ein SID hörbar ist (schneller 5s-Test)
     * @param sidPath Pfad zur SID-Datei
     * @param subtune Subtune-Nummer
     * @return true wenn hörbar, false wenn stumm
     */
    bool isAudible(const std::string& sidPath, int subtune = 0);
    
    /**
     * Konvertiert einzelnen SID zu WAV
     * @param sidPath Pfad zur SID-Datei
     * @param wavPath Ausgabe-WAV-Pfad
     * @param timeoutSec Maximale Länge in Sekunden
     * @param subtune Subtune-Nummer (0 = default/erstes)
     * @return true bei Erfolg
     */
    bool convertToWAV(const std::string& sidPath, const std::string& wavPath, int timeoutSec = 120, int subtune = 0);
    
    /**
     * Konvertiert einzelnen SID direkt zu MP3 (platzsparend!)
     * @param sidPath Pfad zur SID-Datei
     * @param mp3Path Ausgabe-MP3-Pfad
     * @param timeoutSec Maximale Länge in Sekunden
     * @param subtune Subtune-Nummer (0 = default/erstes)
     * @param bitrate MP3-Bitrate in kbps (128/192/256/320)
     * @return true bei Erfolg
     */
    bool convertToMP3(const std::string& sidPath, const std::string& mp3Path, int timeoutSec = 120, int subtune = 0, int bitrate = 192);
    
    /**
     * Berechnet MD5-Checksumme einer Datei
     * @param filepath Pfad zur Datei
     * @return MD5-Hash als Hex-String
     */
    static std::string calculateMD5(const std::string& filepath);
    
    /**
     * Validiert MP3-Datei (Checksumme + Format-Check)
     * @param mp3Path Pfad zur MP3-Datei
     * @return true wenn gültig
     */
    static bool validateMP3(const std::string& mp3Path);
    
    /**
     * Berechnet erwartete Dateigröße für Konvertierung
     * @param timeoutSec Länge in Sekunden
     * @param format "wav" oder "mp3"
     * @param bitrate MP3-Bitrate (nur für MP3)
     * @return Erwartete Größe in Bytes
     */
    static size_t calculateExpectedSize(int timeoutSec, const std::string& format = "wav", int bitrate = 192);
    
    /**
     * Batch-Konvertierung mit Thread-Pool (EMPFOHLEN für HVSC!)
     * @param sidFiles Liste der SID-Dateien
     * @param outputDir Ausgabe-Verzeichnis
     * @param threads Anzahl paralleler Threads (empfohlen: CPU-Kerne * 2-4)
     * @param timeoutSec Max. Länge pro SID
     * @param progressCallback Optional: Callback(completed, total)
     * @return Liste erfolgreich konvertierter WAV-Dateien
     */
    static std::vector<std::string> convertBatchParallel(
        const std::vector<std::string>& sidFiles,
        const std::string& outputDir,
        int threads,
        int timeoutSec = 120,
        std::function<void(int, int)> progressCallback = nullptr
    );
    
private:
    sidplayfp* engine;
    SidTune* tune;
    ReSIDfpBuilder* builder;
    
    bool loadROMs(const std::string& romPath);
    void writeWAVHeader(std::ofstream& file, int dataSize, int sampleRate, int channels, int bitsPerSample);
};

#endif // SIDLIBCONVERTER_H
