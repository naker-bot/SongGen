#ifndef INSTRUMENTMODEL_H
#define INSTRUMENTMODEL_H

#include <vector>
#include <string>
#include <map>
#include <memory>

/**
 * InstrumentModel - Klangmodell für ein spezifisches Instrument
 * 
 * Lernt die charakteristischen Eigenschaften eines Instruments aus Samples:
 * - Frequenzspektrum und Obertöne
 * - Attack, Decay, Sustain, Release (ADSR)
 * - Typische Spielweisen und Patterns
 * - Dynamik-Verhalten
 */

struct InstrumentCharacteristics {
    // ADSR-Envelope
    float attack;      // 0-1 Sekunden
    float decay;       // 0-2 Sekunden
    float sustain;     // 0-1 (Lautstärke-Level)
    float release;     // 0-3 Sekunden
    
    // Frequenz-Eigenschaften
    float fundamentalFreqMin;  // Hz (z.B. Gitarre E2: ~82Hz)
    float fundamentalFreqMax;  // Hz (z.B. Gitarre E4: ~330Hz)
    std::vector<float> harmonicRatios;  // Oberton-Verhältnisse [2.0, 3.0, 4.0, ...]
    
    // Spektrale Eigenschaften
    float spectralCentroid;     // "Helligkeit" des Klangs
    float spectralSpread;       // Frequenz-Verteilung
    float spectralRolloff;      // Wo fallen hohe Frequenzen ab
    
    // Dynamik
    float dynamicRange;         // dB-Unterschied leise/laut
    float velocitySensitivity;  // Wie stark reagiert auf Anschlagstärke
    
    // Spielweise
    std::vector<float> typicalNoteDurations;  // Sekunden
    std::vector<float> typicalNoteGaps;       // Pausen zwischen Noten
    float legatoProbability;    // Wie oft werden Noten gebunden (0-1)
    float staccatoProbability;  // Wie oft kurz/abgehackt (0-1)
    
    // Effekte
    bool hasVibrato;
    float vibratoRate;          // Hz
    float vibratoDepth;         // Cents
    bool hasTremolo;
    
    std::string instrumentName;
    std::string category;       // "string", "percussion", "wind", "synth"
};

class InstrumentModel {
public:
    InstrumentModel(const std::string& name, const std::string& category);
    
    /**
     * Trainiert das Modell mit Samples
     * @param samples Audio-Samples des Instruments
     * @param sampleRate Sample-Rate
     * @param noteInfo Optional: Info über gespielte Noten (für Pitch-Tracking)
     */
    void train(const std::vector<std::vector<float>>& samples, 
               int sampleRate,
               const std::vector<float>& fundamentalFreqs = {});
    
    /**
     * Generiert einen Ton mit diesem Instrument
     * @param frequency Grundfrequenz (Hz)
     * @param duration Dauer (Sekunden)
     * @param velocity Anschlagstärke (0-1)
     * @param sampleRate Ausgabe-Sample-Rate
     * @return Audio-Samples
     */
    std::vector<float> synthesize(float frequency, float duration, 
                                   float velocity, int sampleRate) const;
    
    /**
     * Generiert eine Melodie mit diesem Instrument
     * @param notes Frequenzen der Noten (Hz)
     * @param noteDurations Dauer jeder Note (Sekunden)
     * @param velocities Anschlagstärke jeder Note (0-1)
     * @param sampleRate Ausgabe-Sample-Rate
     * @return Audio-Samples
     */
    std::vector<float> synthesizeMelody(const std::vector<float>& notes,
                                        const std::vector<float>& noteDurations,
                                        const std::vector<float>& velocities,
                                        int sampleRate) const;
    
    // Charakteristiken abrufen/setzen
    const InstrumentCharacteristics& getCharacteristics() const { return characteristics_; }
    void setCharacteristics(const InstrumentCharacteristics& chars) { characteristics_ = chars; }
    
    // Speichern/Laden
    bool saveToFile(const std::string& filepath) const;
    bool loadFromFile(const std::string& filepath);
    
    // Download von vortrainierten Modellen
    static bool downloadPretrainedModel(const std::string& instrumentName, 
                                       const std::string& targetPath);
    static std::vector<std::string> listAvailableModels();
    
private:
    std::string name_;
    std::string category_;
    InstrumentCharacteristics characteristics_;
    
    // Gelernte Sample-Templates
    std::vector<std::vector<float>> sampleTemplates_;
    
    // Hilfsfunktionen
    void analyzeADSR(const std::vector<float>& sample, int sampleRate);
    void analyzeSpectrum(const std::vector<float>& sample, int sampleRate);
    void analyzePlayingStyle(const std::vector<std::vector<float>>& samples, int sampleRate);
    std::vector<float> generateEnvelope(float duration, int sampleRate) const;
    std::vector<float> generateHarmonics(float fundamental, int sampleRate, int numSamples) const;
};

/**
 * InstrumentLibrary - Verwaltet alle Instrument-Modelle
 */
class InstrumentLibrary {
public:
    InstrumentLibrary(const std::string& libraryPath);
    
    // Instrument-Modelle verwalten
    std::shared_ptr<InstrumentModel> getModel(const std::string& name);
    bool hasModel(const std::string& name) const;
    void addModel(std::shared_ptr<InstrumentModel> model);
    std::vector<std::string> listModels() const;
    
    // Vordefinierte Modelle laden
    void loadDefaultModels();  // Gitarre, Bass, Drums, Piano, etc.
    
    // Download und Update
    bool downloadModel(const std::string& name);
    bool updateAllModels();
    
    // Training mit Datenbank-Samples
    void trainFromDatabase(class MediaDatabase& db);
    
private:
    std::string libraryPath_;
    std::map<std::string, std::shared_ptr<InstrumentModel>> models_;
    
    // Standard-Modelle erstellen
    std::shared_ptr<InstrumentModel> createGuitarModel();
    std::shared_ptr<InstrumentModel> createBassModel();
    std::shared_ptr<InstrumentModel> createDrumModel(const std::string& drumType); // kick, snare, hihat
    std::shared_ptr<InstrumentModel> createPianoModel();
    std::shared_ptr<InstrumentModel> createSynthModel(const std::string& synthType);
};

#endif // INSTRUMENTMODEL_H
