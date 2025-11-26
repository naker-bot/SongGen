#ifndef INSTRUMENTEXTRACTOR_H
#define INSTRUMENTEXTRACTOR_H

#include <vector>
#include <string>
#include <map>
#include <chrono>

/**
 * InstrumentExtractor - Extrahiert markante Instrument-Samples aus Audio-Dateien
 * 
 * Erkennt und isoliert:
 * - Kick Drums (50-100 Hz)
 * - Snares (150-250 Hz mit Noise)
 * - Hi-Hats (8-12 kHz)
 * - Bass-Lines (60-250 Hz)
 * - Lead-Melodien (500-4000 Hz)
 * - Pads (harmonisch reich, lang)
 */

struct InstrumentSample {
    enum Type {
        KICK,
        SNARE,
        HIHAT,
        CLAP,
        BASS,
        LEAD,
        PAD,
        FX,
        UNKNOWN
    };
    
    Type type;
    std::string sourceFile;
    float startTime;        // Sekunden im Original
    float duration;         // Sekunden
    std::vector<float> samples;  // Audio-Daten
    int sampleRate;
    
    // Features
    float dominantFreq;
    float spectralCentroid;
    float energy;
    float clarity;          // Wie "sauber" ist das Sample (0-1)
    
    // 🎵 Rhythmus-Features
    float timeSinceLastHit = 0.0f;  // Zeit seit letztem Hit (Sekunden)
    float confidenceScore = 1.0f;    // Wie sicher ist die Erkennung (0-1)
    
    std::string description;
};

class InstrumentExtractor {
public:
    /**
     * Extrahiert Instrument-Samples aus einer Audio-Datei
     * @param audioPath Pfad zur Audio-Datei (MP3/WAV/FLAC)
     * @param minQuality Minimale Qualität (0-1), Standard 0.7
     * @return Liste von extrahierten Instrument-Samples
     */
    static std::vector<InstrumentSample> extractInstruments(
        const std::string& audioPath, 
        float minQuality = 0.7f);
    
    /**
     * Speichert Instrument-Sample als WAV-Datei
     * @param sample Das zu speichernde Sample
     * @param outputPath Ziel-Pfad
     * @return Erfolg
     */
    static bool saveSample(const InstrumentSample& sample, const std::string& outputPath);
    
    /**
     * Lädt alle Instrument-Samples aus Verzeichnis
     * @param instrumentDir Verzeichnis mit Instrument-Samples
     * @return Map von Type -> Liste von Samples
     */
    static std::map<InstrumentSample::Type, std::vector<InstrumentSample>> 
        loadInstrumentLibrary(const std::string& instrumentDir);
    
    /**
     * 🎓 LEARNING: Trainiert Extractor mit manuellem Feedback
     * Lernt optimale Parameter für Onset-Detection, Frequency-Ranges etc.
     * @param sample Referenz-Sample (manuell markiert als gut)
     * @param type Instrument-Type
     * @param quality User-Bewertung (0-1)
     * @return true bei Erfolg
     */
    static bool trainWithSample(const InstrumentSample& sample, 
                                InstrumentSample::Type type, 
                                float quality);
    
    /**
     * 🧠 Verbessert Extraktion basierend auf Lern-Daten
     * Passt Detection-Parameter dynamisch an
     */
    static void optimizeExtractionParameters();
    
    /**
     * 📊 Statistik der Extraktions-Qualität
     */
    static void printExtractionStats();
    
    /**
     * 💾 Speichert/Lädt gelernte Parameter
     */
    static bool saveLearnedParameters(const std::string& path);
    static bool loadLearnedParameters(const std::string& path);
    
    /**
     * 🥁 Rhythmus-Pattern-Erkennung
     * Analysiert zeitliche Abstände zwischen Hits
     */
    static std::vector<float> analyzeRhythmPattern(const std::vector<InstrumentSample>& samples);
    
    /**
     * 🗑️ Automatisches Löschen von stillen/schlechten Samples
     * @param samples Liste von Samples zum Filtern
     * @param deleteFromDisk Auch von Festplatte löschen
     * @return Anzahl gelöschter Samples
     */
    static int autoDeleteSilentSamples(std::vector<InstrumentSample>& samples, 
                                       bool deleteFromDisk = false);

private:
    static std::vector<InstrumentSample> findKicks(
        const std::vector<float>& samples, int sampleRate, const std::string& sourceFile);
    
    static std::vector<InstrumentSample> findSnares(
        const std::vector<float>& samples, int sampleRate, const std::string& sourceFile);
    
    static std::vector<InstrumentSample> findHiHats(
        const std::vector<float>& samples, int sampleRate, const std::string& sourceFile);
    
    static std::vector<InstrumentSample> findBassLines(
        const std::vector<float>& samples, int sampleRate, const std::string& sourceFile);
    
    static std::vector<InstrumentSample> findLeads(
        const std::vector<float>& samples, int sampleRate, const std::string& sourceFile);
    
    static float calculateClarity(const std::vector<float>& samples);
    static float calculateRMSEnergy(const std::vector<float>& samples);
    static bool isSilentSample(const std::vector<float>& samples, float threshold = 0.01f);
    static float getDominantFrequency(const std::vector<float>& samples, int sampleRate);
    static std::vector<size_t> findOnsets(const std::vector<float>& samples, int sampleRate);
    static std::vector<float> applyBandpassFilter(
        const std::vector<float>& samples, float lowFreq, float highFreq, int sampleRate);
    
    // 🎓 Learning System
    struct ExtractionParameters {
        // Onset Detection
        float onsetThreshold = 0.3f;
        float onsetSmoothingWindow = 0.02f;  // Sekunden
        
        // Frequency Ranges (Hz)
        struct FreqRange {
            float low, high;
            float confidence = 1.0f;  // Wie sicher sind wir?
        };
        
        FreqRange kickRange = {40.0f, 120.0f, 1.0f};
        FreqRange snareRange = {120.0f, 300.0f, 1.0f};
        FreqRange hihatRange = {6000.0f, 14000.0f, 1.0f};
        FreqRange bassRange = {50.0f, 300.0f, 1.0f};
        FreqRange leadRange = {400.0f, 5000.0f, 1.0f};
        
        // Duration Ranges (Sekunden)
        float minKickDuration = 0.05f;
        float maxKickDuration = 0.3f;
        float minSnareDuration = 0.05f;
        float maxSnareDuration = 0.2f;
        float minHihatDuration = 0.01f;
        float maxHihatDuration = 0.1f;
        
        // Energy Thresholds
        float minKickEnergy = 0.4f;
        float minSnareEnergy = 0.3f;
        float minHihatEnergy = 0.2f;
        
        // Clarity Thresholds
        float minClarityKick = 0.6f;
        float minClaritySnare = 0.5f;
        float minClarityHihat = 0.7f;
        
        // Silence Detection
        float minRMSEnergy = 0.01f;  // Minimale RMS-Energie (filtert Stille)
        float silenceThreshold = 0.005f;  // Absoluter Schwellwert für Stille
    };
    
    static ExtractionParameters params_;
    
    struct TrainingData {
        InstrumentSample sample;
        InstrumentSample::Type correctType;
        float userQuality;
        std::chrono::system_clock::time_point timestamp;
    };
    
    // 🥁 Rhythmus-Pattern für jedes Instrument
    struct RhythmPattern {
        InstrumentSample::Type type;
        std::vector<float> intervals;  // Zeitabstände in Sekunden
        float averageInterval = 0.0f;
        float variance = 0.0f;
        int hitCount = 0;
    };
    
    static std::vector<TrainingData> trainingHistory_;
    static std::map<InstrumentSample::Type, RhythmPattern> learnedRhythms_;
    static int totalExtractions_;
    static int successfulExtractions_;
    
    // Parameter-Optimierung Helper
    static void updateParameterFromFeedback(const TrainingData& data);
    static float calculateParameterDeviation(const InstrumentSample& sample, 
                                             InstrumentSample::Type type);
};

#endif // INSTRUMENTEXTRACTOR_H
