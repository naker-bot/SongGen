#ifndef INSTRUMENTEXTRACTOR_H
#define INSTRUMENTEXTRACTOR_H

#include <vector>
#include <string>
#include <map>

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
    static float getDominantFrequency(const std::vector<float>& samples, int sampleRate);
    static std::vector<size_t> findOnsets(const std::vector<float>& samples, int sampleRate);
    static std::vector<float> applyBandpassFilter(
        const std::vector<float>& samples, float lowFreq, float highFreq, int sampleRate);
};

#endif // INSTRUMENTEXTRACTOR_H
