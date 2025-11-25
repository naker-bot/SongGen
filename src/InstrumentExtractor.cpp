#include "../include/InstrumentExtractor.h"
#include "../include/AudioAnalyzer.h"
#include <sndfile.h>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <filesystem>

std::vector<InstrumentSample> InstrumentExtractor::extractInstruments(
    const std::string& audioPath, float minQuality) {
    
    std::vector<InstrumentSample> allSamples;
    
    // Lade Audio-Datei
    SF_INFO sfInfo;
    std::memset(&sfInfo, 0, sizeof(sfInfo));
    
    SNDFILE* file = sf_open(audioPath.c_str(), SFM_READ, &sfInfo);
    if (!file) {
        std::cerr << "❌ Konnte Datei nicht öffnen: " << audioPath << std::endl;
        return allSamples;
    }
    
    // Lese Audio-Daten
    std::vector<float> samples(sfInfo.frames * sfInfo.channels);
    sf_readf_float(file, samples.data(), sfInfo.frames);
    sf_close(file);
    
    // Konvertiere zu Mono falls nötig
    if (sfInfo.channels > 1) {
        std::vector<float> mono(sfInfo.frames);
        for (sf_count_t i = 0; i < sfInfo.frames; ++i) {
            float sum = 0.0f;
            for (int ch = 0; ch < sfInfo.channels; ++ch) {
                sum += samples[i * sfInfo.channels + ch];
            }
            mono[i] = sum / sfInfo.channels;
        }
        samples = std::move(mono);
    }
    
    std::cout << "🔍 Extrahiere Instrumente aus: " << std::filesystem::path(audioPath).filename() << std::endl;
    
    // Extrahiere verschiedene Instrument-Typen
    auto kicks = findKicks(samples, sfInfo.samplerate, audioPath);
    auto snares = findSnares(samples, sfInfo.samplerate, audioPath);
    auto hihats = findHiHats(samples, sfInfo.samplerate, audioPath);
    auto bassLines = findBassLines(samples, sfInfo.samplerate, audioPath);
    auto leads = findLeads(samples, sfInfo.samplerate, audioPath);
    
    // Kombiniere und filtere nach Qualität
    allSamples.insert(allSamples.end(), kicks.begin(), kicks.end());
    allSamples.insert(allSamples.end(), snares.begin(), snares.end());
    allSamples.insert(allSamples.end(), hihats.begin(), hihats.end());
    allSamples.insert(allSamples.end(), bassLines.begin(), bassLines.end());
    allSamples.insert(allSamples.end(), leads.begin(), leads.end());
    
    // Filtere nach Qualität
    allSamples.erase(
        std::remove_if(allSamples.begin(), allSamples.end(),
            [minQuality](const InstrumentSample& s) { return s.clarity < minQuality; }),
        allSamples.end()
    );
    
    std::cout << "  ✓ " << kicks.size() << " Kicks, " 
              << snares.size() << " Snares, "
              << hihats.size() << " Hi-Hats, "
              << bassLines.size() << " Bass, "
              << leads.size() << " Leads (≥" << (int)(minQuality*100) << "% Qualität)" << std::endl;
    
    return allSamples;
}

std::vector<InstrumentSample> InstrumentExtractor::findKicks(
    const std::vector<float>& samples, int sampleRate, const std::string& sourceFile) {
    
    std::vector<InstrumentSample> kicks;
    
    // Bandpass-Filter für Kick-Bereich (40-120 Hz)
    auto filtered = applyBandpassFilter(samples, 40.0f, 120.0f, sampleRate);
    
    // Finde Onsets (plötzliche Energie-Anstiege)
    auto onsets = findOnsets(filtered, sampleRate);
    
    for (size_t onset : onsets) {
        // Extrahiere ~200ms nach Onset
        size_t duration = sampleRate / 5;  // 200ms
        if (onset + duration > samples.size()) continue;
        
        std::vector<float> kickSample(samples.begin() + onset, 
                                       samples.begin() + onset + duration);
        
        float clarity = calculateClarity(kickSample);
        if (clarity < 0.6f) continue;  // Zu unsauber
        
        InstrumentSample kick;
        kick.type = InstrumentSample::KICK;
        kick.sourceFile = sourceFile;
        kick.startTime = static_cast<float>(onset) / sampleRate;
        kick.duration = static_cast<float>(duration) / sampleRate;
        kick.samples = kickSample;
        kick.sampleRate = sampleRate;
        kick.dominantFreq = getDominantFrequency(kickSample, sampleRate);
        kick.clarity = clarity;
        kick.description = "Kick @ " + std::to_string(kick.startTime) + "s";
        
        kicks.push_back(kick);
        
        if (kicks.size() >= 10) break;  // Max 10 Kicks pro Track
    }
    
    return kicks;
}

std::vector<InstrumentSample> InstrumentExtractor::findSnares(
    const std::vector<float>& samples, int sampleRate, const std::string& sourceFile) {
    
    std::vector<InstrumentSample> snares;
    
    // Snares haben hohe Frequenzen + Noise (150-400 Hz)
    auto filtered = applyBandpassFilter(samples, 150.0f, 400.0f, sampleRate);
    auto onsets = findOnsets(filtered, sampleRate);
    
    for (size_t onset : onsets) {
        size_t duration = sampleRate / 10;  // 100ms
        if (onset + duration > samples.size()) continue;
        
        std::vector<float> snareSample(samples.begin() + onset,
                                        samples.begin() + onset + duration);
        
        float clarity = calculateClarity(snareSample);
        if (clarity < 0.5f) continue;
        
        InstrumentSample snare;
        snare.type = InstrumentSample::SNARE;
        snare.sourceFile = sourceFile;
        snare.startTime = static_cast<float>(onset) / sampleRate;
        snare.duration = static_cast<float>(duration) / sampleRate;
        snare.samples = snareSample;
        snare.sampleRate = sampleRate;
        snare.dominantFreq = getDominantFrequency(snareSample, sampleRate);
        snare.clarity = clarity;
        snare.description = "Snare @ " + std::to_string(snare.startTime) + "s";
        
        snares.push_back(snare);
        
        if (snares.size() >= 10) break;
    }
    
    return snares;
}

std::vector<InstrumentSample> InstrumentExtractor::findHiHats(
    const std::vector<float>& samples, int sampleRate, const std::string& sourceFile) {
    
    std::vector<InstrumentSample> hihats;
    
    // Hi-Hats sind sehr hochfrequent (8-14 kHz)
    auto filtered = applyBandpassFilter(samples, 8000.0f, 14000.0f, sampleRate);
    auto onsets = findOnsets(filtered, sampleRate);
    
    for (size_t onset : onsets) {
        size_t duration = sampleRate / 20;  // 50ms
        if (onset + duration > samples.size()) continue;
        
        std::vector<float> hihatSample(samples.begin() + onset,
                                        samples.begin() + onset + duration);
        
        float clarity = calculateClarity(hihatSample);
        if (clarity < 0.4f) continue;
        
        InstrumentSample hihat;
        hihat.type = InstrumentSample::HIHAT;
        hihat.sourceFile = sourceFile;
        hihat.startTime = static_cast<float>(onset) / sampleRate;
        hihat.duration = static_cast<float>(duration) / sampleRate;
        hihat.samples = hihatSample;
        hihat.sampleRate = sampleRate;
        hihat.dominantFreq = getDominantFrequency(hihatSample, sampleRate);
        hihat.clarity = clarity;
        hihat.description = "Hi-Hat @ " + std::to_string(hihat.startTime) + "s";
        
        hihats.push_back(hihat);
        
        if (hihats.size() >= 10) break;
    }
    
    return hihats;
}

std::vector<InstrumentSample> InstrumentExtractor::findBassLines(
    const std::vector<float>& samples, int sampleRate, const std::string& sourceFile) {
    
    std::vector<InstrumentSample> bassLines;
    
    // Bass: 60-250 Hz
    auto filtered = applyBandpassFilter(samples, 60.0f, 250.0f, sampleRate);
    
    // Suche nach längeren, kontinuierlichen Bass-Abschnitten (mind. 1 Sekunde)
    size_t minDuration = sampleRate;  // 1 Sekunde
    size_t windowSize = sampleRate / 10;  // 100ms Fenster
    
    for (size_t i = 0; i + minDuration < filtered.size(); i += windowSize) {
        // Berechne Energie in diesem Fenster
        float energy = 0.0f;
        for (size_t j = i; j < i + minDuration && j < filtered.size(); ++j) {
            energy += filtered[j] * filtered[j];
        }
        energy /= minDuration;
        
        if (energy < 0.01f) continue;  // Zu leise
        
        std::vector<float> bassSample(samples.begin() + i,
                                       samples.begin() + std::min(i + minDuration * 2, samples.size()));
        
        float clarity = calculateClarity(bassSample);
        if (clarity < 0.5f) continue;
        
        InstrumentSample bass;
        bass.type = InstrumentSample::BASS;
        bass.sourceFile = sourceFile;
        bass.startTime = static_cast<float>(i) / sampleRate;
        bass.duration = static_cast<float>(bassSample.size()) / sampleRate;
        bass.samples = bassSample;
        bass.sampleRate = sampleRate;
        bass.dominantFreq = getDominantFrequency(bassSample, sampleRate);
        bass.clarity = clarity;
        bass.energy = energy;
        bass.description = "Bass @ " + std::to_string(bass.startTime) + "s";
        
        bassLines.push_back(bass);
        
        if (bassLines.size() >= 5) break;  // Max 5 Bass-Lines pro Track
    }
    
    return bassLines;
}

std::vector<InstrumentSample> InstrumentExtractor::findLeads(
    const std::vector<float>& samples, int sampleRate, const std::string& sourceFile) {
    
    std::vector<InstrumentSample> leads;
    
    // Lead-Melodien: 500-4000 Hz
    auto filtered = applyBandpassFilter(samples, 500.0f, 4000.0f, sampleRate);
    
    size_t minDuration = sampleRate / 2;  // 0.5 Sekunden
    size_t windowSize = sampleRate / 5;   // 200ms
    
    for (size_t i = 0; i + minDuration < filtered.size(); i += windowSize) {
        float energy = 0.0f;
        for (size_t j = i; j < i + minDuration && j < filtered.size(); ++j) {
            energy += filtered[j] * filtered[j];
        }
        energy /= minDuration;
        
        if (energy < 0.02f) continue;
        
        std::vector<float> leadSample(samples.begin() + i,
                                       samples.begin() + std::min(i + minDuration * 4, samples.size()));
        
        float clarity = calculateClarity(leadSample);
        if (clarity < 0.6f) continue;
        
        InstrumentSample lead;
        lead.type = InstrumentSample::LEAD;
        lead.sourceFile = sourceFile;
        lead.startTime = static_cast<float>(i) / sampleRate;
        lead.duration = static_cast<float>(leadSample.size()) / sampleRate;
        lead.samples = leadSample;
        lead.sampleRate = sampleRate;
        lead.dominantFreq = getDominantFrequency(leadSample, sampleRate);
        lead.clarity = clarity;
        lead.energy = energy;
        lead.description = "Lead @ " + std::to_string(lead.startTime) + "s";
        
        leads.push_back(lead);
        
        if (leads.size() >= 5) break;
    }
    
    return leads;
}

float InstrumentExtractor::calculateClarity(const std::vector<float>& samples) {
    if (samples.empty()) return 0.0f;
    
    // Berechne Signal-to-Noise Ratio (vereinfacht)
    float peak = 0.0f;
    float avg = 0.0f;
    
    for (float s : samples) {
        float abs_s = std::abs(s);
        peak = std::max(peak, abs_s);
        avg += abs_s;
    }
    avg /= samples.size();
    
    if (avg == 0.0f) return 0.0f;
    
    float clarity = (peak - avg) / peak;  // Wie stark unterscheidet sich Peak vom Durchschnitt
    return std::clamp(clarity, 0.0f, 1.0f);
}

float InstrumentExtractor::getDominantFrequency(const std::vector<float>& samples, int sampleRate) {
    // Vereinfachte Frequenz-Detektion via Zero-Crossings
    if (samples.size() < 2) return 0.0f;
    
    int zeroCrossings = 0;
    for (size_t i = 1; i < samples.size(); ++i) {
        if ((samples[i-1] >= 0 && samples[i] < 0) || (samples[i-1] < 0 && samples[i] >= 0)) {
            zeroCrossings++;
        }
    }
    
    float freq = (zeroCrossings * sampleRate) / (2.0f * samples.size());
    return freq;
}

std::vector<size_t> InstrumentExtractor::findOnsets(const std::vector<float>& samples, int sampleRate) {
    std::vector<size_t> onsets;
    
    // Berechne Energie-Hüllkurve mit 10ms Fenstern
    size_t windowSize = sampleRate / 100;  // 10ms
    std::vector<float> energy;
    
    for (size_t i = 0; i + windowSize < samples.size(); i += windowSize) {
        float e = 0.0f;
        for (size_t j = i; j < i + windowSize; ++j) {
            e += samples[j] * samples[j];
        }
        energy.push_back(e / windowSize);
    }
    
    // Finde plötzliche Energie-Anstiege
    float threshold = 0.3f;  // 30% Anstieg
    for (size_t i = 1; i < energy.size(); ++i) {
        if (energy[i-1] > 0 && (energy[i] / energy[i-1]) > (1.0f + threshold)) {
            onsets.push_back(i * windowSize);
        }
    }
    
    return onsets;
}

std::vector<float> InstrumentExtractor::applyBandpassFilter(
    const std::vector<float>& samples, float lowFreq, float highFreq, int sampleRate) {
    
    // Vereinfachter Bandpass-Filter (für echte Implementierung FFTW oder ähnlich nutzen)
    std::vector<float> filtered = samples;
    
    // Einfache Moving-Average Tiefpass für Demo
    // TODO: Echten Bandpass mit FFTW implementieren
    
    return filtered;
}

bool InstrumentExtractor::saveSample(const InstrumentSample& sample, const std::string& outputPath) {
    SF_INFO sfInfo;
    std::memset(&sfInfo, 0, sizeof(sfInfo));
    
    sfInfo.samplerate = sample.sampleRate;
    sfInfo.channels = 1;
    sfInfo.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
    
    SNDFILE* outFile = sf_open(outputPath.c_str(), SFM_WRITE, &sfInfo);
    if (!outFile) {
        std::cerr << "❌ Konnte Sample nicht speichern: " << outputPath << std::endl;
        return false;
    }
    
    sf_writef_float(outFile, sample.samples.data(), sample.samples.size());
    sf_close(outFile);
    
    return true;
}

std::map<InstrumentSample::Type, std::vector<InstrumentSample>> 
InstrumentExtractor::loadInstrumentLibrary(const std::string& instrumentDir) {
    
    std::map<InstrumentSample::Type, std::vector<InstrumentSample>> library;
    
    if (!std::filesystem::exists(instrumentDir)) {
        std::filesystem::create_directories(instrumentDir);
        return library;
    }
    
    // Durchsuche Unterverzeichnisse nach Instrument-Typ
    for (const auto& typeDir : std::filesystem::directory_iterator(instrumentDir)) {
        if (!typeDir.is_directory()) continue;
        
        std::string dirname = typeDir.path().filename().string();
        InstrumentSample::Type type = InstrumentSample::UNKNOWN;
        
        if (dirname == "kicks") type = InstrumentSample::KICK;
        else if (dirname == "snares") type = InstrumentSample::SNARE;
        else if (dirname == "hihats") type = InstrumentSample::HIHAT;
        else if (dirname == "bass") type = InstrumentSample::BASS;
        else if (dirname == "leads") type = InstrumentSample::LEAD;
        
        if (type == InstrumentSample::UNKNOWN) continue;
        
        // Lade alle WAV-Dateien aus diesem Verzeichnis
        for (const auto& file : std::filesystem::directory_iterator(typeDir.path())) {
            if (file.path().extension() != ".wav") continue;
            
            SF_INFO sfInfo;
            std::memset(&sfInfo, 0, sizeof(sfInfo));
            
            SNDFILE* sf = sf_open(file.path().c_str(), SFM_READ, &sfInfo);
            if (!sf) continue;
            
            std::vector<float> samples(sfInfo.frames);
            sf_readf_float(sf, samples.data(), sfInfo.frames);
            sf_close(sf);
            
            InstrumentSample sample;
            sample.type = type;
            sample.sourceFile = file.path().string();
            sample.samples = samples;
            sample.sampleRate = sfInfo.samplerate;
            sample.dominantFreq = getDominantFrequency(samples, sfInfo.samplerate);
            sample.clarity = calculateClarity(samples);
            
            library[type].push_back(sample);
        }
    }
    
    return library;
}
