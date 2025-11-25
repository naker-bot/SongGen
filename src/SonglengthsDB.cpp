#include "SonglengthsDB.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <filesystem>
#include <vector>
#include <cmath>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <openssl/md5.h>

SonglengthsDB::SonglengthsDB() {}

bool SonglengthsDB::load(const std::string& dbPath) {
    std::ifstream file(dbPath);
    if (!file.is_open()) {
        std::cerr << "[SonglengthsDB] ❌ Kann nicht öffnen: " << dbPath << "\n";
        return false;
    }
    
    std::string line;
    std::string lastCommentPath;
    int lineCount = 0;
    
    while (std::getline(file, line)) {
        lineCount++;
        
        // Skip leere Zeilen
        if (line.empty()) continue;
        
        // Kommentar-Zeile mit Pfad: ; /MUSICIANS/H/Hubbard_Rob/Commando.sid
        if (line[0] == ';') {
            // Extrahiere Pfad aus Kommentar
            size_t pathStart = line.find('/');
            if (pathStart != std::string::npos) {
                lastCommentPath = line.substr(pathStart);
                // Entferne trailing whitespace
                while (!lastCommentPath.empty() && std::isspace(lastCommentPath.back())) {
                    lastCommentPath.pop_back();
                }
            }
            continue;
        }
        
        // Format: MD5HASH=M:SS M:SS M:SS ...
        size_t pos = line.find('=');
        if (pos == std::string::npos) continue;
        
        std::string md5 = line.substr(0, pos);
        std::string timesStr = line.substr(pos + 1);
        
        // Parse Zeiten
        std::vector<int> times;
        std::istringstream iss(timesStr);
        std::string timeStr;
        
        while (iss >> timeStr) {
            int seconds = parseTime(timeStr);
            times.push_back(seconds);
        }
        
        if (!times.empty()) {
            lengths_[md5] = times;
            // Speichere auch Pfad→MD5 Mapping
            if (!lastCommentPath.empty()) {
                pathToMD5_[lastCommentPath] = md5;
            }
        }
    }
    
    file.close();
    std::cout << "[SonglengthsDB] ✅ Geladen: " << lengths_.size() << " Einträge\n";
    return true;
}

int SonglengthsDB::getLength(const std::string& sidPath, int subtune) {
    // Extrahiere relativen Pfad (alles nach /C64Music/)
    size_t pos = sidPath.find("/C64Music/");
    std::string relativePath;
    
    if (pos != std::string::npos) {
        relativePath = sidPath.substr(pos + 9); // +9 für "/C64Music"
    } else {
        // Versuche nur den Dateinamen
        pos = sidPath.find_last_of('/');
        if (pos != std::string::npos) {
            relativePath = sidPath.substr(pos);
        } else {
            relativePath = sidPath;
        }
    }
    
    // Suche MD5 über Pfad
    std::string md5;
    auto pathIt = pathToMD5_.find(relativePath);
    if (pathIt != pathToMD5_.end()) {
        md5 = pathIt->second;
    } else {
        // Fallback: Versuche MD5 zu berechnen
        md5 = calculateSidMD5(sidPath);
        if (md5.empty()) return 0;
    }
    
    auto it = lengths_.find(md5);
    if (it == lengths_.end()) {
        return 0; // Nicht in Datenbank
    }
    
    const auto& times = it->second;
    
    // subtune ist 1-basiert
    if (subtune < 1 || subtune > (int)times.size()) {
        return 0;
    }
    
    return times[subtune - 1];
}

std::string SonglengthsDB::calculateSidMD5(const std::string& sidPath) {
    // HVSC MD5: Berechnet von den C64-Daten (ab 0x7C, nach kompletten PSID/RSID Header)
    // Dies ist der offizielle HVSC-Standard für Songlengths.md5
    
    std::ifstream file(sidPath, std::ios::binary);
    if (!file.is_open()) return "";
    
    // Lese komplette Datei
    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    
    if (fileSize < 0x7C) return "";
    
    std::vector<unsigned char> data(fileSize);
    file.read(reinterpret_cast<char*>(data.data()), fileSize);
    file.close();
    
    // Prüfe Magic
    if (data[0] != 'P' && data[0] != 'R') return "";
    if (data[1] != 'S' || data[2] != 'I' || data[3] != 'D') return "";
    
    // HVSC MD5: Berechne von C64-Daten ab Ende des Headers (0x7C)
    // Dies schließt flags, speed settings und die eigentlichen Musikdaten ein
    MD5_CTX md5Context;
    MD5_Init(&md5Context);
    
    // Berechne MD5 ab 0x7C (Ende des fixen Headers) bis Dateiende
    size_t dataStart = 0x7C;
    if (fileSize > dataStart) {
        MD5_Update(&md5Context, &data[dataStart], fileSize - dataStart);
    }
    
    unsigned char result[MD5_DIGEST_LENGTH];
    MD5_Final(result, &md5Context);
    
    // Konvertiere zu Hex-String
    std::ostringstream oss;
    for (int i = 0; i < MD5_DIGEST_LENGTH; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)result[i];
    }
    
    return oss.str();
}

int SonglengthsDB::parseTime(const std::string& timeStr) {
    // Format: M:SS oder MM:SS
    size_t colonPos = timeStr.find(':');
    if (colonPos == std::string::npos) {
        // Nur Sekunden
        return std::stoi(timeStr);
    }
    
    int minutes = std::stoi(timeStr.substr(0, colonPos));
    int seconds = std::stoi(timeStr.substr(colonPos + 1));
    
    return minutes * 60 + seconds;
}

int SonglengthsDB::measureActualLength(const std::string& sidPath, int subtune, int maxSeconds) {
    // DEAKTIVIERT: Messung verursacht zu viele hängende Prozesse
    // Tracks ohne DB-Eintrag bekommen 180s default
    return 0;
}

void SonglengthsDB::addCustomLength(const std::string& sidPath, int subtune, int lengthSeconds) {
    // Deaktiviert
}

bool SonglengthsDB::saveCustomDB(const std::string& customDbPath) {
    // Deaktiviert
    return false;
}

// Unten die alte Implementierung als Kommentar für später:
/*
int SonglengthsDB::measureActualLength_OLD(const std::string& sidPath, int subtune, int maxSeconds) {
    std::ifstream file(tempWav, std::ios::binary);
    if (!file.is_open()) {
        std::filesystem::remove(tempWav);
        return 0;
    }
    
    // Überspringe WAV-Header (44 bytes)
    file.seekg(44);
    
    // Lese alle Samples
    std::vector<int16_t> samples;
    int16_t sample;
    while (file.read(reinterpret_cast<char*>(&sample), sizeof(sample))) {
        samples.push_back(sample);
    }
    file.close();
    
    // Cleanup
    std::filesystem::remove(tempWav);
    
    if (samples.empty()) return 0;
    
    // Erkenne Loop-Punkt durch Audio-Fingerprint-Vergleich
    // Viele SID-Tracks loopen endlos - wir schneiden bei Loop-Start ab
    const int WINDOW_SIZE = 44100;  // 1 Sekunde bei 44.1kHz
    const double SILENCE_THRESHOLD = 0.001;  // Schwellwert für Stille
    const int MAX_SILENCE_SECONDS = 5;  // Maximal 5 Sekunden Pause tolerieren
    const int MAX_SILENCE_SAMPLES = MAX_SILENCE_SECONDS * 44100 * 2;  // Stereo
    const int FINGERPRINT_SIZE = 22050;  // 0.5 Sekunden für Fingerprint
    const double LOOP_SIMILARITY_THRESHOLD = 0.95;  // 95% Ähnlichkeit = Loop erkannt
    
    // Berechne RMS-Fingerprints für alle Positionen
    std::vector<double> fingerprints;
    for (size_t i = 0; i + FINGERPRINT_SIZE < samples.size(); i += FINGERPRINT_SIZE) {
        double sumSquares = 0.0;
        for (size_t j = i; j < i + FINGERPRINT_SIZE && j < samples.size(); ++j) {
            double normalized = samples[j] / 32768.0;
            sumSquares += normalized * normalized;
        }
        fingerprints.push_back(std::sqrt(sumSquares / FINGERPRINT_SIZE));
    }
    
    // Suche nach Loop: Vergleiche Pattern am Anfang mit späteren Positionen
    const int MIN_INTRO_SECONDS = 10;  // Mindestens 10s vor Loop-Suche
    const int MIN_INTRO_FRAMES = (MIN_INTRO_SECONDS * 44100 * 2) / FINGERPRINT_SIZE;
    const int PATTERN_LENGTH = 8;  // Vergleiche 8 Fingerprints (4 Sekunden)
    
    int loopDetectedAt = -1;
    
    if (fingerprints.size() > MIN_INTRO_FRAMES + PATTERN_LENGTH * 2) {
        // Nimm Pattern nach Intro (z.B. bei 10-14 Sekunden)
        std::vector<double> introPattern(
            fingerprints.begin() + MIN_INTRO_FRAMES,
            fingerprints.begin() + MIN_INTRO_FRAMES + PATTERN_LENGTH
        );
        
        // Suche dieses Pattern später im Track (Loop-Punkt)
        for (size_t i = MIN_INTRO_FRAMES + PATTERN_LENGTH * 2; 
             i + PATTERN_LENGTH < fingerprints.size(); ++i) {
            
            // Berechne Ähnlichkeit zwischen introPattern und aktuellem Pattern
            double similarity = 0.0;
            for (int j = 0; j < PATTERN_LENGTH; ++j) {
                double diff = std::abs(introPattern[j] - fingerprints[i + j]);
                similarity += 1.0 - std::min(diff / std::max(introPattern[j], 1e-10), 1.0);
            }
            similarity /= PATTERN_LENGTH;
            
            if (similarity > LOOP_SIMILARITY_THRESHOLD) {
                loopDetectedAt = i * FINGERPRINT_SIZE;
                std::cout << "[Measure] 🔁 Loop erkannt bei " << (loopDetectedAt / 44100 / 2) 
                          << "s (Ähnlichkeit: " << (similarity * 100) << "%)\n";
                break;
            }
        }
    }
    
    // Finde letztes hörbares Audio (mit Pausen-Toleranz)
    int lastAudiblePos = 0;
    int silenceStartPos = -1;
    
    for (size_t i = 0; i + WINDOW_SIZE < samples.size(); i += WINDOW_SIZE / 2) {
        double sumSquares = 0.0;
        for (size_t j = i; j < i + WINDOW_SIZE && j < samples.size(); ++j) {
            double normalized = samples[j] / 32768.0;
            sumSquares += normalized * normalized;
        }
        double rms = std::sqrt(sumSquares / WINDOW_SIZE);
        
        if (rms > SILENCE_THRESHOLD) {
            lastAudiblePos = i + WINDOW_SIZE;
            silenceStartPos = -1;
        } else {
            if (silenceStartPos < 0) {
                silenceStartPos = i;
            } else {
                int silenceDuration = i - silenceStartPos;
                if (silenceDuration > MAX_SILENCE_SAMPLES) {
                    break;
                }
            }
        }
    }
    
    // Nutze Loop-Punkt wenn erkannt, sonst lastAudiblePos
    int endPos = (loopDetectedAt > 0) ? loopDetectedAt : lastAudiblePos;
    
    // Konvertiere Samples zu Sekunden (Stereo = 2 Kanäle)
    int lengthSeconds = (endPos / 2) / 44100;
    
    std::cout << "[Measure] ✅ " << sidPath << " Subtune " << subtune 
              << " = " << lengthSeconds << "s\n";
    
    return lengthSeconds;
}
*/

/*
void SonglengthsDB::addCustomLength_OLD(const std::string& sidPath, int subtune, int lengthSeconds) {
    std::string md5 = calculateSidMD5(sidPath);
    if (md5.empty()) {
        std::cerr << "[CustomDB] ❌ Kann MD5 nicht berechnen für " << sidPath << "\n";
        return;
    }
    
    // Extrahiere relativen Pfad
    size_t pos = sidPath.find("/C64Music/");
    std::string relativePath;
    if (pos != std::string::npos) {
        relativePath = sidPath.substr(pos + 9);
    } else {
        pos = sidPath.find_last_of('/');
        relativePath = (pos != std::string::npos) ? sidPath.substr(pos) : sidPath;
    }
    
    // Füge zur Datenbank hinzu
    auto& times = lengths_[md5];
    if ((int)times.size() < subtune) {
        times.resize(subtune, 0);
    }
    times[subtune - 1] = lengthSeconds;
    
    pathToMD5_[relativePath] = md5;
    
    std::cout << "[CustomDB] ➕ Hinzugefügt: " << relativePath 
              << " Subtune " << subtune << " = " << lengthSeconds << "s\n";
}


bool SonglengthsDB::saveCustomDB_OLD(const std::string& customDbPath) {
    std::ofstream file(customDbPath);
    if (!file.is_open()) {
        std::cerr << "[CustomDB] ❌ Kann nicht schreiben: " << customDbPath << "\n";
        return false;
    }
    
    file << "; Custom SongGen Songlengths Database\n";
    file << "; Generated by automatic length measurement\n";
    file << "; Format: MD5=M:SS M:SS M:SS ...\n\n";
    
    // Invertiere pathToMD5_ für Ausgabe
    std::unordered_map<std::string, std::string> md5ToPath;
    for (const auto& [path, md5] : pathToMD5_) {
        md5ToPath[md5] = path;
    }
    
    int count = 0;
    for (const auto& [md5, times] : lengths_) {
        // Schreibe Kommentar mit Pfad (falls vorhanden)
        auto pathIt = md5ToPath.find(md5);
        if (pathIt != md5ToPath.end()) {
            file << "; " << pathIt->second << "\n";
        }
        
        // Schreibe MD5=Zeiten
        file << md5 << "=";
        for (size_t i = 0; i < times.size(); ++i) {
            if (i > 0) file << " ";
            int mins = times[i] / 60;
            int secs = times[i] % 60;
            file << mins << ":" << std::setfill('0') << std::setw(2) << secs;
        }
        file << "\n";
        count++;
    }
    
    file.close();
    std::cout << "[CustomDB] ✅ Gespeichert: " << count << " Einträge in " << customDbPath << "\n";
    return true;
}
*/
