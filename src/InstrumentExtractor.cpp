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
    
    int beforeFilter = allSamples.size();
    
    // 🎓 Lerne: Filtere Stille und zu geringe Qualität
    allSamples.erase(
        std::remove_if(allSamples.begin(), allSamples.end(),
            [minQuality](const InstrumentSample& s) { 
                // Filtere nach Qualität
                if (s.clarity < minQuality) return true;
                
                // 🔇 Filtere Stille (wichtig für Learning!)
                if (isSilentSample(s.samples, params_.silenceThreshold)) {
                    return true;
                }
                
                // Filtere zu geringe RMS-Energie
                float rmsEnergy = calculateRMSEnergy(s.samples);
                if (rmsEnergy < params_.minRMSEnergy) {
                    return true;
                }
                
                return false;
            }),
        allSamples.end()
    );
    
    int filtered = beforeFilter - allSamples.size();
    if (filtered > 0) {
        std::cout << "  🔇 " << filtered << " stumme/leere Samples entfernt" << std::endl;
    }
    
    // 🗑️ Automatisches Löschen sehr schlechter Samples (optional)
    int autoDeleted = autoDeleteSilentSamples(allSamples, false);
    
    std::cout << "  ✓ " << kicks.size() << " Kicks, " 
              << snares.size() << " Snares, "
              << hihats.size() << " Hi-Hats, "
              << bassLines.size() << " Bass, "
              << leads.size() << " Leads (≥" << (int)(minQuality*100) << "% Qualität)" << std::endl;
    
    if (autoDeleted > 0) {
        std::cout << "  🗑️ " << autoDeleted << " Samples automatisch aussortiert" << std::endl;
    }
    
    // 🥁 Analysiere Rhythmus-Pattern der extrahierten Samples
    if (allSamples.size() >= 3) {
        auto rhythmIntervals = analyzeRhythmPattern(allSamples);
        if (!rhythmIntervals.empty()) {
            float avgInterval = 0.0f;
            for (float interval : rhythmIntervals) {
                avgInterval += interval;
            }
            avgInterval /= rhythmIntervals.size();
            
            std::cout << "  🥁 Rhythmus erkannt: Ø " << (int)(avgInterval * 1000) 
                     << "ms zwischen Hits (" << rhythmIntervals.size() << " Intervalle)" << std::endl;
        }
    }
    
    return allSamples;
}

std::vector<InstrumentSample> InstrumentExtractor::findKicks(
    const std::vector<float>& samples, int sampleRate, const std::string& sourceFile) {
    
    std::vector<InstrumentSample> kicks;
    
    // 🎓 Nutze gelernte Parameter
    auto filtered = applyBandpassFilter(samples, params_.kickRange.low, params_.kickRange.high, sampleRate);
    
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
    
    // 🎓 Nutze gelernte Parameter
    auto filtered = applyBandpassFilter(samples, params_.snareRange.low, params_.snareRange.high, sampleRate);
    auto onsets = findOnsets(filtered, sampleRate);
    
    for (size_t onset : onsets) {
        size_t duration = sampleRate / 10;  // 100ms
        if (onset + duration > samples.size()) continue;
        
        std::vector<float> snareSample(samples.begin() + onset,
                                        samples.begin() + onset + duration);
        
        float clarity = calculateClarity(snareSample);
        if (clarity < params_.minClaritySnare) continue;
        
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
    
    // 🎓 Nutze gelernte Parameter
    auto filtered = applyBandpassFilter(samples, params_.hihatRange.low, params_.hihatRange.high, sampleRate);
    auto onsets = findOnsets(filtered, sampleRate);
    
    for (size_t onset : onsets) {
        size_t duration = sampleRate / 20;  // 50ms
        if (onset + duration > samples.size()) continue;
        
        std::vector<float> hihatSample(samples.begin() + onset,
                                        samples.begin() + onset + duration);
        
        float clarity = calculateClarity(hihatSample);
        if (clarity < params_.minClarityHihat) continue;
        
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

float InstrumentExtractor::calculateRMSEnergy(const std::vector<float>& samples) {
    if (samples.empty()) return 0.0f;
    
    // Root Mean Square - durchschnittliche Energie
    float sumSquares = 0.0f;
    for (float s : samples) {
        sumSquares += s * s;
    }
    
    return std::sqrt(sumSquares / samples.size());
}

bool InstrumentExtractor::isSilentSample(const std::vector<float>& samples, float threshold) {
    if (samples.empty()) return true;
    
    // Prüfe ob Sample praktisch stumm ist
    float maxAmplitude = 0.0f;
    float rms = calculateRMSEnergy(samples);
    
    for (float s : samples) {
        maxAmplitude = std::max(maxAmplitude, std::abs(s));
    }
    
    // Sample ist stumm wenn sowohl Peak als auch RMS unter Schwellwert
    return (maxAmplitude < threshold && rms < threshold * 0.5f);
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

// 🎓 Static member initialization
InstrumentExtractor::ExtractionParameters InstrumentExtractor::params_;
std::vector<InstrumentExtractor::TrainingData> InstrumentExtractor::trainingHistory_;
std::map<InstrumentSample::Type, InstrumentExtractor::RhythmPattern> InstrumentExtractor::learnedRhythms_;
int InstrumentExtractor::totalExtractions_ = 0;
int InstrumentExtractor::successfulExtractions_ = 0;

// 🎓 LEARNING: Trainiert Extractor mit manuellem Feedback
bool InstrumentExtractor::trainWithSample(const InstrumentSample& sample, 
                                          InstrumentSample::Type type, 
                                          float quality) {
    std::cout << "\n🎓 Instrumenten-Extractor Learning:" << std::endl;
    std::cout << "   🎵 Type: ";
    switch(type) {
        case InstrumentSample::KICK: std::cout << "KICK"; break;
        case InstrumentSample::SNARE: std::cout << "SNARE"; break;
        case InstrumentSample::HIHAT: std::cout << "HI-HAT"; break;
        case InstrumentSample::BASS: std::cout << "BASS"; break;
        case InstrumentSample::LEAD: std::cout << "LEAD"; break;
        default: std::cout << "UNKNOWN"; break;
    }
    std::cout << std::endl;
    std::cout << "   ⭐ Qualität: " << (int)(quality * 100) << "%" << std::endl;
    std::cout << "   📊 Dominant Freq: " << sample.dominantFreq << " Hz" << std::endl;
    std::cout << "   ✨ Clarity: " << sample.clarity << std::endl;
    
    // Speichere in Training-Historie
    TrainingData data;
    data.sample = sample;
    data.correctType = type;
    data.userQuality = quality;
    data.timestamp = std::chrono::system_clock::now();
    
    trainingHistory_.push_back(data);
    totalExtractions_++;
    
    if (quality >= 0.7f) {
        successfulExtractions_++;
    }
    
    // Sofortige Parameter-Anpassung bei gutem Feedback
    if (quality >= 0.8f) {
        updateParameterFromFeedback(data);
        std::cout << "   ✅ Parameter aktualisiert" << std::endl;
    } else if (quality < 0.5f) {
        std::cout << "   ⚠️  Schlechte Qualität - analysiere Abweichung..." << std::endl;
        float deviation = calculateParameterDeviation(sample, type);
        std::cout << "   📉 Abweichung: " << (int)(deviation * 100) << "%" << std::endl;
    }
    
    // Auto-Optimierung alle 10 Trainings
    if (trainingHistory_.size() % 10 == 0) {
        std::cout << "\n   🔄 Auto-Optimierung wird gestartet..." << std::endl;
        optimizeExtractionParameters();
    }
    
    return true;
}

// 🧠 Verbessert Extraktion basierend auf Lern-Daten
void InstrumentExtractor::optimizeExtractionParameters() {
    std::cout << "\n🧠 Optimiere Extraktions-Parameter..." << std::endl;
    
    if (trainingHistory_.empty()) {
        std::cout << "   ⚠️  Keine Trainings-Daten vorhanden" << std::endl;
        return;
    }
    
    // Gruppiere nach Instrument-Type
    std::map<InstrumentSample::Type, std::vector<TrainingData>> byType;
    for (const auto& data : trainingHistory_) {
        if (data.userQuality >= 0.7f) {  // Nur gute Samples
            byType[data.correctType].push_back(data);
        }
    }
    
    // Optimiere Frequency Ranges
    for (const auto& [type, samples] : byType) {
        if (samples.empty()) continue;
        
        // Berechne optimale Frequency Range
        float minFreq = std::numeric_limits<float>::max();
        float maxFreq = 0.0f;
        float avgClarity = 0.0f;
        
        for (const auto& data : samples) {
            float freq = data.sample.dominantFreq;
            minFreq = std::min(minFreq, freq * 0.8f);  // 20% Toleranz
            maxFreq = std::max(maxFreq, freq * 1.2f);
            avgClarity += data.sample.clarity;
        }
        avgClarity /= samples.size();
        
        // Update Parameter basierend auf Type
        switch(type) {
            case InstrumentSample::KICK:
                params_.kickRange = {minFreq, maxFreq, avgClarity};
                params_.minClarityKick = avgClarity * 0.8f;
                std::cout << "   🥁 KICK: " << minFreq << "-" << maxFreq 
                          << " Hz (Clarity: " << avgClarity << ")" << std::endl;
                break;
                
            case InstrumentSample::SNARE:
                params_.snareRange = {minFreq, maxFreq, avgClarity};
                params_.minClaritySnare = avgClarity * 0.8f;
                std::cout << "   🥁 SNARE: " << minFreq << "-" << maxFreq 
                          << " Hz (Clarity: " << avgClarity << ")" << std::endl;
                break;
                
            case InstrumentSample::HIHAT:
                params_.hihatRange = {minFreq, maxFreq, avgClarity};
                params_.minClarityHihat = avgClarity * 0.8f;
                std::cout << "   🎵 HI-HAT: " << minFreq << "-" << maxFreq 
                          << " Hz (Clarity: " << avgClarity << ")" << std::endl;
                break;
                
            case InstrumentSample::BASS:
                params_.bassRange = {minFreq, maxFreq, avgClarity};
                std::cout << "   🎸 BASS: " << minFreq << "-" << maxFreq 
                          << " Hz (Clarity: " << avgClarity << ")" << std::endl;
                break;
                
            case InstrumentSample::LEAD:
                params_.leadRange = {minFreq, maxFreq, avgClarity};
                std::cout << "   🎹 LEAD: " << minFreq << "-" << maxFreq 
                          << " Hz (Clarity: " << avgClarity << ")" << std::endl;
                break;
                
            default:
                break;
        }
    }
    
    // Optimiere Onset Detection Threshold
    float avgOnsetQuality = 0.0f;
    int onsetCount = 0;
    for (const auto& data : trainingHistory_) {
        if (data.userQuality >= 0.7f && data.sample.startTime > 0) {
            avgOnsetQuality += data.userQuality;
            onsetCount++;
        }
    }
    
    if (onsetCount > 0) {
        avgOnsetQuality /= onsetCount;
        params_.onsetThreshold = 0.3f * avgOnsetQuality;  // Adaptiv
        std::cout << "   🎯 Onset Threshold: " << params_.onsetThreshold << std::endl;
    }
    
    // 🔇 Lerne optimale Stille-Schwellwerte aus Training
    float minGoodRMS = std::numeric_limits<float>::max();
    float maxBadRMS = 0.0f;
    int goodSamples = 0;
    int badSamples = 0;
    
    for (const auto& data : trainingHistory_) {
        float rms = calculateRMSEnergy(data.sample.samples);
        
        if (data.userQuality >= 0.7f) {
            // Gute Samples - finde minimale Energie
            minGoodRMS = std::min(minGoodRMS, rms);
            goodSamples++;
        } else if (data.userQuality < 0.3f) {
            // Schlechte Samples (oft Stille) - finde maximale Energie
            maxBadRMS = std::max(maxBadRMS, rms);
            badSamples++;
        }
    }
    
    // Setze Schwellwerte zwischen schlechten und guten Samples
    if (goodSamples > 0 && badSamples > 0 && maxBadRMS < minGoodRMS) {
        float oldThreshold = params_.minRMSEnergy;
        params_.minRMSEnergy = (maxBadRMS + minGoodRMS) / 2.0f;
        params_.silenceThreshold = maxBadRMS * 1.2f;  // Etwas über max schlechtes Sample
        
        std::cout << "   🔇 Stille-Filter gelernt:" << std::endl;
        std::cout << "      Min RMS-Energie: " << params_.minRMSEnergy 
                  << " (war: " << oldThreshold << ")" << std::endl;
        std::cout << "      Stille-Schwellwert: " << params_.silenceThreshold << std::endl;
        std::cout << "      Basierend auf " << goodSamples << " guten und " 
                  << badSamples << " schlechten Samples" << std::endl;
    }
    
    // 🥁 Lerne Rhythmus-Patterns aus Training-Daten
    std::cout << "\n   🥁 Lerne Rhythmus-Patterns..." << std::endl;
    
    for (const auto& [type, samples] : byType) {
        if (samples.size() < 3) continue;  // Brauchen mindestens 3 Samples für Pattern
        
        // Konvertiere TrainingData zu InstrumentSamples
        std::vector<InstrumentSample> typeSamples;
        for (const auto& data : samples) {
            typeSamples.push_back(data.sample);
        }
        
        // Analysiere Rhythmus
        auto intervals = analyzeRhythmPattern(typeSamples);
        
        if (!intervals.empty()) {
            RhythmPattern pattern;
            pattern.type = type;
            pattern.intervals = intervals;
            pattern.hitCount = typeSamples.size();
            
            // Berechne Durchschnitt und Varianz
            float sum = 0.0f;
            for (float interval : intervals) {
                sum += interval;
            }
            pattern.averageInterval = sum / intervals.size();
            
            // Varianz
            float varianceSum = 0.0f;
            for (float interval : intervals) {
                float diff = interval - pattern.averageInterval;
                varianceSum += diff * diff;
            }
            pattern.variance = varianceSum / intervals.size();
            
            learnedRhythms_[type] = pattern;
            
            std::string typeName;
            switch(type) {
                case InstrumentSample::KICK: typeName = "Kicks"; break;
                case InstrumentSample::SNARE: typeName = "Snares"; break;
                case InstrumentSample::HIHAT: typeName = "Hi-Hats"; break;
                case InstrumentSample::BASS: typeName = "Bass"; break;
                case InstrumentSample::LEAD: typeName = "Leads"; break;
                default: typeName = "Other"; break;
            }
            
            std::cout << "      • " << typeName << ": Ø " 
                     << (int)(pattern.averageInterval * 1000) << "ms zwischen Hits"
                     << " (Varianz: " << (int)(pattern.variance * 1000) << "ms)"
                     << " [" << pattern.hitCount << " Hits]" << std::endl;
        }
    }
    
    std::cout << "   ✅ Parameter-Optimierung abgeschlossen" << std::endl;
}

// 📊 Statistik der Extraktions-Qualität
void InstrumentExtractor::printExtractionStats() {
    std::cout << "\n📊 Instrumenten-Extraktions-Statistik:" << std::endl;
    std::cout << "   📈 Gesamt Extraktionen: " << totalExtractions_ << std::endl;
    std::cout << "   ✅ Erfolgreich: " << successfulExtractions_ << std::endl;
    
    if (totalExtractions_ > 0) {
        float successRate = (float)successfulExtractions_ / totalExtractions_ * 100.0f;
        std::cout << "   🎯 Erfolgsrate: " << (int)successRate << "%" << std::endl;
    }
    
    std::cout << "   📚 Trainings-Daten: " << trainingHistory_.size() << " Samples" << std::endl;
    
    // Statistik nach Type
    std::map<InstrumentSample::Type, int> countByType;
    std::map<InstrumentSample::Type, float> avgQualityByType;
    
    for (const auto& data : trainingHistory_) {
        countByType[data.correctType]++;
        avgQualityByType[data.correctType] += data.userQuality;
    }
    
    std::cout << "\n   📋 Nach Instrument-Type:" << std::endl;
    for (const auto& [type, count] : countByType) {
        float avgQuality = avgQualityByType[type] / count;
        std::string typeName;
        
        switch(type) {
            case InstrumentSample::KICK: typeName = "Kicks"; break;
            case InstrumentSample::SNARE: typeName = "Snares"; break;
            case InstrumentSample::HIHAT: typeName = "Hi-Hats"; break;
            case InstrumentSample::BASS: typeName = "Bass"; break;
            case InstrumentSample::LEAD: typeName = "Leads"; break;
            default: typeName = "Other"; break;
        }
        
        std::cout << "      • " << typeName << ": " << count 
                  << " Samples (Ø " << (int)(avgQuality * 100) << "%)" << std::endl;
    }
    
    // 🥁 Gelernte Rhythmus-Patterns
    if (!learnedRhythms_.empty()) {
        std::cout << "\n   🥁 Gelernte Rhythmus-Patterns:" << std::endl;
        for (const auto& [type, pattern] : learnedRhythms_) {
            std::string typeName;
            switch(type) {
                case InstrumentSample::KICK: typeName = "Kicks"; break;
                case InstrumentSample::SNARE: typeName = "Snares"; break;
                case InstrumentSample::HIHAT: typeName = "Hi-Hats"; break;
                case InstrumentSample::BASS: typeName = "Bass"; break;
                case InstrumentSample::LEAD: typeName = "Leads"; break;
                default: typeName = "Other"; break;
            }
            
            std::cout << "      • " << typeName << ": Ø " 
                     << (int)(pattern.averageInterval * 1000) << "ms"
                     << " (±" << (int)(std::sqrt(pattern.variance) * 1000) << "ms)"
                     << " - " << pattern.hitCount << " Hits, "
                     << pattern.intervals.size() << " Intervalle" << std::endl;
        }
    }
}

// 💾 Speichert gelernte Parameter
bool InstrumentExtractor::saveLearnedParameters(const std::string& path) {
    std::cout << "\n💾 Speichere gelernte Parameter: " << path << std::endl;
    
    std::ofstream file(path);
    if (!file.is_open()) {
        std::cerr << "   ❌ Fehler beim Öffnen der Datei" << std::endl;
        return false;
    }
    
    // Einfaches Text-Format (JSON-ähnlich)
    file << "{\n";
    file << "  \"onsetThreshold\": " << params_.onsetThreshold << ",\n";
    file << "  \"kickRange\": [" << params_.kickRange.low << ", " << params_.kickRange.high << "],\n";
    file << "  \"snareRange\": [" << params_.snareRange.low << ", " << params_.snareRange.high << "],\n";
    file << "  \"hihatRange\": [" << params_.hihatRange.low << ", " << params_.hihatRange.high << "],\n";
    file << "  \"bassRange\": [" << params_.bassRange.low << ", " << params_.bassRange.high << "],\n";
    file << "  \"leadRange\": [" << params_.leadRange.low << ", " << params_.leadRange.high << "],\n";
    file << "  \"minClarityKick\": " << params_.minClarityKick << ",\n";
    file << "  \"minClaritySnare\": " << params_.minClaritySnare << ",\n";
    file << "  \"minClarityHihat\": " << params_.minClarityHihat << ",\n";
    file << "  \"minRMSEnergy\": " << params_.minRMSEnergy << ",\n";
    file << "  \"silenceThreshold\": " << params_.silenceThreshold << ",\n";
    file << "  \"totalExtractions\": " << totalExtractions_ << ",\n";
    file << "  \"successfulExtractions\": " << successfulExtractions_ << "\n";
    file << "}\n";
    
    file.close();
    std::cout << "   ✅ Parameter gespeichert" << std::endl;
    return true;
}

// 💾 Lädt gelernte Parameter
bool InstrumentExtractor::loadLearnedParameters(const std::string& path) {
    std::cout << "\n📂 Lade gelernte Parameter: " << path << std::endl;
    
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cout << "   ⚠️  Keine Parameter-Datei gefunden - nutze Defaults" << std::endl;
        return false;
    }
    
    // Vereinfachtes Parsing (in Produktion: JSON-Library nutzen)
    std::string line;
    while (std::getline(file, line)) {
        if (line.find("onsetThreshold") != std::string::npos) {
            sscanf(line.c_str(), "  \"onsetThreshold\": %f", &params_.onsetThreshold);
        }
        else if (line.find("minClarityKick") != std::string::npos) {
            sscanf(line.c_str(), "  \"minClarityKick\": %f", &params_.minClarityKick);
        }
        else if (line.find("minClaritySnare") != std::string::npos) {
            sscanf(line.c_str(), "  \"minClaritySnare\": %f", &params_.minClaritySnare);
        }
        else if (line.find("minClarityHihat") != std::string::npos) {
            sscanf(line.c_str(), "  \"minClarityHihat\": %f", &params_.minClarityHihat);
        }
        else if (line.find("minRMSEnergy") != std::string::npos) {
            sscanf(line.c_str(), "  \"minRMSEnergy\": %f", &params_.minRMSEnergy);
        }
        else if (line.find("silenceThreshold") != std::string::npos) {
            sscanf(line.c_str(), "  \"silenceThreshold\": %f", &params_.silenceThreshold);
        }
        else if (line.find("totalExtractions") != std::string::npos) {
            sscanf(line.c_str(), "  \"totalExtractions\": %d", &totalExtractions_);
        }
        else if (line.find("successfulExtractions") != std::string::npos) {
            sscanf(line.c_str(), "  \"successfulExtractions\": %d", &successfulExtractions_);
        }
    }
    
    file.close();
    std::cout << "   ✅ Parameter geladen" << std::endl;
    std::cout << "   📊 " << totalExtractions_ << " Extraktionen, " 
              << successfulExtractions_ << " erfolgreich" << std::endl;
    return true;
}

// Helper: Update Parameter basierend auf Feedback
void InstrumentExtractor::updateParameterFromFeedback(const TrainingData& data) {
    // Passt Parameter basierend auf einzelnem gutem Feedback an
    float freq = data.sample.dominantFreq;
    float clarity = data.sample.clarity;
    
    switch(data.correctType) {
        case InstrumentSample::KICK:
            params_.kickRange.low = std::min(params_.kickRange.low, freq * 0.9f);
            params_.kickRange.high = std::max(params_.kickRange.high, freq * 1.1f);
            params_.minClarityKick = (params_.minClarityKick + clarity * 0.8f) / 2.0f;
            break;
            
        case InstrumentSample::SNARE:
            params_.snareRange.low = std::min(params_.snareRange.low, freq * 0.9f);
            params_.snareRange.high = std::max(params_.snareRange.high, freq * 1.1f);
            params_.minClaritySnare = (params_.minClaritySnare + clarity * 0.8f) / 2.0f;
            break;
            
        case InstrumentSample::HIHAT:
            params_.hihatRange.low = std::min(params_.hihatRange.low, freq * 0.9f);
            params_.hihatRange.high = std::max(params_.hihatRange.high, freq * 1.1f);
            params_.minClarityHihat = (params_.minClarityHihat + clarity * 0.8f) / 2.0f;
            break;
            
        default:
            break;
    }
}

// Helper: Berechne Parameter-Abweichung
float InstrumentExtractor::calculateParameterDeviation(const InstrumentSample& sample, 
                                                       InstrumentSample::Type type) {
    float deviation = 0.0f;
    float freq = sample.dominantFreq;
    
    switch(type) {
        case InstrumentSample::KICK:
            if (freq < params_.kickRange.low || freq > params_.kickRange.high) {
                deviation = std::min(
                    std::abs(freq - params_.kickRange.low),
                    std::abs(freq - params_.kickRange.high)
                ) / params_.kickRange.low;
            }
            break;
            
        case InstrumentSample::SNARE:
            if (freq < params_.snareRange.low || freq > params_.snareRange.high) {
                deviation = std::min(
                    std::abs(freq - params_.snareRange.low),
                    std::abs(freq - params_.snareRange.high)
                ) / params_.snareRange.low;
            }
            break;
            
        case InstrumentSample::HIHAT:
            if (freq < params_.hihatRange.low || freq > params_.hihatRange.high) {
                deviation = std::min(
                    std::abs(freq - params_.hihatRange.low),
                    std::abs(freq - params_.hihatRange.high)
                ) / params_.hihatRange.low;
            }
            break;
            
        default:
            break;
    }
    
    return std::min(1.0f, deviation);
}

// 🥁 Rhythmus-Pattern-Erkennung
std::vector<float> InstrumentExtractor::analyzeRhythmPattern(const std::vector<InstrumentSample>& samples) {
    std::vector<float> intervals;
    
    if (samples.size() < 2) {
        return intervals;
    }
    
    // Sortiere Samples nach Start-Zeit
    std::vector<InstrumentSample> sorted = samples;
    std::sort(sorted.begin(), sorted.end(), 
        [](const InstrumentSample& a, const InstrumentSample& b) {
            return a.startTime < b.startTime;
        });
    
    // Berechne Zeitabstände zwischen aufeinanderfolgenden Hits
    for (size_t i = 1; i < sorted.size(); ++i) {
        float interval = sorted[i].startTime - sorted[i-1].startTime;
        intervals.push_back(interval);
    }
    
    return intervals;
}

// 🗑️ Automatisches Löschen von stillen/schlechten Samples
int InstrumentExtractor::autoDeleteSilentSamples(std::vector<InstrumentSample>& samples, 
                                                  bool deleteFromDisk) {
    int deletedCount = 0;
    
    auto it = samples.begin();
    while (it != samples.end()) {
        bool shouldDelete = false;
        
        // Prüfe auf Stille
        if (isSilentSample(it->samples, params_.silenceThreshold)) {
            shouldDelete = true;
            std::cout << "🔇 Lösche stilles Sample: " << it->description << std::endl;
        }
        
        // Prüfe auf zu geringe RMS-Energie
        float rms = calculateRMSEnergy(it->samples);
        if (rms < params_.minRMSEnergy) {
            shouldDelete = true;
            std::cout << "📉 Lösche Sample mit zu geringer Energie (RMS: " 
                     << rms << "): " << it->description << std::endl;
        }
        
        // Prüfe auf zu geringe Clarity
        if (it->clarity < 0.3f) {  // Sehr niedriger Schwellwert
            shouldDelete = true;
            std::cout << "❌ Lösche Sample mit schlechter Qualität (Clarity: " 
                     << it->clarity << "): " << it->description << std::endl;
        }
        
        if (shouldDelete) {
            // Optional: Lösche Datei von Festplatte
            if (deleteFromDisk && !it->sourceFile.empty()) {
                try {
                    if (std::filesystem::exists(it->sourceFile)) {
                        std::filesystem::remove(it->sourceFile);
                        std::cout << "   💾 Datei gelöscht: " << it->sourceFile << std::endl;
                    }
                } catch (const std::exception& e) {
                    std::cerr << "   ⚠️ Fehler beim Löschen: " << e.what() << std::endl;
                }
            }
            
            it = samples.erase(it);
            deletedCount++;
        } else {
            ++it;
        }
    }
    
    if (deletedCount > 0) {
        std::cout << "\n✅ " << deletedCount << " stille/schlechte Samples automatisch gelöscht" << std::endl;
    }
    
    return deletedCount;
}
