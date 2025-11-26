#ifndef SONGGENERATOR_H
#define SONGGENERATOR_H

#include "MediaDatabase.h"
#include "TrainingModel.h"
#include "InstrumentModel.h"
#include "ChordProgressionEngine.h"
#include "RhythmEngine.h"
#include "SongStructureEngine.h"
#include "AudioEffects.h"
#include "MIDIExporter.h"
#include "BassLineEngine.h"
#include "PatternCaptureEngine.h"
#include <string>
#include <vector>
#include <map>
#include <functional>

/**
 * Parameter für Song-Generierung
 */
struct GenerationParams {
    // Stil-Auswahl
    std::string genre = "Trap";  // Trap, Techno, Rock, Pop, Klassik, Metal, New Metal, Trance, etc.
    std::string subgenre = "";
    
    // Rhythmus-Parameter
    float bpm = 120.0f;
    std::string intensity = "mittel";  // hart, mittel, soft
    std::string bassLevel = "mittel";   // basslastig, mittel, soft
    
    // Kompositions-Parameter
    int duration = 180;  // Sekunden
    bool useVocals = false;
    std::string vocalStyle = "";  // rap, gesang, choir, etc.
    
    // Struktur
    bool useIntro = true;
    bool useOutro = true;
    int numVerses = 2;
    int numChorus = 3;
    bool useBridge = true;
    bool useBreakdown = false;
    
    // Erweiterte Parameter
    float energy = 0.7f;       // 0.0 - 1.0
    float complexity = 0.5f;   // 0.0 - 1.0 (Anzahl Layer/Instrumente)
    float variation = 0.5f;    // 0.0 - 1.0 (Wie abwechslungsreich)
    
    // Source-Dateien (optional: manuell ausgewählte Samples)
    std::vector<int64_t> sourceMediaIds;
};

/**
 * SongGenerator - KI-basierte Song-Komposition
 * 
 * Nimmt Trainingsdaten aus der Datenbank und generiert
 * neue Songs basierend auf den gewünschten Parametern.
 * 
 * Prozess:
 * 1. Auswahl passender Source-Samples aus Datenbank
 * 2. Feature-Extraktion und Musteranalyse
 * 3. NPU-basierte Melodie-Generierung
 * 4. Rhythmus-Synthese
 * 5. Instrument-Layering
 * 6. Mixing und Mastering
 */
class SongGenerator {
public:
    SongGenerator(MediaDatabase& db);
    ~SongGenerator();
    
    // GPU/NPU-Beschleunigung für AI-Generierung
    void initializeAccelerator();
    bool hasAccelerator() const { return useAccelerator_; }
    
    /**
     * Generiert einen Song
     * @param params Generierungs-Parameter
     * @param outputPath Ausgabe-Pfad für WAV-Datei
     * @param progressCallback Optional: callback(phase, progress 0-1)
     * @return true bei Erfolg
     */
    bool generate(
        const GenerationParams& params,
        const std::string& outputPath,
        std::function<void(const std::string&, float)> progressCallback = nullptr
    );
    
    /**
     * Vorschau-Generierung (nur 30 Sekunden)
     * Schneller für Experimente
     */
    bool generatePreview(const GenerationParams& params, const std::string& outputPath);
    
    // Hilfsfunktionen
    std::vector<MediaMetadata> selectSourceSamples(const GenerationParams& params, int count = 20);
    bool validateParams(const GenerationParams& params);
    
    // Pattern Engine Access
    SongGen::PatternCaptureEngine* getPatternEngine() { return patternEngine_.get(); }
    
private:
    MediaDatabase& db_;
    
    // GPU/NPU-Beschleunigung
    bool useAccelerator_ = false;
    std::string acceleratorDevice_ = "CPU";
    
    // ML-Modell für Generation
    std::unique_ptr<TrainingModel> mlModel_;
    std::string modelPath_;
    
    // Instrument-Modelle
    std::unique_ptr<InstrumentLibrary> instrumentLibrary_;
    
    // New AI Music Generation Systems
    std::unique_ptr<SongGen::ChordProgressionEngine> chordEngine_;
    std::unique_ptr<SongGen::RhythmEngine> rhythmEngine_;
    std::unique_ptr<SongGen::SongStructureEngine> structureEngine_;
    std::unique_ptr<SongGen::MixMasterEngine> mixMasterEngine_;
    std::unique_ptr<SongGen::BassLineEngine> bassEngine_;
    std::unique_ptr<SongGen::MIDIExporter> midiExporter_;
    std::unique_ptr<SongGen::PatternCaptureEngine> patternEngine_;
    
    // Generierungs-Pipeline
    bool generateMelody(const GenerationParams& params, std::vector<float>& samples);
    bool generateRhythm(const GenerationParams& params, std::vector<float>& samples);
    bool generateBass(const GenerationParams& params, std::vector<float>& samples);
    bool layerInstruments(const GenerationParams& params, std::vector<float>& samples);
    bool addVocals(const GenerationParams& params, std::vector<float>& samples);
    bool mixAndMaster(std::vector<float>& samples);
    
    // Audio-Synthese
    bool synthesizeTone(float frequency, float duration, int sampleRate, std::vector<float>& output);
    bool applyEnvelope(std::vector<float>& samples, float attack, float decay, float sustain, float release);
    bool applyFilter(std::vector<float>& samples, const std::string& type, float cutoff);
    
    // Audio-Export
    bool exportWAV(const std::string& path, const std::vector<float>& samples, int sampleRate = 44100);
    bool exportMP3(const std::string& path, const std::vector<float>& samples, int sampleRate = 44100, int bitrate = 192);
};

#endif // SONGGENERATOR_H
