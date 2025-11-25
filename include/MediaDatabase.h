#ifndef MEDIADATABASE_H
#define MEDIADATABASE_H

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <sqlite3.h>
#include <mutex>

/**
 * Struktur für Mediendatei-Metadaten
 */
struct MediaMetadata {
    int64_t id = 0;
    std::string filepath;
    std::string title;
    std::string artist;
    
    // Audio-Analyse-Daten
    float bpm = 0.0f;
    float duration = 0.0f;
    
    // Stil-Kategorisierung
    std::string genre;        // Trap, Techno, Rock, Pop, Klassik, Metal, New Metal, Trance, etc.
    std::string subgenre;
    
    // Charakteristiken
    std::string intensity;    // hart, soft
    std::string bassLevel;    // basslastig, soft, mittel
    std::string mood;         // Erweiterte Stil-Info: Rhythmus-Pattern, Tags, etc.
    
    // Erkannte Features (NPU/ML)
    std::string instruments;  // Comma-separated: "guitar,drums,bass,synth"
    std::string melodySignature;  // ML-Feature-Vektor als String
    std::string rhythmPattern;
    
    // Audio-Features
    float spectralCentroid = 0.0f;
    float spectralRolloff = 0.0f;
    float zeroCrossingRate = 0.0f;
    float mfccHash = 0.0f;  // Simplified MFCC signature
    
    // Metadaten
    int64_t addedTimestamp = 0;
    int64_t lastUsed = 0;
    int useCount = 0;
    bool analyzed = false;
    bool isTrainingData = true;  // Standard: alle Dateien für Training verwenden
    std::string fileHash;  // SHA256 oder MD5 für Duplikatserkennung
};

/**
 * MediaDatabase - SQLite-basierte Datenbank für Trainingsdaten
 * 
 * Features:
 * - Speichert alle Mediendateien mit Audio-Features
 * - NPU-optimierte Feature-Extraktion
 * - Schnelle Suche nach Genre, Stil, BPM, Instrumenten
 * - Training-Dataset-Verwaltung
 */
class MediaDatabase {
public:
    MediaDatabase(const std::string& dbPath = "~/.songgen/media.db");
    ~MediaDatabase();
    
    // Datenbank-Operationen
    bool initialize();
    bool addMedia(const MediaMetadata& meta);
    bool updateMedia(const MediaMetadata& meta);
    bool deleteMedia(int64_t id);
    bool existsByPath(const std::string& filepath);
    
    // Suche und Filter
    std::vector<MediaMetadata> searchByGenre(const std::string& genre);
    std::vector<MediaMetadata> searchByInstruments(const std::vector<std::string>& instruments);
    std::vector<MediaMetadata> searchByBPM(float minBpm, float maxBpm);
    std::vector<MediaMetadata> searchByIntensity(const std::string& intensity);
    std::vector<MediaMetadata> searchByBassLevel(const std::string& bassLevel);
    std::vector<MediaMetadata> getAll();
    std::vector<MediaMetadata> getUnanalyzed();
    MediaMetadata getById(int64_t id);
    
    // Erweiterte Suche
    std::vector<MediaMetadata> findSimilar(const MediaMetadata& reference, int limit = 10);
    std::vector<MediaMetadata> searchAdvanced(
        const std::string& genre = "",
        const std::string& intensity = "",
        const std::string& bassLevel = "",
        float minBpm = 0.0f,
        float maxBpm = 999.0f
    );
    
    // Statistiken
    size_t getTotalCount();
    size_t getCountByGenre(const std::string& genre);
    std::vector<std::string> getAllGenres();
    
    // Sortierte Abfragen für Playlist-Integration
    std::vector<MediaMetadata> getAllSortedByGenre();
    std::vector<MediaMetadata> getAllSortedByBPM();
    std::vector<MediaMetadata> getAllSortedByMood();
    std::vector<MediaMetadata> getPlayableByCategory(const std::string& category);
    
    // Playlist-Ready: Gruppiert nach Kategorien
    std::map<std::string, std::vector<MediaMetadata>> getGroupedByGenre();
    std::map<std::string, std::vector<MediaMetadata>> getGroupedByBPMRange();
    
    // Duplikatserkennung
    struct DuplicateInfo {
        int64_t id1;
        int64_t id2;
        std::string filepath1;
        std::string filepath2;
        float similarity;
        std::string reason;  // "identical", "similar_mfcc", "same_spectral"
    };
    std::vector<DuplicateInfo> findDuplicates(float threshold = 0.95f);
    std::vector<DuplicateInfo> findIdenticalFiles();  // Exakte Duplikate (gleicher Pfad/Hash)
    std::vector<DuplicateInfo> findAudioDuplicates(float mfccThreshold = 0.98f);  // Audio-ähnlichkeit
    bool removeDuplicate(int64_t keepId, int64_t removeId);
    
    // Training-Dataset Verwaltung
    struct TrainingStats {
        size_t totalFiles;
        size_t analyzedFiles;
        size_t duplicates;
        std::map<std::string, size_t> genreDistribution;
        std::map<std::string, size_t> intensityDistribution;
        float avgBpm;
        float avgDuration;
        size_t filesWithoutGenre;
    };
    TrainingStats getTrainingStats();
    std::vector<MediaMetadata> getTrainingSet(const std::string& genre = "", size_t maxSamples = 0);
    bool markAsTrainingData(int64_t id, bool isTraining = true);
    bool balanceDataset();  // Balanciert Genre-Verteilung
    
    // Interaktive Training-Entscheidungen
    struct TrainingDecision {
        int64_t id = 0;
        std::string question;           // "Genre-Klassifikation unklar: BPM=135, Spektrum=Techno/Trance. Ihre Entscheidung?"
        std::vector<std::string> options;  // ["Techno", "Trance", "Progressive", "Manuell eingeben"]
        std::string userAnswer;         // Gewählte Antwort
        float confidence = 0.0f;        // AI-Konfidenz vor Frage (0.0-1.0)
        std::string context;            // JSON mit Details: {"fileId": 123, "bpm": 135, "detectedGenres": ["Techno", "Trance"]}
        int64_t timestamp = 0;          // Unix-Timestamp
        std::string decisionType;       // "genre_classification", "pattern_selection", "parameter_conflict", "confidence_threshold"
        bool answered = false;          // true wenn beantwortet, false wenn übersprungen/abgebrochen
        std::string audioFile;          // Pfad zur Audio-Datei für Play-Button
    };
    bool saveDecision(const TrainingDecision& decision);
    std::vector<TrainingDecision> getDecisionHistory(size_t limit = 100);
    std::vector<TrainingDecision> getUnansweredQuestions();
    std::vector<TrainingDecision> findSimilarDecisions(const std::string& context, float threshold = 0.8f);
    bool deleteDecision(int64_t id);
    bool markQuestionAsAnswered(int64_t id, const std::string& answer);
    
private:
    std::string dbPath_;
    sqlite3* db_ = nullptr;
    std::mutex dbMutex_;
    
    bool executeSQL(const std::string& sql);
    sqlite3_stmt* prepareStatement(const std::string& sql);
    std::string expandPath(const std::string& path);
};

#endif // MEDIADATABASE_H
