#include "SongGenerator.h"
#include <cmath>
#include <random>
#include <fstream>
#include <iostream>
#include <functional>
#include <lame/lame.h>

#ifdef WITH_OPENVINO
#include <openvino/openvino.hpp>
using namespace ov;
#endif

SongGenerator::SongGenerator(MediaDatabase& db) : db_(db) {
    initializeAccelerator();
    
    // Versuche ML-Modell zu laden
    modelPath_ = std::string(getenv("HOME")) + "/.songgen/model.sgml";
    mlModel_ = std::make_unique<TrainingModel>(db_);
    
    if (mlModel_->loadModel(modelPath_)) {
        std::cout << "✅ ML-Modell geladen: " << modelPath_ << std::endl;
    } else {
        std::cout << "ℹ️ Kein ML-Modell gefunden, nutze Synthese-Fallback" << std::endl;
    }
}

SongGenerator::~SongGenerator() {
}

void SongGenerator::initializeAccelerator() {
#ifdef WITH_OPENVINO
    try {
        Core core;
        auto devices = core.get_available_devices();
        
        // Priorität: NPU > GPU > CPU
        for (const auto& device : devices) {
            if (device.find("NPU") != std::string::npos) {
                acceleratorDevice_ = device;
                useAccelerator_ = true;
                std::cout << "🎼 Generator nutzt Intel NPU: " << device << std::endl;
                return;
            }
        }
        
        for (const auto& device : devices) {
            if (device.find("GPU") != std::string::npos) {
                acceleratorDevice_ = device;
                useAccelerator_ = true;
                std::cout << "🎼 Generator nutzt Intel GPU: " << device << std::endl;
                return;
            }
        }
        
        std::cout << "🎼 Generator nutzt CPU (kein NPU/GPU gefunden)" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "⚠️ OpenVINO Fehler im Generator: " << e.what() << std::endl;
    }
#else
    std::cout << "🎼 Generator nutzt CPU (OpenVINO nicht verfügbar)" << std::endl;
#endif
    useAccelerator_ = false;
    acceleratorDevice_ = "CPU";
}

bool SongGenerator::generate(
    const GenerationParams& params,
    const std::string& outputPath,
    std::function<void(const std::string&, float)> progressCallback) {
    
    if (!validateParams(params)) {
        std::cerr << "Invalid generation parameters" << std::endl;
        return false;
    }
    
    int sampleRate = 44100;
    size_t numSamples = params.duration * sampleRate;
    std::vector<float> samples(numSamples, 0.0f);
    
    // Phase 1: Melodie generieren
    if (progressCallback) progressCallback("Generating melody...", 0.2f);
    generateMelody(params, samples);
    
    // Phase 2: Rhythmus
    if (progressCallback) progressCallback("Generating rhythm...", 0.4f);
    generateRhythm(params, samples);
    
    // Phase 3: Bass
    if (progressCallback) progressCallback("Generating bass...", 0.6f);
    generateBass(params, samples);
    
    // Phase 4: Instrumente
    if (progressCallback) progressCallback("Layering instruments...", 0.75f);
    layerInstruments(params, samples);
    
    // Phase 5: Vocals
    if (params.useVocals) {
        if (progressCallback) progressCallback("Adding vocals...", 0.85f);
        addVocals(params, samples);
    }
    
    // Phase 6: Mixing & Mastering
    if (progressCallback) progressCallback("Mixing and mastering...", 0.95f);
    mixAndMaster(samples);
    
    // Fade-In am Anfang (100ms) um Rauschen zu vermeiden
    int fadeInSamples = static_cast<int>(sampleRate * 0.1f);  // 100ms
    for (int i = 0; i < fadeInSamples && i < static_cast<int>(samples.size()); ++i) {
        float fadeGain = static_cast<float>(i) / fadeInSamples;
        samples[i] *= fadeGain;
    }
    
    // Export als MP3
    if (progressCallback) progressCallback("Exporting to MP3...", 0.99f);
    bool success = exportMP3(outputPath, samples, sampleRate, 192);
    
    if (progressCallback) progressCallback("Done!", 1.0f);
    
    return success;
}

bool SongGenerator::generatePreview(const GenerationParams& params, const std::string& outputPath) {
    GenerationParams previewParams = params;
    previewParams.duration = 30;  // 30 Sekunden
    return generate(previewParams, outputPath);
}

std::vector<MediaMetadata> SongGenerator::selectSourceSamples(const GenerationParams& params, int count) {
    // Wähle passende Samples aus Datenbank basierend auf Genre, BPM, etc.
    auto candidates = db_.searchByGenre(params.genre);
    
    // Filter nach BPM (±20 BPM)
    std::vector<MediaMetadata> filtered;
    for (const auto& meta : candidates) {
        if (std::abs(meta.bpm - params.bpm) < 20.0f) {
            filtered.push_back(meta);
        }
    }
    
    // Limitiere auf count
    if (filtered.size() > static_cast<size_t>(count)) {
        filtered.resize(count);
    }
    
    return filtered;
}

bool SongGenerator::validateParams(const GenerationParams& params) {
    if (params.duration <= 0 || params.duration > 600) return false;
    if (params.bpm < 60.0f || params.bpm > 200.0f) return false;
    if (params.energy < 0.0f || params.energy > 1.0f) return false;
    return true;
}

bool SongGenerator::generateMelody(const GenerationParams& params, std::vector<float>& samples) {
    // Versuche ML-basierte Generierung
    if (mlModel_ && mlModel_->isModelLoaded()) {
        try {
            // Generiere Latent Vector
            std::random_device rd;
            std::mt19937 gen(rd());
            std::normal_distribution<float> dist(0.0f, 1.0f);
            
            std::vector<float> latentVector(32);
            for (auto& val : latentVector) {
                val = dist(gen);
            }
            
            // ML-Inferenz mit NPU/GPU - returns normalized feature vector
            auto featureVector = mlModel_->generate(latentVector, params.genre, params.bpm);
            
            // Konvertiere Features zu Audio-Samples
            int sampleRate = 44100;
            size_t position = 0;
            
            // Nutze Feature-Vektor für Synthese (erste 13 Werte sind MFCC-ähnlich)
            float baseFreq = 440.0f;  // A4 Grundfrequenz
            float noteDuration = 60.0f / params.bpm / 4.0f;  // 16tel-Noten
            
            while (position < samples.size()) {
                size_t samplesPerNote = static_cast<size_t>(noteDuration * sampleRate);
                
                // Nutze Features für Frequenz-Variation
                size_t featureIdx = (position / samplesPerNote) % featureVector.size();
                float freqMultiplier = 1.0f + (featureVector[featureIdx] * 0.3f);  // ±30% Variation
                
                std::vector<float> tone;
                synthesizeTone(baseFreq * freqMultiplier, noteDuration, sampleRate, tone);
                
                float level = 0.2f + (params.energy * 0.3f);
                for (size_t j = 0; j < tone.size() && (position + j) < samples.size(); ++j) {
                    samples[position + j] += tone[j] * level;
                }
                
                position += samplesPerNote;
            }
            
            std::cout << "🎵 ML-basierte Melodie generiert mit " << acceleratorDevice_ << std::endl;
            return true;
            
        } catch (const std::exception& e) {
            std::cerr << "⚠️ ML-Generierung fehlgeschlagen: " << e.what() << ", nutze Fallback" << std::endl;
        }
    }
    
    // Fallback: Genre-basierte Melodie-Generierung mit Tonleitern
    std::cout << "🎵 Synthese-basierte Melodie (kein ML-Modell)" << std::endl;
    
    // Seed basierend auf Parametern für einzigartige aber reproduzierbare Melodien
    std::random_device rd;
    unsigned int seed = rd() ^ static_cast<unsigned int>(params.bpm * 1000) ^ 
                       static_cast<unsigned int>(params.energy * 1000) ^
                       std::hash<std::string>{}(params.genre);
    std::mt19937 gen(seed);
    
    // Wähle Tonleiter basierend auf Genre
    std::vector<float> scale;
    float rootNote = 440.0f * (0.8f + (params.energy * 0.4f));  // Variiere Root Note
    
    if (params.genre == "Trap") {
        // Moll-Pentatonik (dunkel)
        scale = {rootNote, rootNote*1.2f, rootNote*1.33f, rootNote*1.5f, rootNote*1.78f};
    } else if (params.genre == "Techno") {
        // Techno: minimalistisch, repetitiv, tiefere Töne
        scale = {rootNote*0.5f, rootNote*0.6f, rootNote*0.75f, rootNote*0.89f, rootNote};
    } else if (params.genre == "Pop" || params.genre == "Rock") {
        // Dur-Tonleiter (fröhlich)
        scale = {rootNote, rootNote*1.125f, rootNote*1.25f, rootNote*1.33f, 
                 rootNote*1.5f, rootNote*1.67f, rootNote*1.875f};
    } else if (params.genre == "Metal") {
        // Phrygisch (Metal-typisch)
        scale = {rootNote*0.5f, rootNote*0.53f, rootNote*0.6f, rootNote*0.67f, 
                 rootNote*0.75f, rootNote*0.8f, rootNote*0.89f};
    } else {
        // Chromatisch
        scale = {rootNote*0.5f, rootNote*0.6f, rootNote*0.75f, rootNote, rootNote*1.5f};
    }
    
    // Erstelle mehrere melodische Phrasen
    std::vector<std::vector<size_t>> phrases;
    std::uniform_int_distribution<size_t> noteDist(0, scale.size() - 1);
    
    // 4 verschiedene 4-Noten-Phrasen
    for (int p = 0; p < 4; ++p) {
        std::vector<size_t> phrase;
        size_t lastNote = noteDist(gen);
        for (int n = 0; n < 4; ++n) {
            // Melodische Bewegung: bevorzuge Nachbarnoten
            std::uniform_int_distribution<int> stepDist(-2, 2);
            int step = stepDist(gen);
            lastNote = std::clamp<size_t>(lastNote + step, 0, scale.size() - 1);
            phrase.push_back(lastNote);
        }
        phrases.push_back(phrase);
    }
    
    int sampleRate = 44100;
    float beatDuration = 60.0f / params.bpm;
    size_t position = 0;
    int phraseIndex = 0;
    int noteInPhrase = 0;
    
    // Notenlängen basierend auf Genre
    std::vector<float> noteLengths;
    if (params.genre == "Techno" || params.genre == "House") {
        noteLengths = {0.25f, 0.25f, 0.5f, 0.25f};  // 16tel und 8tel
    } else if (params.genre == "Ambient") {
        noteLengths = {2.0f, 1.5f, 1.0f, 2.0f};  // Lange Noten
    } else {
        noteLengths = {0.5f, 0.25f, 0.5f, 0.75f};  // Mixed
    }
    
    // Generiere Melodie mit strukturierter Variation
    while (position < samples.size()) {
        // Wähle Phrase (wechsle alle 4 Noten)
        if (noteInPhrase >= 4) {
            noteInPhrase = 0;
            // Wechsle Phrase mit Wahrscheinlichkeit
            std::uniform_int_distribution<int> phraseDist(0, 100);
            if (phraseDist(gen) < 40) {  // 40% Chance für neue Phrase
                phraseIndex = (phraseIndex + 1) % phrases.size();
            }
        }
        
        size_t scaleIndex = phrases[phraseIndex][noteInPhrase];
        float frequency = scale[scaleIndex];
        float noteDuration = noteLengths[noteInPhrase] * beatDuration;
        
        size_t samplesPerNote = static_cast<size_t>(noteDuration * sampleRate);
        
        std::vector<float> tone;
        synthesizeTone(frequency, noteDuration, sampleRate, tone);
        
        // Dynamik: variiere Lautstärke
        float baseLevel = 0.2f + (params.energy * 0.3f);
        float dynamics = 0.8f + (static_cast<float>(noteInPhrase) / 8.0f);  // Leichte Betonung
        float level = baseLevel * dynamics;
        
        for (size_t j = 0; j < tone.size() && (position + j) < samples.size(); ++j) {
            samples[position + j] += tone[j] * level;
        }
        
        position += samplesPerNote;
        noteInPhrase++;
    }
    
    return true;
}

bool SongGenerator::generateRhythm(const GenerationParams& params, std::vector<float>& samples) {
    int sampleRate = 44100;
    float beatDuration = 60.0f / params.bpm;
    size_t samplesPerBeat = static_cast<size_t>(beatDuration * sampleRate);
    
    // Seed für Variation
    std::random_device rd;
    unsigned int seed = rd() ^ static_cast<unsigned int>(params.bpm * 456);
    std::mt19937 rhythmGen(seed);
    std::uniform_int_distribution<int> fillDist(0, 100);
    
    // Genre-spezifische Rhythmus-Pattern
    for (size_t beat = 0; beat * samplesPerBeat < samples.size(); ++beat) {
        size_t position = beat * samplesPerBeat;
        
        // Kick Drum (Beat 1 und 3 in 4/4, manchmal auch Variationen)
        bool playKick = (beat % 4 == 0 || beat % 4 == 2);
        
        // Gelegentlich zusätzliche Kicks (Fills)
        if (!playKick && (beat % 16 >= 12) && fillDist(rhythmGen) < 30) {
            playKick = true;
        }
        
        if (playKick) {
            std::vector<float> kick;
            float kickFreq = 50.0f + (fillDist(rhythmGen) % 10);  // Leichte Pitch-Variation
            synthesizeTone(kickFreq, 0.1f, sampleRate, kick);
            
            float kickLevel = params.intensity == "hart" ? 0.7f : 0.5f;
            for (size_t j = 0; j < kick.size() && (position + j) < samples.size(); ++j) {
                samples[position + j] += kick[j] * kickLevel;
            }
        }
        
        // Snare (Beat 2 und 4)
        if (beat % 4 == 1 || beat % 4 == 3) {
            std::vector<float> snare;
            
            // Snare = High-Freq Noise + Tone
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_real_distribution<float> dist(-0.5f, 0.5f);
            
            size_t snareLen = static_cast<size_t>(0.1f * sampleRate);
            snare.resize(snareLen);
            for (size_t i = 0; i < snareLen; ++i) {
                snare[i] = dist(gen) * std::exp(-5.0f * i / snareLen);  // Decaying noise
            }
            
            for (size_t j = 0; j < snare.size() && (position + j) < samples.size(); ++j) {
                samples[position + j] += snare[j] * 0.4f;
            }
        }
        
        // Hi-Hat (Off-beats für Techno/Trap)
        if (params.genre == "Techno" || params.genre == "Trap" || params.genre == "Trance") {
            if (beat % 2 == 1) {
                std::vector<float> hihat;
                
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_real_distribution<float> dist(-0.2f, 0.2f);
                
                size_t hihatLen = static_cast<size_t>(0.05f * sampleRate);
                hihat.resize(hihatLen);
                for (size_t i = 0; i < hihatLen; ++i) {
                    hihat[i] = dist(gen) * std::exp(-10.0f * i / hihatLen);
                }
                
                for (size_t j = 0; j < hihat.size() && (position + j) < samples.size(); ++j) {
                    samples[position + j] += hihat[j] * 0.25f;
                }
            }
        }
        
        // Clap für Trap
        if (params.genre == "Trap" && beat % 8 == 4) {
            std::vector<float> clap;
            synthesizeTone(1000.0f, 0.05f, sampleRate, clap);
            
            for (size_t j = 0; j < clap.size() && (position + j) < samples.size(); ++j) {
                samples[position + j] += clap[j] * 0.3f;
            }
        }
    }
    
    return true;
}

bool SongGenerator::generateBass(const GenerationParams& params, std::vector<float>& samples) {
    // Bass-Line mit Variation
    int sampleRate = 44100;
    float beatDuration = 60.0f / params.bpm;
    
    // Seed für reproduzierbare Variation
    std::random_device rd;
    unsigned int seed = rd() ^ static_cast<unsigned int>(params.bpm * 789);
    std::mt19937 gen(seed);
    
    // Bass-Frequenzen (Grundton und Variationen)
    std::vector<float> bassNotes;
    if (params.genre == "Techno" || params.genre == "Trap") {
        bassNotes = {65.4f, 73.4f, 82.4f, 65.4f};  // C2, D2, E2, C2
    } else if (params.genre == "Metal" || params.genre == "Rock") {
        bassNotes = {82.4f, 73.4f, 69.3f, 61.7f};  // E2, D2, C#2, B1
    } else {
        bassNotes = {65.4f, 69.3f, 73.4f, 65.4f};  // C2, C#2, D2, C2
    }
    
    std::uniform_int_distribution<size_t> noteDist(0, bassNotes.size() - 1);
    
    size_t position = 0;
    int noteIndex = 0;
    
    // Generiere Bass-Pattern
    while (position < samples.size()) {
        float frequency = bassNotes[noteIndex % bassNotes.size()];
        
        // Notenlänge: meistens ganze oder halbe Beats
        float noteDuration = (noteIndex % 4 == 0) ? beatDuration * 2.0f : beatDuration;
        size_t samplesPerNote = static_cast<size_t>(noteDuration * sampleRate);
        
        std::vector<float> bassNote;
        synthesizeTone(frequency, noteDuration, sampleRate, bassNote);
        
        // Mix Level
        float bassLevel = 0.4f;
        if (params.bassLevel == "basslastig") bassLevel = 0.6f;
        else if (params.bassLevel == "soft") bassLevel = 0.2f;
        
        for (size_t j = 0; j < bassNote.size() && (position + j) < samples.size(); ++j) {
            samples[position + j] += bassNote[j] * bassLevel;
        }
        
        position += samplesPerNote;
        noteIndex++;
        
        // Gelegentlich Pattern wechseln
        if (noteIndex % 16 == 0) {
            std::shuffle(bassNotes.begin(), bassNotes.end(), gen);
        }
    }
    
    return true;
}

bool SongGenerator::layerInstruments(const GenerationParams& params, std::vector<float>& samples) {
    // Lade passende Source-Samples aus Datenbank
    auto sourceSamples = selectSourceSamples(params, 10);
    
    if (sourceSamples.empty()) {
        return true;  // Keine Samples verfügbar, verwende nur Synthese
    }
    
    int sampleRate = 44100;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> sampleDist(0, sourceSamples.size() - 1);
    
    // Lade und mixe zufällige Samples
    for (int layer = 0; layer < static_cast<int>(params.complexity * 5); ++layer) {
        const auto& meta = sourceSamples[sampleDist(gen)];
        
        // Lade Audio
        std::ifstream file(meta.filepath, std::ios::binary);
        if (!file.is_open()) continue;
        
        // Einfaches WAV-Loading
        file.seekg(44);  // Skip header
        
        std::vector<float> sourceAudio;
        short sample;
        while (file.read(reinterpret_cast<char*>(&sample), sizeof(short))) {
            sourceAudio.push_back(sample / 32768.0f);
        }
        file.close();
        
        if (sourceAudio.empty()) continue;
        
        // Mix mit reduziertem Level
        float mixLevel = 0.1f / (layer + 1);  // Weniger Level für mehr Layer
        
        for (size_t i = 0; i < std::min(samples.size(), sourceAudio.size()); ++i) {
            samples[i] += sourceAudio[i] * mixLevel;
        }
    }
    
    return true;
}

bool SongGenerator::addVocals(const GenerationParams& params, std::vector<float>& samples) {
    // Einfache Vocal-Simulation mit Formant-Synthese
    
    int sampleRate = 44100;
    float beatDuration = 60.0f / params.bpm;
    
    std::random_device rd;
    std::mt19937 gen(rd());
    
    // Vocal-Formant-Frequenzen (A, E, I, O, U)
    std::vector<std::vector<float>> formants = {
        {730, 1090, 2440},  // A
        {270, 2290, 3010},  // E
        {390, 1990, 2550},  // I
        {570, 840, 2410},   // O
        {440, 1020, 2240}   // U
    };
    
    std::uniform_int_distribution<size_t> formantDist(0, formants.size() - 1);
    
    // Generiere Vocal-Phrasen an wichtigen Stellen
    for (size_t measure = 0; measure < static_cast<size_t>(params.duration / (beatDuration * 4)); ++measure) {
        size_t position = static_cast<size_t>(measure * beatDuration * 4 * sampleRate);
        
        if (position >= samples.size()) break;
        
        // Nur alle 2 Takte
        if (measure % 2 != 0) continue;
        
        const auto& formant = formants[formantDist(gen)];
        
        // Synthese mit mehreren Formanten
        std::vector<float> vocal(static_cast<size_t>(beatDuration * 2 * sampleRate), 0.0f);
        
        for (float freq : formant) {
            std::vector<float> formantTone;
            synthesizeTone(freq, beatDuration * 2, sampleRate, formantTone);
            
            for (size_t i = 0; i < vocal.size() && i < formantTone.size(); ++i) {
                vocal[i] += formantTone[i] * 0.2f;
            }
        }
        
        // Mix Vocals
        for (size_t i = 0; i < vocal.size() && (position + i) < samples.size(); ++i) {
            samples[position + i] += vocal[i] * 0.15f;
        }
    }
    
    return true;
}

bool SongGenerator::mixAndMaster(std::vector<float>& samples) {
    // Normalisiere Audio
    float maxAmplitude = 0.0f;
    for (float sample : samples) {
        maxAmplitude = std::max(maxAmplitude, std::abs(sample));
    }
    
    if (maxAmplitude > 0) {
        float scale = 0.9f / maxAmplitude;  // 90% max um Clipping zu vermeiden
        for (float& sample : samples) {
            sample *= scale;
        }
    }
    
    return true;
}

bool SongGenerator::synthesizeTone(float frequency, float duration, int sampleRate, std::vector<float>& output) {
    size_t numSamples = static_cast<size_t>(duration * sampleRate);
    output.resize(numSamples);
    
    const float pi = 3.14159265358979323846f;
    
    for (size_t i = 0; i < numSamples; ++i) {
        float t = static_cast<float>(i) / sampleRate;
        output[i] = std::sin(2 * pi * frequency * t);
    }
    
    // Envelope (ADSR) - längeres Attack für weicheren Start
    applyEnvelope(output, 0.02f, 0.1f, 0.7f, 0.25f);
    
    return true;
}

bool SongGenerator::applyEnvelope(std::vector<float>& samples, float attack, float decay, float sustain, float release) {
    size_t attackSamples = static_cast<size_t>(attack * samples.size());
    size_t decaySamples = static_cast<size_t>(decay * samples.size());
    size_t releaseSamples = static_cast<size_t>(release * samples.size());
    
    for (size_t i = 0; i < samples.size(); ++i) {
        float envelope = 1.0f;
        
        if (i < attackSamples) {
            envelope = static_cast<float>(i) / attackSamples;
        } else if (i < attackSamples + decaySamples) {
            float t = static_cast<float>(i - attackSamples) / decaySamples;
            envelope = 1.0f - (1.0f - sustain) * t;
        } else if (i > samples.size() - releaseSamples) {
            float t = static_cast<float>(samples.size() - i) / releaseSamples;
            envelope = sustain * t;
        } else {
            envelope = sustain;
        }
        
        samples[i] *= envelope;
    }
    
    return true;
}

bool SongGenerator::applyFilter(std::vector<float>& samples, const std::string& type, float cutoff) {
    // TODO: Filter-Implementation
    return true;
}

bool SongGenerator::exportWAV(const std::string& path, const std::vector<float>& samples, int sampleRate) {
    // Erstelle Ausgabe-Verzeichnis falls nicht vorhanden
    std::filesystem::path outputPath(path);
    std::filesystem::path dir = outputPath.parent_path();
    
    if (!dir.empty() && !std::filesystem::exists(dir)) {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec) {
            std::cerr << "❌ Konnte Verzeichnis nicht erstellen: " << dir << " - " << ec.message() << "\n";
            return false;
        }
    }
    
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "❌ Konnte WAV-Datei nicht öffnen: " << path << "\n";
        return false;
    }
    
    // WAV-Header schreiben
    int numChannels = 1;  // Mono
    int bitsPerSample = 16;
    int byteRate = sampleRate * numChannels * bitsPerSample / 8;
    int blockAlign = numChannels * bitsPerSample / 8;
    int dataSize = samples.size() * bitsPerSample / 8;
    
    // RIFF-Header
    file.write("RIFF", 4);
    int chunkSize = 36 + dataSize;
    file.write(reinterpret_cast<const char*>(&chunkSize), 4);
    file.write("WAVE", 4);
    
    // fmt-Chunk
    file.write("fmt ", 4);
    int fmtSize = 16;
    file.write(reinterpret_cast<const char*>(&fmtSize), 4);
    short audioFormat = 1;  // PCM
    file.write(reinterpret_cast<const char*>(&audioFormat), 2);
    file.write(reinterpret_cast<const char*>(&numChannels), 2);
    file.write(reinterpret_cast<const char*>(&sampleRate), 4);
    file.write(reinterpret_cast<const char*>(&byteRate), 4);
    file.write(reinterpret_cast<const char*>(&blockAlign), 2);
    file.write(reinterpret_cast<const char*>(&bitsPerSample), 2);
    
    // data-Chunk
    file.write("data", 4);
    file.write(reinterpret_cast<const char*>(&dataSize), 4);
    
    // Samples schreiben
    for (float sample : samples) {
        short intSample = static_cast<short>(sample * 32767.0f);
        file.write(reinterpret_cast<const char*>(&intSample), 2);
    }
    
    file.close();
    return true;
}

bool SongGenerator::exportMP3(const std::string& path, const std::vector<float>& samples, int sampleRate, int bitrate) {
    // Erstelle Ausgabe-Verzeichnis falls nicht vorhanden
    std::filesystem::path outputPath(path);
    std::filesystem::path dir = outputPath.parent_path();
    
    if (!dir.empty() && !std::filesystem::exists(dir)) {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec) {
            std::cerr << "❌ Konnte Verzeichnis nicht erstellen: " << dir << " - " << ec.message() << "\n";
            return false;
        }
    }
    
    // Initialisiere LAME für MP3 Encoding
    lame_t lame = lame_init();
    if (!lame) {
        std::cerr << "❌ LAME Initialisierung fehlgeschlagen\n";
        return false;
    }
    
    // Konfiguriere LAME
    lame_set_in_samplerate(lame, sampleRate);
    lame_set_num_channels(lame, 1);  // Mono
    lame_set_brate(lame, bitrate);
    lame_set_quality(lame, 5);  // 0=best, 9=worst (5=good balance)
    
    if (lame_init_params(lame) < 0) {
        std::cerr << "❌ LAME Parameter-Initialisierung fehlgeschlagen\n";
        lame_close(lame);
        return false;
    }
    
    // Öffne MP3 Output-Datei
    FILE* mp3File = fopen(path.c_str(), "wb");
    if (!mp3File) {
        std::cerr << "❌ Konnte MP3-Datei nicht öffnen: " << path << "\n";
        lame_close(lame);
        return false;
    }
    
    // Konvertiere float samples zu short
    std::vector<short> pcmBuffer(samples.size());
    for (size_t i = 0; i < samples.size(); ++i) {
        // Clamp und konvertiere zu 16-bit PCM
        float sample = std::max(-1.0f, std::min(1.0f, samples[i]));
        pcmBuffer[i] = static_cast<short>(sample * 32767.0f);
    }
    
    // Encode zu MP3
    const int MP3_SIZE = samples.size() + 10000;
    unsigned char* mp3Buffer = new unsigned char[MP3_SIZE];
    
    int mp3Bytes = lame_encode_buffer(
        lame,
        pcmBuffer.data(),  // left channel (mono)
        nullptr,           // right channel (mono = nullptr)
        pcmBuffer.size(),
        mp3Buffer,
        MP3_SIZE
    );
    
    if (mp3Bytes < 0) {
        std::cerr << "❌ LAME Encoding fehlgeschlagen: " << mp3Bytes << "\n";
        delete[] mp3Buffer;
        fclose(mp3File);
        lame_close(lame);
        return false;
    }
    
    // Schreibe MP3-Daten
    if (mp3Bytes > 0) {
        fwrite(mp3Buffer, 1, mp3Bytes, mp3File);
    }
    
    // Flush final MP3 frames
    mp3Bytes = lame_encode_flush(lame, mp3Buffer, MP3_SIZE);
    if (mp3Bytes > 0) {
        fwrite(mp3Buffer, 1, mp3Bytes, mp3File);
    }
    
    // Cleanup
    delete[] mp3Buffer;
    fclose(mp3File);
    lame_close(lame);
    
    std::cout << "✅ MP3 generiert: " << path << "\n";
    return true;
}
