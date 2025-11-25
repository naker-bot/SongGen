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
};

#endif // TRAININGMODEL_H
