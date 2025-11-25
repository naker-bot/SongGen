#include "AudioSegmenter.h"
#include <fstream>
#include <cmath>
#include <algorithm>
#include <iostream>

std::vector<SongSegment> AudioSegmenter::analyzeStructure(const std::string& wavPath) {
    std::vector<SongSegment> segments;
    
    // 1. Lade WAV
    std::ifstream file(wavPath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "❌ Cannot open: " << wavPath << std::endl;
        return segments;
    }
    
    // WAV-Header lesen
    char header[44];
    file.read(header, 44);
    
    int sampleRate = *reinterpret_cast<int*>(&header[24]);
    short numChannels = *reinterpret_cast<short*>(&header[22]);
    short bitsPerSample = *reinterpret_cast<short*>(&header[34]);
    
    if (bitsPerSample != 16) {
        std::cerr << "⚠️ Only 16-bit WAV supported for segmentation\n";
        return segments;
    }
    
    // Audio-Daten lesen
    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(44);
    
    size_t dataSize = fileSize - 44;
    size_t numSamples = dataSize / 2; // 16-bit = 2 bytes
    
    std::vector<short> rawSamples(numSamples);
    file.read(reinterpret_cast<char*>(rawSamples.data()), dataSize);
    file.close();
    
    // Konvertiere zu float und Mono
    std::vector<float> samples;
    if (numChannels == 1) {
        samples.resize(numSamples);
        for (size_t i = 0; i < numSamples; ++i) {
            samples[i] = rawSamples[i] / 32768.0f;
        }
    } else {
        // Stereo -> Mono
        size_t monoSamples = numSamples / 2;
        samples.resize(monoSamples);
        for (size_t i = 0; i < monoSamples; ++i) {
            samples[i] = (rawSamples[i*2] + rawSamples[i*2+1]) / 65536.0f;
        }
    }
    
    // 2. Energie-Analyse (EINFACHE VERSION - für Proof-of-Concept)
    std::vector<float> energy = calculateEnergyEnvelope(samples, sampleRate);
    
    float songDuration = samples.size() / static_cast<float>(sampleRate);
    
    // 3. Heuristik-basierte Segmentierung
    // INTRO: Erste 10-20% mit niedrigerer Energie
    float introThreshold = songDuration * 0.15f;
    if (songDuration > 10.0f) {
        float introEnergy = 0.0f;
        int introSamples = std::min(static_cast<int>(sampleRate * 10), static_cast<int>(samples.size()));
        for (int i = 0; i < introSamples; ++i) {
            introEnergy += std::abs(samples[i]);
        }
        introEnergy /= introSamples;
        
        segments.push_back({
            SongSegment::INTRO,
            0.0f,
            std::min(10.0f, songDuration * 0.15f),
            introEnergy,
            0.3f,
            "Intro section"
        });
    }
    
    // MAIN BODY: 20-80% - klassifiziere als Verse/Chorus basierend auf Energie
    float bodyStart = segments.empty() ? 0.0f : segments.back().startTime + segments.back().duration;
    float bodyEnd = songDuration * 0.85f;
    
    // Finde hohe Energie-Peaks für Chorus
    float avgEnergy = 0.0f;
    for (size_t i = 0; i < energy.size(); ++i) {
        avgEnergy += energy[i];
    }
    avgEnergy /= energy.size();
    
    // Vereinfacht: alternierende Verse/Chorus-Struktur
    float sectionDuration = 15.0f; // 15 Sekunden pro Abschnitt
    bool isVerse = true;
    
    for (float t = bodyStart; t < bodyEnd; t += sectionDuration) {
        float dur = std::min(sectionDuration, bodyEnd - t);
        
        // Berechne Energie für diesen Abschnitt
        int startSample = static_cast<int>(t * sampleRate);
        int endSample = std::min(startSample + static_cast<int>(dur * sampleRate), static_cast<int>(samples.size()));
        
        float sectionEnergy = 0.0f;
        for (int i = startSample; i < endSample; ++i) {
            sectionEnergy += std::abs(samples[i]);
        }
        sectionEnergy /= (endSample - startSample);
        
        segments.push_back({
            isVerse ? SongSegment::VERSE : SongSegment::CHORUS,
            t,
            dur,
            sectionEnergy,
            isVerse ? 0.5f : 0.8f,
            isVerse ? "Verse section" : "Chorus section"
        });
        
        isVerse = !isVerse;
    }
    
    // OUTRO: Letzte 10-15%
    if (songDuration > 10.0f) {
        float outroStart = songDuration * 0.85f;
        segments.push_back({
            SongSegment::OUTRO,
            outroStart,
            songDuration - outroStart,
            0.4f,
            0.3f,
            "Outro section"
        });
    }
    
    return segments;
}

std::vector<float> AudioSegmenter::calculateEnergyEnvelope(const std::vector<float>& samples, int sampleRate) {
    int windowSize = sampleRate / 10; // 100ms Fenster
    int hopSize = windowSize / 2;
    
    std::vector<float> energy;
    
    for (size_t i = 0; i < samples.size(); i += hopSize) {
        float e = 0.0f;
        int count = 0;
        
        for (int j = 0; j < windowSize && (i + j) < samples.size(); ++j) {
            e += samples[i + j] * samples[i + j];
            count++;
        }
        
        energy.push_back(std::sqrt(e / count));
    }
    
    return energy;
}

std::vector<float> AudioSegmenter::detectOnsets(const std::vector<float>& samples, int sampleRate) {
    // TODO: Onset-Detection für präzisere Segmentierung
    return {};
}

std::vector<SongSegment> AudioSegmenter::findRepetitions(const std::vector<float>& energy) {
    // TODO: Finde wiederholende Muster für Verse/Chorus-Erkennung
    return {};
}

SongSegment::Type AudioSegmenter::classifySegment(float energy, float complexity, float position) {
    // Einfache Heuristik
    if (position < 0.15f) return SongSegment::INTRO;
    if (position > 0.85f) return SongSegment::OUTRO;
    if (energy > 0.7f && complexity > 0.6f) return SongSegment::CHORUS;
    if (complexity > 0.8f) return SongSegment::SOLO;
    if (energy < 0.5f) return SongSegment::VERSE;
    
    return SongSegment::UNKNOWN;
}
