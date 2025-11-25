#ifndef AUDIOANALYZER_H
#define AUDIOANALYZER_H

#include "MediaDatabase.h"
#include <string>
#include <vector>
#include <complex>
#include <memory>
#include <functional>

/**
 * AudioAnalyzer - FFT-basierte Audio-Feature-Extraktion
 * 
 * Analysiert Audiodateien und extrahiert:
 * - BPM (Tempo)
 * - Melodie-Signature
 * - Rhythmus-Pattern
 * - Instrument-Erkennung
 * - Spektrale Features
 * - Stil-Kategorisierung
 * 
 * Optional NPU-beschleunigt falls verfügbar
 */
class AudioAnalyzer {
public:
    AudioAnalyzer();
    ~AudioAnalyzer();
    
    /**
     * Analysiert Audiodatei und füllt Metadaten
     * @param filepath Pfad zur Audio-Datei (WAV, MP3, FLAC, etc.)
     * @param meta Output: MediaMetadata-Struktur
     * @return true bei Erfolg
     */
    bool analyze(const std::string& filepath, MediaMetadata& meta);
    
    /**
     * Batch-Analyse mit Multi-Threading
     * @param filepaths Liste der Audio-Dateien
     * @param progressCallback Optional: callback(current, total)
     * @return Vector mit analysierten Metadaten
     */
    std::vector<MediaMetadata> analyzeBatch(
        const std::vector<std::string>& filepaths,
        const std::function<void(size_t, size_t)>& progressCallback = nullptr
    );
    
    // Feature-Extraktion
    float detectBPM(const std::vector<float>& samples, int sampleRate);
    std::vector<std::string> detectInstruments(const std::vector<float>& samples, int sampleRate);
    std::string classifyGenre(const MediaMetadata& meta);
    std::string detectIntensity(const std::vector<float>& samples);
    std::string detectBassLevel(const std::vector<float>& samples, int sampleRate);
    
    // Automatische Genre-Erkennung aus Audio-Features
    std::string detectGenreFromAudio(const std::vector<float>& samples, int sampleRate, float bpm);
    
    // Übersteuerungs-Erkennung und Reparatur
    struct ClippingInfo {
        bool hasClipping;
        float clippingPercentage;
        int clippedSamples;
        float peakLevel;
        float recommendedGain;
    };
    ClippingInfo detectClipping(const std::vector<float>& samples);
    bool repairClipping(const std::string& inputPath, const std::string& outputPath, float targetPeak = 0.95f);
    std::vector<float> applyDeclipping(const std::vector<float>& samples);
    
    // Erweiterte Stil-Analyse
    std::string analyzeRhythmPattern(const std::vector<float>& samples, int sampleRate, float bpm);
    std::string detectMusicalStyle(const MediaMetadata& meta);
    std::vector<std::string> extractStyleTags(const MediaMetadata& meta);
    
    // Automatische Sortierung
    struct SortCategory {
        std::string name;
        std::string path;
        std::vector<std::string> criteria;
    };
    std::string suggestCategory(const MediaMetadata& meta);
    std::vector<SortCategory> generateSortStructure(const std::vector<MediaMetadata>& files);
    
    // Spektral-Analyse
    float calculateSpectralCentroid(const std::vector<std::complex<float>>& spectrum);
    float calculateSpectralCentroid(const std::vector<float>& spectrum, int sampleRate); // Overload for frequency-based
    float calculateSpectralRolloff(const std::vector<std::complex<float>>& spectrum, float threshold = 0.85f);
    float calculateZeroCrossingRate(const std::vector<float>& samples);
    float calculateMFCCHash(const std::vector<float>& samples, int sampleRate);

    // Stille-Erkennung / Auto-Trim
    // Analysiert eine WAV-Datei (16-Bit PCM) und ermittelt, ob sie hörbaren Inhalt hat.
    // Optional werden führende und nachlaufende Stille entfernt.
    // Rückgabewert: true = Audio vorhanden (ggf. getrimmt), false = praktisch stumm/leer.
    bool detectSilenceAndTrimWav(
        const std::string& inPath,
        const std::string& outPath,
        float silenceThreshold = 0.0005f,  // Viel empfindlicher für leise SID-Tracks
        float minSoundSeconds = 1.0f,       // Nur 1 Sekunde Audio nötig
        float tailSilenceSeconds = 3.0f
    );
    
    // Audio-Loading (public for use in callbacks)
    bool loadAudioFile(const std::string& filepath, std::vector<float>& samples, int& sampleRate);
    
private:
    // FFT-Utilities
    std::vector<std::complex<float>> performFFT(const std::vector<float>& samples);
    std::vector<float> extractMFCC(const std::vector<float>& samples, int sampleRate, int numCoeffs = 13);
    
    // Rhythmus-Analyse
    std::vector<float> detectOnsets(const std::vector<float>& samples, int sampleRate);
    std::vector<int> analyzeTimingPattern(const std::vector<float>& onsets, float bpm);
    std::string classifyRhythmComplexity(const std::vector<int>& pattern);
    
    // Audio-Loading internal
    bool loadWAV(const std::string& filepath, std::vector<float>& samples, int& sampleRate);
    
    // ML/NPU-Integration (falls verfügbar)
    bool useNPU_ = false;
    void initializeNPU();
    std::vector<float> runNPUInference(const std::vector<float>& features);
};

#endif // AUDIOANALYZER_H
