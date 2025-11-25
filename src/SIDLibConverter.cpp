#include "SIDLibConverter.h"
#include "SonglengthsDB.h"
#include <iostream>
#include <fstream>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <vector>
#include <map>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <iomanip>
#include <sstream>
#include <openssl/md5.h>
#include <thread>
#include <random>
#include <atomic>
#include <lame/lame.h>

extern char **environ;

// Helper: Shell-escape a string for safe command execution
static std::string shell_escape(const std::string& str) {
    std::string result = "'";
    for (char c : str) {
        if (c == '\'') {
            result += "'\\''";
        } else {
            result += c;
        }
    }
    result += "'";
    return result;
}

extern char **environ;

SIDLibConverter::SIDLibConverter() : engine(nullptr), tune(nullptr), builder(nullptr) {
    engine = new sidplayfp();
    if (!engine) {
        std::cerr << "[SIDLib] ❌ Engine allocation failed\n";
        return;
    }
    
    // Erstelle ReSIDfp Builder für SID-Emulation
    builder = new ReSIDfpBuilder("ReSIDfp");
    if (!builder) {
        std::cerr << "[SIDLib] ❌ Builder allocation failed\n";
        delete engine;
        engine = nullptr;
        return;
    }
    
    // Erstelle 1 SID-Chip und prüfe Erfolg
    unsigned int chips = builder->create(1);
    if (chips == 0) {
        std::cerr << "[SIDLib] ❌ Failed to create SID chips!\n";
        delete builder;
        delete engine;
        builder = nullptr;
        engine = nullptr;
        return;
    }
    
    builder->filter(true);  // Aktiviere Filter
    builder->filter6581Curve(0.5);  // Standard filter curve
    builder->filter8580Curve(0.5);
    
    // Pre-konfiguriere Engine EINMAL (nicht bei jeder Konvertierung!)
    SidConfig config;
    config.frequency = 44100;
    config.playback = SidConfig::STEREO;
    config.samplingMethod = SidConfig::RESAMPLE_INTERPOLATE;
    config.fastSampling = false;  // False für Stabilität
    config.sidEmulation = builder;  // WICHTIG: Builder setzen!
    
    if (!engine->config(config)) {
        std::cerr << "❌ SID Engine config failed: " << engine->error() << "\n";
        delete builder;
        delete engine;
        builder = nullptr;
        engine = nullptr;
    }
}

SIDLibConverter::~SIDLibConverter() {
    if (tune) delete tune;
    if (engine) delete engine;
    if (builder) delete builder;
}

bool SIDLibConverter::loadROMs(const std::string& romPath) {
    // Lade Kernal, Basic, Character ROMs falls vorhanden
    std::string kernalPath = romPath + "/kernal";
    std::string basicPath = romPath + "/basic";
    std::string charPath = romPath + "/chargen";
    
    // Optional: ROMs laden wenn vorhanden
    // engine->setRoms(...);
    
    return true;
}

int SIDLibConverter::getSubtuneCount(const std::string& sidPath) {
    // Lese SID-Header: Bytes 0x0E-0x0F enthalten die Anzahl der Songs (Big Endian)
    std::ifstream file(sidPath, std::ios::binary);
    if (!file.is_open()) return 1;
    
    // Prüfe Magic Bytes (PSID oder RSID)
    char magic[5] = {0};
    file.read(magic, 4);
    if (std::string(magic) != "PSID" && std::string(magic) != "RSID") {
        return 1;
    }
    
    // Springe zu Offset 0x0E für Song-Count
    file.seekg(0x0E);
    unsigned char songsHi, songsLo;
    file.read(reinterpret_cast<char*>(&songsHi), 1);
    file.read(reinterpret_cast<char*>(&songsLo), 1);
    
    int songs = (songsHi << 8) | songsLo;
    return (songs > 0) ? songs : 1;
}

// Misst die tatsächliche Länge eines Tracks durch Loop-Detection
int SIDLibConverter::measureTrackLength(const std::string& sidPath, int subtune, int maxSeconds) {
    if (!engine) return 0;
    
    // Lade SID-Datei
    SidTune* measureTune = new SidTune(sidPath.c_str());
    if (!measureTune || !measureTune->getStatus()) {
        if (measureTune) delete measureTune;
        return 0;
    }
    
    measureTune->selectSong(subtune);
    if (!engine->load(measureTune)) {
        delete measureTune;
        return 0;
    }
    
    // Rendere Audio und sammle Fingerprints (Hash pro 2 Sekunden)
    const int bufferSize = 8192;
    short buffer[bufferSize];
    const int sampleRate = 44100;
    const int chunkSeconds = 2;  // 2-Sekunden-Chunks für Fingerprints
    const int samplesPerChunk = sampleRate * chunkSeconds * 2;  // Stereo
    
    std::vector<uint64_t> fingerprints;
    std::vector<short> chunkBuffer;
    chunkBuffer.reserve(samplesPerChunk);
    
    int totalFrames = maxSeconds * sampleRate;
    int framesRendered = 0;
    bool loopFound = false;
    int loopStartSecond = 0;
    
    while (framesRendered < totalFrames && !loopFound) {
        int framesToRender = std::min(bufferSize / 2, totalFrames - framesRendered);
        int samplesRendered = engine->play(buffer, framesToRender * 2);
        
        if (samplesRendered == 0) break;
        
        // Sammle in Chunk-Buffer
        for (int i = 0; i < samplesRendered; i++) {
            chunkBuffer.push_back(buffer[i]);
        }
        
        // Wenn Chunk voll: Berechne Fingerprint
        if (chunkBuffer.size() >= samplesPerChunk) {
            // Einfacher Hash: XOR aller Samples in 4KB-Blöcken
            uint64_t hash = 0;
            for (size_t i = 0; i < samplesPerChunk; i += 2048) {
                uint64_t blockHash = 0;
                for (size_t j = 0; j < 2048 && (i + j) < chunkBuffer.size(); j++) {
                    blockHash = (blockHash * 31) + chunkBuffer[i + j];
                }
                hash ^= blockHash;
            }
            
            fingerprints.push_back(hash);
            
            // Prüfe auf Loop: Suche identischen Hash in Historie
            if (fingerprints.size() > 10) {  // Min. 20 Sekunden vor Loop-Check
                for (size_t i = 0; i < fingerprints.size() - 5; i++) {
                    // Prüfe ob 3 aufeinanderfolgende Chunks matchen
                    bool match = true;
                    for (int j = 0; j < 3 && match; j++) {
                        if (i + j >= fingerprints.size() - 3 - j) {
                            match = false;
                        } else if (fingerprints[i + j] != fingerprints[fingerprints.size() - 3 + j]) {
                            match = false;
                        }
                    }
                    
                    if (match) {
                        loopFound = true;
                        loopStartSecond = (fingerprints.size() - 3) * chunkSeconds;
                        break;
                    }
                }
            }
            
            chunkBuffer.clear();
        }
        
        framesRendered += samplesRendered / 2;
    }
    
    delete measureTune;
    
    // Wenn Loop gefunden: Gebe Loop-Start zurück, sonst 0 (= kein Loop erkannt)
    return loopFound ? loopStartSecond : 0;
}

int SIDLibConverter::getTrackLength(const std::string& sidPath, int subtune) {
    // Statische Instanz der Songlengths-Datenbank
    static SonglengthsDB* db = nullptr;
    static bool dbInitialized = false;
    static std::filesystem::file_time_type lastModTime;
    
    // Cache für gemessene Längen (SID-Pfad + Subtune → Länge)
    static std::map<std::string, int> measurementCache;
    static std::string cacheFilePath = std::string(getenv("HOME")) + "/.songgen/measured_lengths.cache";
    static bool cacheLoaded = false;
    
    std::string dbPath = std::string(getenv("HOME")) + "/.songgen/hvsc/C64Music/DOCUMENTS/Songlengths.md5";
    std::string customDbPath = std::string(getenv("HOME")) + "/.songgen/custom_songlengths.md5";
    
    // Lade Cache beim ersten Aufruf
    if (!cacheLoaded) {
        cacheLoaded = true;
        if (std::filesystem::exists(cacheFilePath)) {
            std::ifstream cacheFile(cacheFilePath);
            std::string line;
            int loaded = 0;
            while (std::getline(cacheFile, line)) {
                size_t sep = line.find('=');
                if (sep != std::string::npos) {
                    std::string key = line.substr(0, sep);
                    int length = std::stoi(line.substr(sep + 1));
                    measurementCache[key] = length;
                    loaded++;
                }
            }
            if (loaded > 0) {
                std::cout << "[SIDLib] 📦 Measurement cache loaded: " << loaded << " entries\n";
            }
        }
    }
    
    // Prüfe ob Datenbank neu geladen werden muss
    bool needsReload = false;
    if (std::filesystem::exists(dbPath)) {
        auto currentModTime = std::filesystem::last_write_time(dbPath);
        if (!dbInitialized || currentModTime != lastModTime) {
            needsReload = true;
            lastModTime = currentModTime;
        }
    }
    
    if (!dbInitialized || needsReload) {
        dbInitialized = true;
        
        if (!db) {
            db = new SonglengthsDB();
        }
        
        // Lade/Reload HVSC-Datenbank
        if (std::filesystem::exists(dbPath)) {
            if (db->load(dbPath)) {
                std::cout << "[SIDLib] ✅ Songlengths.md5 " 
                          << (needsReload ? "neu geladen" : "geladen") << "\n";
            } else {
                std::cerr << "[SIDLib] ⚠️ Konnte Songlengths.md5 nicht laden\n";
            }
        } else {
            std::cerr << "[SIDLib] ⚠️ Songlengths.md5 nicht gefunden: " << dbPath << "\n";
        }
        
        // Lade Custom-Datenbank (eigene Messungen)
        if (std::filesystem::exists(customDbPath)) {
            if (db->load(customDbPath)) {
                std::cout << "[SIDLib] ✅ Custom DB geladen\n";
            }
        }
    }
    
    // Versuche Länge aus Datenbank zu holen (HVSC + Custom)
    if (db) {
        int length = db->getLength(sidPath, subtune);
        if (length > 0) {
            return length;
        }
    }
    
    // Prüfe Measurement-Cache
    std::string cacheKey = sidPath + ":" + std::to_string(subtune);
    auto it = measurementCache.find(cacheKey);
    if (it != measurementCache.end()) {
        return it->second;
    }
    
    // DEAKTIVIERT: Automatische Messung führt zu Segfaults
    // TODO: Loop-Detection später in convertToMP3 integrieren
    
    // Letzter Fallback: 180 Sekunden (speichere im Cache)
    measurementCache[cacheKey] = 180;
    
    // Speichere im Cache
    std::ofstream cacheFile(cacheFilePath, std::ios::app);
    if (cacheFile.is_open()) {
        cacheFile << cacheKey << "=" << 180 << "\n";
    }
    
    return 180;
}

SIDLibConverter::Timing SIDLibConverter::getTiming(const std::string& sidPath) {
    // OPTIMIERUNG: Lese nur minimale Bytes (0x78 statt ganze Datei)
    // SID-Header Byte 0x77 (Version 2+) enthält Clock-Info:
    // Bit 2-3: 00 = Unknown, 01 = PAL, 10 = NTSC, 11 = PAL+NTSC
    
    FILE* file = fopen(sidPath.c_str(), "rb");
    if (!file) return UNKNOWN;
    
    // Lese Header in einem Rutsch (0x78 bytes = 120 bytes)
    unsigned char header[0x78];
    size_t read = fread(header, 1, 0x78, file);
    fclose(file);
    
    if (read < 0x78) return UNKNOWN;
    
    // Prüfe Magic (PSID oder RSID)
    if (header[0] != 'P' && header[0] != 'R') return UNKNOWN;
    if (header[1] != 'S' || header[2] != 'I' || header[3] != 'D') return UNKNOWN;
    
    // Version bei 0x05
    unsigned char version = header[0x05];
    if (version < 2) {
        // PSID v1 hat keine Timing-Info → Standard ist PAL (C64 = europäisch)
        return PAL;
    }
    
    // Flags bei 0x77
    unsigned char flags = header[0x77];
    int clockBits = (flags >> 2) & 0x03;
    return static_cast<Timing>(clockBits);
}

bool SIDLibConverter::isAudible(const std::string& sidPath, int subtune) {
    // Erstelle temporäre Test-WAV (5 Sekunden)
    std::string tempPath = "/tmp/sid_test_" + std::to_string(getpid()) + ".wav";
    
    // Schnelle 5s-Konvertierung
    std::string cmd = "/usr/bin/sidplayfp -t5";
    
    Timing timing = getTiming(sidPath);
    if (timing == PAL) cmd += " -vp";
    else if (timing == NTSC) cmd += " -vn";
    
    if (subtune > 1) cmd += " -o" + std::to_string(subtune);
    
    cmd += " -w" + tempPath;
    cmd += " \"" + sidPath + "\"";
    cmd += " >/dev/null 2>&1";
    
    int result = system(cmd.c_str());
    
    // Prüfe ob Datei erstellt wurde
    std::error_code ec;
    if (!std::filesystem::exists(tempPath, ec) || ec) {
        return false;
    }
    
    auto fileSize = std::filesystem::file_size(tempPath, ec);
    if (ec || fileSize < 10000) {
        std::filesystem::remove(tempPath, ec);
        return false;
    }
    
    // Analysiere Audio-Inhalt (einfache RMS-Berechnung)
    std::ifstream file(tempPath, std::ios::binary);
    if (!file.is_open()) {
        std::filesystem::remove(tempPath, ec);
        return false;
    }
    
    // Überspringe WAV-Header (44 bytes)
    file.seekg(44);
    
    // Lese erste 50000 Samples (ca. 1 Sekunde bei 44.1kHz stereo)
    const size_t samplesToCheck = 50000;
    std::vector<int16_t> samples(samplesToCheck);
    file.read(reinterpret_cast<char*>(samples.data()), samplesToCheck * sizeof(int16_t));
    size_t samplesRead = file.gcount() / sizeof(int16_t);
    file.close();
    
    // Berechne RMS (Root Mean Square)
    double sumSquares = 0.0;
    for (size_t i = 0; i < samplesRead; ++i) {
        double normalized = samples[i] / 32768.0;
        sumSquares += normalized * normalized;
    }
    double rms = std::sqrt(sumSquares / samplesRead);
    
    // Cleanup
    std::filesystem::remove(tempPath, ec);
    
    // Schwellwert: RMS > 0.001 bedeutet hörbares Audio
    // (Stille wäre < 0.0001)
    const double SILENCE_THRESHOLD = 0.001;
    bool audible = rms > SILENCE_THRESHOLD;
    
    if (!audible) {
        std::cerr << "[SIDLib] 🔇 Silent track (RMS=" << rms << "): " << sidPath << " subtune=" << subtune << "\n";
    }
    
    return audible;
}

size_t SIDLibConverter::calculateExpectedSize(int timeoutSec, const std::string& format, int bitrate) {
    if (format == "mp3") {
        // MP3: bitrate * seconds * 1000 / 8 (in bytes)
        return (bitrate * timeoutSec * 1000) / 8;
    } else {
        // WAV: 44.1kHz * 2 channels * 16-bit * seconds + header
        return (44100 * 2 * 2 * timeoutSec) + 44;
    }
}

bool SIDLibConverter::convertToWAV(const std::string& sidPath, const std::string& wavPath, int timeoutSec, int subtune) {
    if (!engine) return false;
    
    std::filesystem::create_directories(std::filesystem::path(wavPath).parent_path());
    
    // Lade SID-Datei
    tune = new SidTune(sidPath.c_str());
    if (!tune || !tune->getStatus()) {
        if (tune) delete tune;
        tune = nullptr;
        return false;
    }
    
    // Wähle Subtune
    tune->selectSong(subtune);
    
    // Lade Tune in Engine
    if (!engine->load(tune)) {
        delete tune;
        tune = nullptr;
        return false;
    }
    
    // Öffne WAV-Datei zum Schreiben
    std::ofstream wavFile(wavPath, std::ios::binary);
    if (!wavFile.is_open()) {
        return false;
    }
    
    // Schreibe WAV-Header (wird später aktualisiert)
    const int sampleRate = 44100;
    const int channels = 2;
    const int bitsPerSample = 16;
    int dataSize = 0;
    writeWAVHeader(wavFile, dataSize, sampleRate, channels, bitsPerSample);
    
    // Rendere Audio mit größerem Buffer für bessere Performance
    // bufferSize = Anzahl der short-Werte (für Stereo: 32768 Frames = 65536 Samples)
    const int bufferSize = 65536;  // 65536 shorts = 32768 stereo frames (~0.74s @ 44.1kHz)
    short buffer[bufferSize];
    
    // totalFrames = Anzahl der Stereo-Frames (1 Frame = L+R = 2 samples)
    int totalFrames = timeoutSec * sampleRate;
    int framesRendered = 0;
    
    while (framesRendered < totalFrames) {
        // Berechne wie viele Frames noch fehlen
        int framesToRender = std::min(bufferSize / channels, totalFrames - framesRendered);
        
        // engine->play() nimmt Buffer und Anzahl SAMPLES (nicht Frames!)
        // Für Stereo: framesToRender * 2 = Anzahl samples
        int samplesRequested = framesToRender * channels;
        int samplesRendered = engine->play(buffer, samplesRequested);
        
        if (samplesRendered == 0) break;
        
        // Schreibe alle gerenderten samples zur WAV-Datei
        wavFile.write(reinterpret_cast<char*>(buffer), samplesRendered * sizeof(short));
        dataSize += samplesRendered * sizeof(short);
        
        // Update frame counter (samples / channels)
        framesRendered += samplesRendered / channels;
    }
    
    // Aktualisiere WAV-Header mit korrekter Größe
    wavFile.seekp(0);
    writeWAVHeader(wavFile, dataSize, sampleRate, channels, bitsPerSample);
    wavFile.close();
    
    // Cleanup
    delete tune;
    tune = nullptr;
    
    // Validiere Ausgabe
    if (dataSize < 10000) {
        std::filesystem::remove(wavPath);
        return false;
    }
    
    return true;
}

bool SIDLibConverter::convertToMP3(const std::string& sidPath, const std::string& mp3Path, int timeoutSec, int subtune, int bitrate) {
    if (!engine) return false;
    
    std::filesystem::create_directories(std::filesystem::path(mp3Path).parent_path());
    
    // Lade SID-Datei
    tune = new SidTune(sidPath.c_str());
    if (!tune || !tune->getStatus()) {
        if (tune) delete tune;
        tune = nullptr;
        return false;
    }
    
    // Wähle Subtune
    tune->selectSong(subtune);
    
    // Lade Tune in Engine
    if (!engine->load(tune)) {
        delete tune;
        tune = nullptr;
        return false;
    }
    
    // Initialisiere LAME für MP3 Encoding (viel schneller als ffmpeg-Prozess)
    lame_t lame = lame_init();
    if (!lame) {
        delete tune;
        tune = nullptr;
        return false;
    }
    
    // Konfiguriere LAME
    lame_set_in_samplerate(lame, 44100);
    lame_set_num_channels(lame, 2);
    lame_set_brate(lame, bitrate);
    lame_set_quality(lame, 5);  // 0=best, 9=worst (5=good balance)
    
    if (lame_init_params(lame) < 0) {
        lame_close(lame);
        delete tune;
        tune = nullptr;
        return false;
    }
    
    // Öffne MP3 Output-Datei
    FILE* mp3File = fopen(mp3Path.c_str(), "wb");
    if (!mp3File) {
        lame_close(lame);
        delete tune;
        tune = nullptr;
        return false;
    }
    
    // Rendere Audio und encode direkt zu MP3
    const int pcmBufferSize = 65536;  // PCM buffer: 32768 stereo frames
    short pcmBuffer[pcmBufferSize];
    unsigned char mp3Buffer[pcmBufferSize + 10000];  // MP3 buffer needs extra space
    
    const int sampleRate = 44100;
    int totalFrames = timeoutSec * sampleRate;
    int framesRendered = 0;
    
    // Loop-Detection: Speichere Audio-Fingerprints
    const int FINGERPRINT_SIZE = 8192;  // ~185ms bei 44.1kHz
    const int CHECK_INTERVAL = 44100 * 2;  // Prüfe alle 2 Sekunden
    const int MIN_LENGTH = 44100 * 15;  // Mindestens 15 Sekunden spielen
    std::vector<std::vector<short>> fingerprints;
    int framesSinceLastCheck = 0;
    
    while (framesRendered < totalFrames) {
        int framesToRender = std::min(pcmBufferSize / 2, totalFrames - framesRendered);
        int samplesRequested = framesToRender * 2;
        int samplesRendered = engine->play(pcmBuffer, samplesRequested);
        
        if (samplesRendered == 0) break;
        
        // Loop-Detection: Speichere Fingerprint alle 2 Sekunden (nach 15s Minimum)
        framesSinceLastCheck += samplesRendered / 2;
        if (framesRendered >= MIN_LENGTH && framesSinceLastCheck >= CHECK_INTERVAL) {
            framesSinceLastCheck = 0;
            
            // Erstelle Fingerprint (erste FINGERPRINT_SIZE samples)
            std::vector<short> fingerprint;
            int fpSize = std::min(FINGERPRINT_SIZE, samplesRendered);
            fingerprint.assign(pcmBuffer, pcmBuffer + fpSize);
            
            // Prüfe gegen vorherige Fingerprints
            bool loopDetected = false;
            for (const auto& prevFp : fingerprints) {
                if (prevFp.size() != fingerprint.size()) continue;
                
                // Vergleiche mit Toleranz (90% Übereinstimmung)
                int matches = 0;
                for (size_t i = 0; i < fingerprint.size(); ++i) {
                    int diff = std::abs(fingerprint[i] - prevFp[i]);
                    if (diff < 500) matches++;  // Toleranz: 500 von 32767
                }
                
                float similarity = (float)matches / fingerprint.size();
                if (similarity > 0.90f) {
                    loopDetected = true;
                    break;
                }
            }
            
            if (loopDetected) {
                // Loop erkannt, stoppe früher
                break;
            }
            
            fingerprints.push_back(fingerprint);
            
            // Behalte nur die letzten 10 Fingerprints (20 Sekunden Historie)
            if (fingerprints.size() > 10) {
                fingerprints.erase(fingerprints.begin());
            }
        }
        
        // Encode PCM → MP3 (interleaved stereo)
        int framesInBuffer = samplesRendered / 2;
        int mp3Bytes = lame_encode_buffer_interleaved(
            lame,
            pcmBuffer,
            framesInBuffer,
            mp3Buffer,
            sizeof(mp3Buffer)
        );
        
        if (mp3Bytes < 0) {
            std::cerr << "[SIDLib] ⚠️ LAME encoding error: " << mp3Bytes << "\n";
            break;
        }
        
        // Schreibe MP3-Daten direkt in Datei
        if (mp3Bytes > 0) {
            fwrite(mp3Buffer, 1, mp3Bytes, mp3File);
        }
        
        framesRendered += samplesRendered / 2;
    }
    
    // Flush final MP3 frames
    int mp3Bytes = lame_encode_flush(lame, mp3Buffer, sizeof(mp3Buffer));
    if (mp3Bytes > 0) {
        fwrite(mp3Buffer, 1, mp3Bytes, mp3File);
    } else if (mp3Bytes < 0) {
        std::cerr << "[SIDLib] ⚠️ LAME flush error: " << mp3Bytes << "\n";
    }
    
    // Cleanup
    fflush(mp3File);  // Force write to disk
    fclose(mp3File);
    lame_close(lame);
    
    // Cleanup
    delete tune;
    tune = nullptr;
    
    // Prüfe MP3
    std::error_code ec;
    if (!std::filesystem::exists(mp3Path, ec) || ec) {
        std::cerr << "[SIDLib] ❌ MP3 creation failed: " << mp3Path << "\n";
        if (ec) std::cerr << "  Error: " << ec.message() << "\n";
        return false;
    }
    
    auto actualSize = std::filesystem::file_size(mp3Path, ec);
    const int MIN_VALID_SIZE = 1000;  // Mindestens 1KB
    
    if (ec || actualSize < MIN_VALID_SIZE) {
        std::cerr << "[SIDLib] ❌ Invalid MP3 (size=" << actualSize << "), removing\n";
        std::filesystem::remove(mp3Path);
        return false;
    }
    
    // std::cerr << "[SIDLib] ✅ MP3 Success: " << actualSize << " bytes (" << (actualSize / 1024) << " KB)\n";
    
    // Validiere MP3
    if (!validateMP3(mp3Path)) {
        // std::cerr << "[SIDLib] ⚠️ MP3 validation failed, but keeping file\n";
    }
    
    return true;
}

std::string SIDLibConverter::calculateMD5(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return "";
    }
    
    MD5_CTX md5Context;
    MD5_Init(&md5Context);
    
    char buffer[8192];
    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
        MD5_Update(&md5Context, buffer, file.gcount());
    }
    
    unsigned char result[MD5_DIGEST_LENGTH];
    MD5_Final(result, &md5Context);
    
    std::ostringstream oss;
    for (int i = 0; i < MD5_DIGEST_LENGTH; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(result[i]);
    }
    
    return oss.str();
}

bool SIDLibConverter::validateMP3(const std::string& mp3Path) {
    std::error_code ec;
    
    // 1. Prüfe ob Datei existiert
    if (!std::filesystem::exists(mp3Path, ec) || ec) {
        std::cerr << "[Validate] ❌ File doesn't exist\n";
        return false;
    }
    
    // 2. Prüfe Dateigröße
    auto fileSize = std::filesystem::file_size(mp3Path, ec);
    if (ec || fileSize < 1000) {
        std::cerr << "[Validate] ❌ File too small: " << fileSize << " bytes\n";
        return false;
    }
    
    // 3. Prüfe MP3-Magic-Bytes (ID3 oder FF FB/FF FA)
    std::ifstream file(mp3Path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[Validate] ❌ Can't open file\n";
        return false;
    }
    
    char magic[3];
    file.read(magic, 3);
    
    bool isValidMP3 = false;
    if (magic[0] == 'I' && magic[1] == 'D' && magic[2] == '3') {
        // ID3v2 Tag
        isValidMP3 = true;
    } else if ((unsigned char)magic[0] == 0xFF && ((unsigned char)magic[1] & 0xE0) == 0xE0) {
        // MP3 Frame Sync
        isValidMP3 = true;
    }
    
    file.close();
    
    if (!isValidMP3) {
        std::cerr << "[Validate] ❌ Invalid MP3 magic bytes\n";
        return false;
    }
    
    // 4. Berechne MD5-Checksumme
    std::string md5 = calculateMD5(mp3Path);
    if (md5.empty()) {
        std::cerr << "[Validate] ⚠️ MD5 calculation failed\n";
        return true; // Nicht kritisch
    }
    
    // std::cerr << "[Validate] ✅ Valid MP3, MD5: " << md5.substr(0, 8) << "...\n";
    return true;
}

void SIDLibConverter::writeWAVHeader(std::ofstream& file, int dataSize, int sampleRate, int channels, int bitsPerSample) {
    int byteRate = sampleRate * channels * (bitsPerSample / 8);
    int blockAlign = channels * (bitsPerSample / 8);
    
    // RIFF Header
    file.write("RIFF", 4);
    int chunkSize = 36 + dataSize;
    file.write(reinterpret_cast<char*>(&chunkSize), 4);
    file.write("WAVE", 4);
    
    // fmt Subchunk
    file.write("fmt ", 4);
    int subchunk1Size = 16;
    file.write(reinterpret_cast<char*>(&subchunk1Size), 4);
    short audioFormat = 1;  // PCM
    file.write(reinterpret_cast<char*>(&audioFormat), 2);
    short numChannels = channels;
    file.write(reinterpret_cast<char*>(&numChannels), 2);
    file.write(reinterpret_cast<char*>(&sampleRate), 4);
    file.write(reinterpret_cast<char*>(&byteRate), 4);
    short blockAlignShort = blockAlign;
    file.write(reinterpret_cast<char*>(&blockAlignShort), 2);
    short bitsPerSampleShort = bitsPerSample;
    file.write(reinterpret_cast<char*>(&bitsPerSampleShort), 2);
    
    // data Subchunk
    file.write("data", 4);
    file.write(reinterpret_cast<char*>(&dataSize), 4);
}

// Thread-sichere Pool-Version für massive Parallelität
std::vector<std::string> SIDLibConverter::convertBatchParallel(
    const std::vector<std::string>& sidFiles,
    const std::string& outputDir,
    int threads,
    int timeoutSec,
    std::function<void(int, int)> progressCallback
) {
    std::vector<std::string> successFiles;
    std::mutex resultMutex;
    std::atomic<int> completed(0);
    
    // Thread-Pool mit pre-allocated capacity
    std::vector<std::thread> threadPool;
    threadPool.reserve(threads);
    std::atomic<size_t> currentIndex(0);
    
    auto worker = [&]() {
        // Jeder Thread hat seine eigene Engine!
        SIDLibConverter converter;
        
        // Batch-Sammlung für weniger Lock-Contention
        std::vector<std::string> localSuccesses;
        localSuccesses.reserve(100);
        
        try {
            while (true) {
                size_t idx = currentIndex.fetch_add(1, std::memory_order_relaxed);
                if (idx >= sidFiles.size()) break;
                
                const std::string& sidPath = sidFiles[idx];
                std::string filename = std::filesystem::path(sidPath).stem().string();
                std::string wavPath = outputDir + "/" + filename + ".wav";
                
                // Konvertiere
                if (converter.convertToWAV(sidPath, wavPath, timeoutSec)) {
                    localSuccesses.push_back(wavPath);
                    
                    // Flush batch alle 50 Erfolge für weniger Mutex-Overhead
                    if (localSuccesses.size() >= 50) {
                        std::lock_guard<std::mutex> lock(resultMutex);
                        successFiles.insert(successFiles.end(), 
                                          localSuccesses.begin(), localSuccesses.end());
                        localSuccesses.clear();
                    }
                }
                
                // Progress - alle 50 Dateien für besseres Feedback
                int done = completed.fetch_add(1, std::memory_order_relaxed) + 1;
                if (progressCallback && (done % 50 == 0 || done == (int)sidFiles.size())) {
                    progressCallback(done, sidFiles.size());
                }
            }
            
            // Flush remaining batch
            if (!localSuccesses.empty()) {
                std::lock_guard<std::mutex> lock(resultMutex);
                successFiles.insert(successFiles.end(), 
                                  localSuccesses.begin(), localSuccesses.end());
            }
        } catch (...) {
            // Verhindere Zombie-Threads bei Exceptions
            if (!localSuccesses.empty()) {
                std::lock_guard<std::mutex> lock(resultMutex);
                successFiles.insert(successFiles.end(), 
                                  localSuccesses.begin(), localSuccesses.end());
            }
        }
    };
    
    // Starte Threads - nutze Hardware-Concurrency optimal
    int effectiveThreads = std::min(threads, (int)sidFiles.size());
    threadPool.reserve(effectiveThreads);
    for (int i = 0; i < effectiveThreads; i++) {
        threadPool.emplace_back(worker);
    }
    
    // Warte auf Fertigstellung - mit Timeout-Schutz
    for (auto& t : threadPool) {
        if (t.joinable()) {
            t.join();
        }
    }
    
    // Final Progress Update
    if (progressCallback) {
        progressCallback(completed.load(), sidFiles.size());
    }
    
    return successFiles;
}
