#ifndef TRAININGMODEL_H
#define TRAININGMODEL_H

#include "MediaDatabase.h"
#include "InstrumentExtractor.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <map>

#ifdef WITH_OPENVINO
#include <openvino/openvino.hpp>
#endif

/**
 * TrainingModel - ML-Modell-Training für Audio-Generierung
 * 
 * Trainiert ein VAE-Modell auf extrahierten Audio-Features:
 * - MFCC (Mel-Frequency Cepstral Coefficients)
 * - Spektral-Features (Centroid, Rolloff, ZCR)
 * - Genre/Stil-Embeddings
 * 
 * Nutzt OpenVINO für NPU/GPU-Beschleunigung
 */
class TrainingModel {
public:
    TrainingModel(MediaDatabase& db);
    ~TrainingModel();
    
    /**
     * Trainiert das Modell
     * @param epochs Anzahl Trainings-Epochen
     * @param batchSize Batch-Größe
     * @param learningRate Learning Rate
     * @param progressCallback Optional: callback(epoch, loss, accuracy)
     * @return true bei Erfolg
     */
    bool train(
        int epochs = 100,
        int batchSize = 32,
        float learningRate = 0.001f,
        std::function<void(int, float, float)> progressCallback = nullptr
    );
    
    /**
     * Speichert trainiertes Modell
     * @param path Pfad zur Model-Datei (.xml für OpenVINO IR)
     * @return true bei Erfolg
     */
    bool saveModel(const std::string& path);
    
    /**
     * Lädt trainiertes Modell
     * @param path Pfad zur Model-Datei
     * @return true bei Erfolg
     */
    bool loadModel(const std::string& path);
    
    /**
     * 🎓 ONLINE-LEARNING: Trainiert Modell mit korrigierter Datenbasis
     * Wird nach jeder Korrektur automatisch aufgerufen
     * @param correctedTrack Korrigierter Track
     * @param originalGenre Original-Genre vor Korrektur
     * @return true bei Erfolg
     */
    bool retrainWithCorrectedData(const MediaMetadata& correctedTrack, const std::string& originalGenre);
    
    /**
     * 🎓 BATCH-LEARNING: Trainiert mit allen ausstehenden Korrekturen
     * Effizienter als Einzeltraining, wird alle N Korrekturen aufgerufen
     * @param minCorrections Minimum Korrekturen für Batch-Update
     * @return Anzahl verarbeiteter Korrekturen
     */
    int batchRetrainPending(int minCorrections = 10);
    
    /**
     * 🧠 INTELLIGENTE ANALYSE: Findet ähnliche Tracks mit potentiell falschem Genre
     * Analysiert Spektral-Ähnlichkeit und schlägt Korrekturen vor
     * @param correctedTrack Gerade korrigierter Track als Referenz
     * @param oldGenre Original-Genre vor Korrektur
     * @return Liste von Track-IDs mit ähnlichen Features aber falschem Genre
     */
    std::vector<int64_t> findSimilarTracksWithWrongGenre(
        const MediaMetadata& correctedTrack, 
        const std::string& oldGenre,
        float similarityThreshold = 0.80f
    );
    
    /**
     * 🔍 PATTERN-LEARNING: Lernt Korrektur-Muster aus Historie
     * Erkennt z.B. "Alle Tracks von Artist X sind eigentlich Genre Y statt Z"
     * @return Map von erkannten Mustern (Artist/BPM-Range -> korrektes Genre)
     */
    std::map<std::string, std::string> learnCorrectionPatterns();
    
    /**
     * ⚡ AUTO-KORREKTUR: Wendet gelernte Muster auf Datenbank an
     * Analysiert alle Tracks und korrigiert basierend auf Ähnlichkeit
     * @param autoApply true = automatisch speichern, false = nur vorschlagen
     * @return Anzahl vorgeschlagener/durchgeführter Korrekturen
     */
    int suggestDatabaseCorrections(bool autoApply = false);
    
    /**
     * Statistik für Online-Learning
     */
    int getPendingCorrections() const { return pendingCorrections_; }
    int getTotalRetrains() const { return totalRetrains_; }
    int getSuggestedCorrections() const { return suggestedCorrections_; }
    
    /**
     * 📊 Alle Statistiken für Idle Learning
     */
    std::map<std::string, int> getStats() const {
        return {
            {"corrections", totalRetrains_},
            {"suggestions", suggestedCorrections_},
            {"pending", pendingCorrections_}
        };
    }
    
    /**
     * 🧹 CLEANUP: Entfernt falsche Lernmuster bei Korrektur
     * Wenn Track A von Genre X -> Y korrigiert wird,
     * werden alle falschen Lernmuster mit dem alten Genre entfernt
     * @param correctedTrack Korrigierter Track
     * @param oldGenre Altes (falsches) Genre
     * @return Anzahl entfernter falscher Muster
     */
    int removeFalseLearningPatterns(const MediaMetadata& correctedTrack, const std::string& oldGenre);
    
    /**
     * 🔄 REVALIDATE: Überprüft und korrigiert die gesamte Korrektur-Historie
     * Entfernt widersprüchliche Einträge und konsolidiert Muster
     * @return Anzahl bereinigter Einträge
     */
    int revalidateCorrectionHistory();
    
    /**
     * 🗑️ Löscht Korrektur-Historie-Einträge für bestimmten Track
     * Wird aufgerufen wenn Track gelöscht oder erneut korrigiert wird
     * @param trackId Track-ID oder Filepath
     * @return Anzahl gelöschter Einträge
     */
    int clearHistoryForTrack(const std::string& filepath);
    
    /**
     * Generiert Audio-Features aus latenten Vektor
     * @param latentVector Zufalls-Vektor oder interpolierte Features
     * @param genre Genre-Embedding
     * @param bpm Tempo
     * @return Generierte Audio-Features
     */
    std::vector<float> generate(
        const std::vector<float>& latentVector,
        const std::string& genre,
        float bpm
    );
    
    /**
     * Extrahiert Features aus Training-Dataset
     * @return Anzahl extrahierter Feature-Vektoren
     */
    size_t extractTrainingFeatures();
    
    /**
     * 🎸 Entfernt ähnlich klingende Instrumenten-Duplikate
     * Nutzt Cross-Correlation, RMS und Peak-Analyse
     */
    void removeDuplicateInstruments();
    
    /**
     * 🎭 GENRE-FUSION LEARNING: Lernt typische Genre-Kombinationen
     * Analysiert Tracks mit mehreren Genre-Tags (wie The Prodigy)
     * @return Map von Genre-Kombinationen zu Häufigkeiten
     */
    std::map<std::string, int> learnGenreFusions();
    
    /**
     * 🎨 KÜNSTLER-STIL-ERKENNUNG: Lernt charakteristische Stile
     * Erkennt typische Sound-Signaturen von Künstlern
     * @param artist Künstlername (z.B. "The Prodigy")
     * @return Charakteristische Features des Künstlers
     */
    std::vector<float> learnArtistStyle(const std::string& artist);
    
    /**
     * 🔍 Schlägt Multi-Genre-Tags basierend auf Audio-Ähnlichkeit vor
     * Analysiert einen Track und schlägt passende Genre-Kombinationen vor
     * @param media Track zum Analysieren
     * @return Vorgeschlagene Genre-Tags (z.B. "Breakbeat,Electronic,Industrial")
     */
    std::string suggestGenreTags(const MediaMetadata& media);
    
    /**
     * Prüft ob NPU/GPU verfügbar ist
     */
    bool hasAccelerator() const { return useAccelerator_; }
    std::string getAcceleratorDevice() const { return acceleratorDevice_; }
    
    /**
     * Modell-Status
     */
    bool isModelLoaded() const { return modelLoaded_; }
    bool isModelTrained() const { return modelTrained_; }
    
private:
    MediaDatabase& db_;
    
    // Feature-Extraktion
    struct AudioFeatures {
        std::vector<float> mfcc;           // 13 Koeffizienten
        float spectralCentroid;
        float spectralRolloff;
        float zeroCrossingRate;
        float bpm;
        std::string genre;
        int genreId;                        // Genre als numerischer ID
    };
    
    std::vector<AudioFeatures> trainingFeatures_;
    std::map<std::string, int> genreToId_;
    std::map<int, std::string> idToGenre_;
    
    // Instrument-Library für Training
    std::map<InstrumentSample::Type, std::vector<InstrumentSample>> instrumentLibrary_;
    void loadInstrumentLibrary();
    void playInstrumentCombination(const std::string& genre);  // Spielt harmonische Kombination
    
    // OpenVINO
#ifdef WITH_OPENVINO
    std::shared_ptr<ov::Core> core_;
    std::shared_ptr<ov::CompiledModel> compiledModel_;
    std::shared_ptr<ov::InferRequest> inferRequest_;
#endif
    
    bool useAccelerator_ = false;
    std::string acceleratorDevice_ = "CPU";
    bool modelLoaded_ = false;
    bool modelTrained_ = false;
    
    // 🎓 Online-Learning State
    std::vector<MediaMetadata> pendingRetrainTracks_;  // Queue für Batch-Retraining
    std::vector<std::string> originalGenres_;           // Original-Genres vor Korrektur
    int pendingCorrections_ = 0;                        // Anzahl wartender Korrekturen
    int totalRetrains_ = 0;                             // Gesamt-Retrainings durchgeführt
    int suggestedCorrections_ = 0;                      // Automatisch vorgeschlagene Korrekturen
    int removedFalsePatterns_ = 0;                      // Anzahl entfernter falscher Muster
    std::chrono::steady_clock::time_point lastRetrainTime_;  // Letzter Retrain-Zeitpunkt
    
    // 🧠 Korrektur-Historie für Pattern-Learning
    struct CorrectionHistoryEntry {
        int64_t trackId;
        std::string filepath;
        std::string artist;
        std::string oldGenre;
        std::string newGenre;
        float bpm;
        AudioFeatures features;
        std::chrono::system_clock::time_point timestamp;
    };
    std::vector<CorrectionHistoryEntry> correctionHistory_;
    
    // Training Resume
    int lastEpoch_ = 0;
    float lastLoss_ = 0.0f;
    std::string checkpointPath_;
    
    // Training-Hilfsfunktionen
    void initializeAccelerator();
    void balanceDataset();
    bool saveCheckpoint(const std::string& path, int epoch, float loss);
    bool loadCheckpoint(const std::string& path, int& outEpoch, float& outLoss);
    std::vector<float> normalizeFeatures(const AudioFeatures& features);
    AudioFeatures denormalizeFeatures(const std::vector<float>& normalized);
    int getOrCreateGenreId(const std::string& genre);
    std::vector<float> createLatentVector(int dimensions = 32);
    
    // Simplified Training (ohne externe ML-Library)
    // In Produktion würde man PyTorch/TensorFlow nutzen und zu OpenVINO IR exportieren
    void trainSimplifiedVAE(
        int epochs,
        int batchSize,
        float learningRate,
        std::function<void(int, float, float)> progressCallback
    );
    
    // 🎓 Online-Learning Helper
    void incrementalUpdate(const AudioFeatures& correctedFeatures, const std::string& oldGenre);
    void batchUpdate(const std::vector<AudioFeatures>& correctedBatch);
    AudioFeatures extractFeaturesFromTrack(const MediaMetadata& track);
    
    // 🧠 Intelligente Analyse Helper
    float calculateFeatureSimilarity(const AudioFeatures& a, const AudioFeatures& b);
    float calculateSpectralSimilarity(const MediaMetadata& a, const MediaMetadata& b);
    bool matchesCorrectionPattern(const MediaMetadata& track, const CorrectionHistoryEntry& pattern);
    void addToCorrectionHistory(const MediaMetadata& track, const std::string& oldGenre);
    
    // 🥁 Instrumenten-Duplikat-Erkennung (private)
    bool isSimilarInstrument(const InstrumentSample& a, const InstrumentSample& b, float threshold = 0.85f);
    
    // 🧹 Pattern-Cleanup Helper
    bool isConflictingPattern(const CorrectionHistoryEntry& a, const CorrectionHistoryEntry& b);
    void consolidateSimilarPatterns();
    int removeOutdatedPatterns(const std::string& filepath);
};

#endif // TRAININGMODEL_H
