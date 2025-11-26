#include "InstrumentModel.h"
#include <cmath>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <numeric>
#include <filesystem>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ==================== InstrumentModel ====================

InstrumentModel::InstrumentModel(const std::string& name, const std::string& category)
    : name_(name), category_(category) {
    
    // Standard-Werte setzen
    characteristics_.attack = 0.01f;
    characteristics_.decay = 0.1f;
    characteristics_.sustain = 0.7f;
    characteristics_.release = 0.2f;
    characteristics_.fundamentalFreqMin = 50.0f;
    characteristics_.fundamentalFreqMax = 2000.0f;
    characteristics_.spectralCentroid = 1000.0f;
    characteristics_.dynamicRange = 40.0f;
    characteristics_.velocitySensitivity = 0.8f;
    characteristics_.legatoProbability = 0.3f;
    characteristics_.staccatoProbability = 0.2f;
    characteristics_.hasVibrato = false;
    characteristics_.hasTremolo = false;
    characteristics_.instrumentName = name;
    characteristics_.category = category;
}

void InstrumentModel::train(const std::vector<std::vector<float>>& samples, 
                            int sampleRate,
                            const std::vector<float>& fundamentalFreqs) {
    
    if (samples.empty()) return;
    
    std::cout << "🎸 Trainiere Instrument-Modell: " << name_ << std::endl;
    
    // Analysiere jeden Sample
    for (size_t i = 0; i < samples.size(); i++) {
        const auto& sample = samples[i];
        if (sample.empty()) continue;
        
        analyzeADSR(sample, sampleRate);
        analyzeSpectrum(sample, sampleRate);
        
        // Speichere Template
        sampleTemplates_.push_back(sample);
    }
    
    // Analysiere Spielweise über alle Samples
    analyzePlayingStyle(samples, sampleRate);
    
    std::cout << "   ✓ ADSR: A=" << characteristics_.attack << "s D=" << characteristics_.decay 
              << "s S=" << characteristics_.sustain << " R=" << characteristics_.release << "s\n";
    std::cout << "   ✓ Frequenzbereich: " << characteristics_.fundamentalFreqMin 
              << " - " << characteristics_.fundamentalFreqMax << " Hz\n";
    std::cout << "   ✓ " << sampleTemplates_.size() << " Sample-Templates gespeichert\n";
}

void InstrumentModel::analyzeADSR(const std::vector<float>& sample, int sampleRate) {
    if (sample.empty()) return;
    
    // Finde Peak und Envelope
    float maxAmp = 0.0f;
    size_t peakIndex = 0;
    for (size_t i = 0; i < sample.size(); i++) {
        float amp = std::abs(sample[i]);
        if (amp > maxAmp) {
            maxAmp = amp;
            peakIndex = i;
        }
    }
    
    // Attack: Zeit bis Peak
    characteristics_.attack = std::max(0.001f, static_cast<float>(peakIndex) / sampleRate);
    
    // Decay: Zeit von Peak bis Sustain-Level (70% von Peak)
    float sustainLevel = maxAmp * 0.7f;
    size_t decayEnd = peakIndex;
    for (size_t i = peakIndex; i < sample.size(); i++) {
        if (std::abs(sample[i]) <= sustainLevel) {
            decayEnd = i;
            break;
        }
    }
    characteristics_.decay = static_cast<float>(decayEnd - peakIndex) / sampleRate;
    
    // Sustain: Level nach Decay
    characteristics_.sustain = sustainLevel / maxAmp;
    
    // Release: Länge des Ausklangs (letztes Viertel des Samples)
    size_t releaseStart = sample.size() * 3 / 4;
    characteristics_.release = static_cast<float>(sample.size() - releaseStart) / sampleRate;
}

void InstrumentModel::analyzeSpectrum(const std::vector<float>& sample, int sampleRate) {
    // Vereinfachte FFT-Analyse (nur Grundfrequenz und Obertöne)
    // In echter Implementierung: FFTW verwenden
    
    // Finde dominante Frequenzen durch Auto-Korrelation
    size_t windowSize = std::min(sample.size(), static_cast<size_t>(sampleRate / 10));
    
    float maxCorr = 0.0f;
    int bestLag = 0;
    
    for (int lag = 20; lag < 1000 && lag < static_cast<int>(windowSize); lag++) {
        float corr = 0.0f;
        for (size_t i = 0; i < windowSize - lag; i++) {
            corr += sample[i] * sample[i + lag];
        }
        if (corr > maxCorr) {
            maxCorr = corr;
            bestLag = lag;
        }
    }
    
    if (bestLag > 0) {
        float fundamental = static_cast<float>(sampleRate) / bestLag;
        characteristics_.fundamentalFreqMin = std::min(characteristics_.fundamentalFreqMin, fundamental * 0.8f);
        characteristics_.fundamentalFreqMax = std::max(characteristics_.fundamentalFreqMax, fundamental * 1.2f);
    }
    
    // Oberton-Struktur (typisch für verschiedene Instrumente)
    characteristics_.harmonicRatios = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
}

void InstrumentModel::analyzePlayingStyle(const std::vector<std::vector<float>>& samples, int sampleRate) {
    // Analysiere typische Note-Dauern und Pausen
    characteristics_.typicalNoteDurations.clear();
    characteristics_.typicalNoteGaps.clear();
    
    for (const auto& sample : samples) {
        float duration = static_cast<float>(sample.size()) / sampleRate;
        characteristics_.typicalNoteDurations.push_back(duration);
    }
    
    // Schätze Spielweise basierend auf Dauern
    if (!characteristics_.typicalNoteDurations.empty()) {
        float avgDuration = std::accumulate(characteristics_.typicalNoteDurations.begin(),
                                           characteristics_.typicalNoteDurations.end(), 0.0f) 
                           / characteristics_.typicalNoteDurations.size();
        
        // Kurze Noten = mehr Staccato
        if (avgDuration < 0.2f) {
            characteristics_.staccatoProbability = 0.7f;
            characteristics_.legatoProbability = 0.1f;
        } else if (avgDuration > 0.5f) {
            // Lange Noten = mehr Legato
            characteristics_.legatoProbability = 0.6f;
            characteristics_.staccatoProbability = 0.1f;
        }
    }
}

std::vector<float> InstrumentModel::generateEnvelope(float duration, int sampleRate) const {
    int numSamples = static_cast<int>(duration * sampleRate);
    std::vector<float> envelope(numSamples, 0.0f);
    
    int attackSamples = static_cast<int>(characteristics_.attack * sampleRate);
    int decaySamples = static_cast<int>(characteristics_.decay * sampleRate);
    int releaseSamples = static_cast<int>(characteristics_.release * sampleRate);
    int sustainSamples = numSamples - attackSamples - decaySamples - releaseSamples;
    
    if (sustainSamples < 0) sustainSamples = 0;
    
    int idx = 0;
    
    // Attack
    for (int i = 0; i < attackSamples && idx < numSamples; i++, idx++) {
        envelope[idx] = static_cast<float>(i) / attackSamples;
    }
    
    // Decay
    for (int i = 0; i < decaySamples && idx < numSamples; i++, idx++) {
        float t = static_cast<float>(i) / decaySamples;
        envelope[idx] = 1.0f - t * (1.0f - characteristics_.sustain);
    }
    
    // Sustain
    for (int i = 0; i < sustainSamples && idx < numSamples; i++, idx++) {
        envelope[idx] = characteristics_.sustain;
    }
    
    // Release
    for (int i = 0; i < releaseSamples && idx < numSamples; i++, idx++) {
        float t = static_cast<float>(i) / releaseSamples;
        envelope[idx] = characteristics_.sustain * (1.0f - t);
    }
    
    return envelope;
}

std::vector<float> InstrumentModel::generateHarmonics(float fundamental, int sampleRate, int numSamples) const {
    std::vector<float> output(numSamples, 0.0f);
    
    // Generiere Grundton + Obertöne
    for (size_t h = 0; h < characteristics_.harmonicRatios.size(); h++) {
        float freq = fundamental * characteristics_.harmonicRatios[h];
        if (freq > sampleRate / 2) break;  // Nyquist-Grenze
        
        // Amplitude fällt mit höheren Obertönen
        float amplitude = 1.0f / (h + 1);
        
        for (int i = 0; i < numSamples; i++) {
            float t = static_cast<float>(i) / sampleRate;
            output[i] += amplitude * std::sin(2.0f * M_PI * freq * t);
        }
    }
    
    // Normalisieren
    float maxVal = 0.0f;
    for (float val : output) {
        maxVal = std::max(maxVal, std::abs(val));
    }
    if (maxVal > 0.0f) {
        for (float& val : output) {
            val /= maxVal;
        }
    }
    
    return output;
}

std::vector<float> InstrumentModel::synthesize(float frequency, float duration, 
                                                float velocity, int sampleRate) const {
    
    int numSamples = static_cast<int>(duration * sampleRate);
    
    // Generiere Harmonics
    auto harmonics = generateHarmonics(frequency, sampleRate, numSamples);
    
    // Generiere Envelope
    auto envelope = generateEnvelope(duration, sampleRate);
    
    // Kombiniere mit Velocity
    std::vector<float> output(numSamples);
    for (int i = 0; i < numSamples; i++) {
        output[i] = harmonics[i] * envelope[i] * velocity;
    }
    
    return output;
}

std::vector<float> InstrumentModel::synthesizeMelody(const std::vector<float>& notes,
                                                     const std::vector<float>& noteDurations,
                                                     const std::vector<float>& velocities,
                                                     int sampleRate) const {
    
    std::vector<float> output;
    
    for (size_t i = 0; i < notes.size(); i++) {
        float freq = notes[i];
        float duration = noteDurations[i];
        float velocity = velocities[i];
        
        auto noteSamples = synthesize(freq, duration, velocity, sampleRate);
        output.insert(output.end(), noteSamples.begin(), noteSamples.end());
    }
    
    return output;
}

bool InstrumentModel::saveToFile(const std::string& filepath) const {
    std::ofstream file(filepath, std::ios::binary);
    if (!file) return false;
    
    // Speichere Charakteristiken (vereinfacht)
    file.write(reinterpret_cast<const char*>(&characteristics_), sizeof(InstrumentCharacteristics));
    
    std::cout << "💾 Instrument-Modell gespeichert: " << filepath << std::endl;
    return true;
}

bool InstrumentModel::loadFromFile(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) return false;
    
    file.read(reinterpret_cast<char*>(&characteristics_), sizeof(InstrumentCharacteristics));
    
    std::cout << "📂 Instrument-Modell geladen: " << filepath << std::endl;
    return true;
}

// ==================== InstrumentLibrary ====================

InstrumentLibrary::InstrumentLibrary(const std::string& libraryPath)
    : libraryPath_(libraryPath) {
    
    std::filesystem::create_directories(libraryPath);
    std::cout << "🎹 Instrument-Library initialisiert: " << libraryPath << std::endl;
}

std::shared_ptr<InstrumentModel> InstrumentLibrary::getModel(const std::string& name) {
    auto it = models_.find(name);
    if (it != models_.end()) {
        return it->second;
    }
    
    // Versuche von Datei zu laden
    std::string filepath = libraryPath_ + "/" + name + ".imodel";
    if (std::filesystem::exists(filepath)) {
        auto model = std::make_shared<InstrumentModel>(name, "loaded");
        if (model->loadFromFile(filepath)) {
            models_[name] = model;
            return model;
        }
    }
    
    return nullptr;
}

bool InstrumentLibrary::hasModel(const std::string& name) const {
    return models_.find(name) != models_.end() ||
           std::filesystem::exists(libraryPath_ + "/" + name + ".imodel");
}

void InstrumentLibrary::addModel(std::shared_ptr<InstrumentModel> model) {
    if (!model) return;
    
    const auto& chars = model->getCharacteristics();
    models_[chars.instrumentName] = model;
    
    // Speichere auf Disk
    std::string filepath = libraryPath_ + "/" + chars.instrumentName + ".imodel";
    model->saveToFile(filepath);
}

std::vector<std::string> InstrumentLibrary::listModels() const {
    std::vector<std::string> list;
    for (const auto& [name, _] : models_) {
        list.push_back(name);
    }
    return list;
}

void InstrumentLibrary::loadDefaultModels() {
    std::cout << "🎼 Lade Standard-Instrument-Modelle..." << std::endl;
    
    // Erstelle Standard-Modelle
    addModel(createGuitarModel());
    addModel(createBassModel());
    addModel(createDrumModel("kick"));
    addModel(createDrumModel("snare"));
    addModel(createDrumModel("hihat"));
    addModel(createPianoModel());
    addModel(createSynthModel("lead"));
    addModel(createSynthModel("pad"));
    
    std::cout << "   ✓ " << models_.size() << " Standard-Modelle geladen\n";
}

std::shared_ptr<InstrumentModel> InstrumentLibrary::createGuitarModel() {
    auto model = std::make_shared<InstrumentModel>("guitar", "string");
    
    InstrumentCharacteristics chars = model->getCharacteristics();
    chars.attack = 0.005f;
    chars.decay = 0.1f;
    chars.sustain = 0.6f;
    chars.release = 0.3f;
    chars.fundamentalFreqMin = 82.0f;   // E2
    chars.fundamentalFreqMax = 1320.0f; // E6
    chars.harmonicRatios = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};  // Reichhaltige Obertöne
    chars.spectralCentroid = 800.0f;
    chars.dynamicRange = 50.0f;
    chars.velocitySensitivity = 0.9f;
    chars.legatoProbability = 0.4f;
    chars.staccatoProbability = 0.3f;
    chars.hasVibrato = true;
    chars.vibratoRate = 5.0f;
    chars.vibratoDepth = 20.0f;
    
    model->setCharacteristics(chars);
    return model;
}

std::shared_ptr<InstrumentModel> InstrumentLibrary::createBassModel() {
    auto model = std::make_shared<InstrumentModel>("bass", "string");
    
    InstrumentCharacteristics chars = model->getCharacteristics();
    chars.attack = 0.01f;
    chars.decay = 0.15f;
    chars.sustain = 0.7f;
    chars.release = 0.2f;
    chars.fundamentalFreqMin = 41.0f;  // E1
    chars.fundamentalFreqMax = 330.0f; // E4
    chars.harmonicRatios = {1.0f, 2.0f, 3.0f, 4.0f};  // Weniger Obertöne = dunkler
    chars.spectralCentroid = 300.0f;
    chars.dynamicRange = 45.0f;
    chars.velocitySensitivity = 0.85f;
    chars.legatoProbability = 0.6f;   // Bass oft legato
    chars.staccatoProbability = 0.2f;
    
    model->setCharacteristics(chars);
    return model;
}

std::shared_ptr<InstrumentModel> InstrumentLibrary::createDrumModel(const std::string& drumType) {
    auto model = std::make_shared<InstrumentModel>("drum_" + drumType, "percussion");
    
    InstrumentCharacteristics chars = model->getCharacteristics();
    
    if (drumType == "kick") {
        chars.attack = 0.001f;
        chars.decay = 0.05f;
        chars.sustain = 0.0f;
        chars.release = 0.1f;
        chars.fundamentalFreqMin = 40.0f;
        chars.fundamentalFreqMax = 100.0f;
        chars.harmonicRatios = {1.0f, 1.5f};  // Wenig Harmonics
        chars.spectralCentroid = 80.0f;
    } else if (drumType == "snare") {
        chars.attack = 0.001f;
        chars.decay = 0.08f;
        chars.sustain = 0.1f;
        chars.release = 0.15f;
        chars.fundamentalFreqMin = 150.0f;
        chars.fundamentalFreqMax = 300.0f;
        chars.harmonicRatios = {1.0f, 2.0f, 4.0f, 8.0f};  // + Noise
        chars.spectralCentroid = 2000.0f;
    } else if (drumType == "hihat") {
        chars.attack = 0.001f;
        chars.decay = 0.05f;
        chars.sustain = 0.0f;
        chars.release = 0.05f;
        chars.fundamentalFreqMin = 5000.0f;
        chars.fundamentalFreqMax = 12000.0f;
        chars.harmonicRatios = {1.0f};  // Hauptsächlich Rauschen
        chars.spectralCentroid = 8000.0f;
    }
    
    chars.dynamicRange = 60.0f;
    chars.velocitySensitivity = 1.0f;
    chars.staccatoProbability = 1.0f;  // Drums immer kurz
    
    model->setCharacteristics(chars);
    return model;
}

std::shared_ptr<InstrumentModel> InstrumentLibrary::createPianoModel() {
    auto model = std::make_shared<InstrumentModel>("piano", "keyboard");
    
    InstrumentCharacteristics chars = model->getCharacteristics();
    chars.attack = 0.002f;
    chars.decay = 0.2f;
    chars.sustain = 0.4f;
    chars.release = 0.5f;
    chars.fundamentalFreqMin = 27.5f;   // A0
    chars.fundamentalFreqMax = 4186.0f; // C8
    chars.harmonicRatios = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    chars.spectralCentroid = 1500.0f;
    chars.dynamicRange = 80.0f;
    chars.velocitySensitivity = 1.0f;
    chars.legatoProbability = 0.5f;
    chars.staccatoProbability = 0.3f;
    
    model->setCharacteristics(chars);
    return model;
}

std::shared_ptr<InstrumentModel> InstrumentLibrary::createSynthModel(const std::string& synthType) {
    auto model = std::make_shared<InstrumentModel>("synth_" + synthType, "synth");
    
    InstrumentCharacteristics chars = model->getCharacteristics();
    
    if (synthType == "lead") {
        chars.attack = 0.01f;
        chars.decay = 0.1f;
        chars.sustain = 0.8f;
        chars.release = 0.2f;
        chars.fundamentalFreqMin = 200.0f;
        chars.fundamentalFreqMax = 2000.0f;
        chars.harmonicRatios = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
        chars.spectralCentroid = 1200.0f;
        chars.hasVibrato = true;
        chars.vibratoRate = 6.0f;
        chars.vibratoDepth = 15.0f;
    } else if (synthType == "pad") {
        chars.attack = 0.5f;   // Langsamer Attack
        chars.decay = 0.3f;
        chars.sustain = 0.9f;
        chars.release = 1.0f;   // Langer Release
        chars.fundamentalFreqMin = 100.0f;
        chars.fundamentalFreqMax = 1000.0f;
        chars.harmonicRatios = {1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 4.0f};  // Dichter Sound
        chars.spectralCentroid = 600.0f;
        chars.legatoProbability = 0.9f;  // Pads meistens gebunden
    }
    
    chars.dynamicRange = 60.0f;
    chars.velocitySensitivity = 0.8f;
    
    model->setCharacteristics(chars);
    return model;
}

void InstrumentLibrary::trainFromDatabase(MediaDatabase& db) {
    std::cout << "🎓 Trainiere Instrument-Modelle aus Datenbank..." << std::endl;
    
    // Hier würde Instrument-Extraktion aus der Datenbank stattfinden
    // und die extrahierten Samples würden zum Training verwendet
    
    std::cout << "   ℹ️ Feature noch nicht implementiert - verwende Standard-Modelle\n";
}
