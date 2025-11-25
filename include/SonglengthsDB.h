#ifndef SONGLENGTHSDB_H
#define SONGLENGTHSDB_H

#include <string>
#include <unordered_map>
#include <vector>

class SonglengthsDB {
public:
    SonglengthsDB();
    
    /**
     * Lädt die Songlengths.md5 Datei
     * @param dbPath Pfad zur Songlengths.md5 Datei
     * @return true bei Erfolg
     */
    bool load(const std::string& dbPath);
    
    /**
     * Hole Track-Länge für einen SID
     * @param sidPath Pfad zur SID-Datei
     * @param subtune Subtune-Nummer (1-basiert)
     * @return Länge in Sekunden, oder 0 wenn nicht gefunden
     */
    int getLength(const std::string& sidPath, int subtune = 1);
    
    /**
     * Messe tatsächliche Audio-Länge einer SID-Datei
     * @param sidPath Pfad zur SID-Datei
     * @param subtune Subtune-Nummer (1-basiert)
     * @param maxSeconds Maximale Test-Dauer in Sekunden
     * @return Gemessene Länge in Sekunden, oder 0 bei Fehler
     */
    static int measureActualLength(const std::string& sidPath, int subtune = 1, int maxSeconds = 600);
    
    /**
     * Füge gemessene Länge zur eigenen Datenbank hinzu
     * @param sidPath Pfad zur SID-Datei
     * @param subtune Subtune-Nummer (1-basiert)
     * @param lengthSeconds Länge in Sekunden
     */
    void addCustomLength(const std::string& sidPath, int subtune, int lengthSeconds);
    
    /**
     * Speichere eigene Messungen in Datei
     * @param customDbPath Pfad zur eigenen MD5-Liste
     * @return true bei Erfolg
     */
    bool saveCustomDB(const std::string& customDbPath);
    
    /**
     * Berechne MD5 einer SID-Datei (nur der relevante Teil)
     * @param sidPath Pfad zur SID-Datei
     * @return MD5-Hash als Hex-String
     */
    static std::string calculateSidMD5(const std::string& sidPath);
    
private:
    // MD5-Hash -> Vector von Track-Längen (in Sekunden)
    std::unordered_map<std::string, std::vector<int>> lengths_;
    // Relativer Pfad -> MD5-Hash (für schnellen Lookup)
    std::unordered_map<std::string, std::string> pathToMD5_;
    
    /**
     * Parse eine Zeit im Format M:SS oder MM:SS
     * @param timeStr Zeit-String
     * @return Sekunden
     */
    static int parseTime(const std::string& timeStr);
};

#endif // SONGLENGTHSDB_H
