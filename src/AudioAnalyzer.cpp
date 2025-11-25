#include "AudioAnalyzer.h"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <thread>
#include <map>
#include <cstring>

#ifdef WITH_FFTW3
#include <fftw3.h>
#endif

#ifdef WITH_SNDFILE
#include <sndfile.h>
#endif

#ifdef WITH_OPENVINO
#include <openvino/openvino.hpp>
using namespace ov;
#endif

AudioAnalyzer::AudioAnalyzer() {
    initializeNPU();
}

AudioAnalyzer::~AudioAnalyzer() {
}

void AudioAnalyzer::initializeNPU() {
#ifdef WITH_OPENVINO
    try {
        // OpenVINO Core initialisieren
        Core core;
        auto devices = core.get_available_devices();
        
        // Prüfe auf NPU-Device
        for (const auto& device : devices) {
            if (device.find("NPU") != std::string::npos) {
                std::cout << "⚡ Intel NPU via OpenVINO erkannt: " << device << "\n";
                useNPU_ = true;
                return;
            }
        }
        
        // Prüfe auf GPU als Fallback
        static bool gpuMessageShown = false;
        for (const auto& device : devices) {
            if (device.find("GPU") != std::string::npos) {
                if (!gpuMessageShown) {
                    std::cout << "🎮 Intel GPU via OpenVINO erkannt: " << device << "\n";
                    gpuMessageShown = true;
                }
                useNPU_ = true;  // GPU auch für Beschleunigung nutzen
                return;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "⚠️ OpenVINO Fehler: " << e.what() << "\n";
    }
#endif
    
    // Fallback: Hardware-Detection ohne OpenVINO
    if (std::system("lspci | grep -i 'neural' >/dev/null 2>&1") == 0) {
        std::cout << "⚡ Intel NPU erkannt (OpenVINO nicht installiert)\n";
        useNPU_ = true;
        return;
    }
    
    std::cout << "💻 Keine NPU gefunden, nutze CPU für Inferenz\n";
    useNPU_ = false;
}

bool AudioAnalyzer::analyze(const std::string& filepath, MediaMetadata& meta) {
    std::vector<float> samples;
    int sampleRate = 44100;
    
    // Lade Audio-Datei
    if (!loadAudioFile(filepath, samples, sampleRate)) {
        std::cerr << "Failed to load audio: " << filepath << std::endl;
        return false;
    }
    
    meta.filepath = filepath;
    meta.duration = static_cast<float>(samples.size()) / sampleRate;
    
    // BPM-Erkennung
    meta.bpm = detectBPM(samples, sampleRate);
    
    // Spektrale Features
    auto spectrum = performFFT(samples);
    meta.spectralCentroid = calculateSpectralCentroid(spectrum);
    meta.spectralRolloff = calculateSpectralRolloff(spectrum);
    meta.zeroCrossingRate = calculateZeroCrossingRate(samples);
    
    // MFCC-Hash
    meta.mfccHash = calculateMFCCHash(samples, sampleRate);
    
    // NPU-beschleunigte Feature-Extraktion (falls verfügbar)
    if (useNPU_) {
        std::vector<float> features = {
            meta.bpm / 200.0f,  // Normalisiert
            meta.spectralCentroid / 5000.0f,
            meta.spectralRolloff / 5000.0f,
            meta.zeroCrossingRate,
            meta.mfccHash
        };
        auto npuFeatures = runNPUInference(features);
        // NPU-Features können für erweiterte Genre-Klassifikation genutzt werden
    }
    
    // Instrument-Erkennung
    auto instruments = detectInstruments(samples, sampleRate);
    meta.instruments = "";
    for (size_t i = 0; i < instruments.size(); ++i) {
        meta.instruments += instruments[i];
        if (i < instruments.size() - 1) meta.instruments += ",";
    }
    
    // Intensität und Bass-Level
    meta.intensity = detectIntensity(samples);
    meta.bassLevel = detectBassLevel(samples, sampleRate);
    
    // Genre-Klassifikation (basierend auf Features)
    meta.genre = classifyGenre(meta);
    
    // Erweiterte Stil-Analyse
    std::string rhythmPattern = analyzeRhythmPattern(samples, sampleRate, meta.bpm);
    std::string musicalStyle = detectMusicalStyle(meta);
    auto styleTags = extractStyleTags(meta);
    
    // Speichere erweiterte Info im mood-Feld
    meta.mood = rhythmPattern + " | " + musicalStyle;
    for (const auto& tag : styleTags) {
        meta.mood += " #" + tag;
    }
    
    meta.analyzed = true;
    meta.addedTimestamp = std::time(nullptr);
    
    return true;
}

bool AudioAnalyzer::detectSilenceAndTrimWav(
    const std::string& inPath,
    const std::string& outPath,
    float silenceThreshold,
    float minSoundSeconds,
    float tailSilenceSeconds) {

    std::vector<float> samples;
    int sampleRate = 44100;

    if (!loadWAV(inPath, samples, sampleRate) || samples.empty()) {
        std::cerr << "[Analyzer] ❌ Kann WAV nicht laden: " << inPath << "\n";
        return false;
    }

    const size_t totalSamples = samples.size();
    const int channels = 1; // loadWAV normalisiert bereits auf Mono
    float durationSec = static_cast<float>(totalSamples) / sampleRate;
    
    std::cerr << "[Analyzer] Track-Länge: " << durationSec << "s, " << totalSamples << " samples\n";

    const size_t frameSize = static_cast<size_t>(0.02f * sampleRate); // 20 ms Frames
    if (frameSize == 0 || totalSamples < frameSize * 2) {
        std::cerr << "[Analyzer] ❌ Track zu kurz\n";
        return false;
    }

    const float minSoundSamples = minSoundSeconds * sampleRate;
    const float tailSilenceSamples = tailSilenceSeconds * sampleRate;

    size_t loudFrames = 0;           // Zähler für laute Frames
    float maxLevel = 0.0f;           // Maximaler Pegel gefunden
    float totalEnergy = 0.0f;        // Gesamt-Energie

    // Analysiere GESAMTEN Track (kein Trimming!)
    for (size_t i = 0; i + frameSize <= totalSamples; i += frameSize) {
        float sumAbs = 0.0f;
        for (size_t j = 0; j < frameSize; ++j) {
            sumAbs += std::fabs(samples[i + j]);
        }
        float level = sumAbs / frameSize;
        
        if (level > maxLevel) maxLevel = level;
        totalEnergy += level;
        
        if (level > silenceThreshold) {
            loudFrames++;
        }
    }
    
    float avgEnergy = totalEnergy / (totalSamples / frameSize);

    std::cerr << "[Analyzer] Max-Pegel: " << maxLevel << ", Avg-Energie: " << avgEnergy << "\n";
    std::cerr << "[Analyzer] Laute Frames: " << loudFrames << " von " << (totalSamples / frameSize) << "\n";
    std::cerr << "[Analyzer] Schwellwert: " << silenceThreshold << "\n";

    // Validierung: Track muss genug hörbaren Content haben
    float audibleRatio = static_cast<float>(loudFrames) / (totalSamples / frameSize);
    std::cerr << "[Analyzer] Hörbarer Anteil: " << (audibleRatio * 100.0f) << "%\n";
    
    if (maxLevel < silenceThreshold) {
        // Komplett stumm
        std::cerr << "[Analyzer] ❌ Komplett stumm (max level: " << maxLevel << ")\n";
        return false;
    }
    
    if (audibleRatio < 0.05f) {
        // Weniger als 5% des Tracks ist hörbar
        std::cerr << "[Analyzer] ❌ Zu wenig Audio (nur " << (audibleRatio * 100.0f) << "%)\n";
        return false;
    }

    std::cerr << "[Analyzer] ✅ Track ist gültig, speichere OHNE Trimming\n";

    // KEIN TRIMMING! Speichere kompletten Track
    std::vector<float> trimmed = samples;

    // Schreibe WAV (nutze bestehende load/save-Logik über analyze nicht, um DB nicht zu berühren)
    // Wir bauen hier einen einfachen 16-Bit PCM Writer.

    std::ofstream out(outPath, std::ios::binary);
    if (!out.is_open()) {
        return false;
    }

    const int bitsPerSample = 16;
    const int byteRate = sampleRate * channels * (bitsPerSample / 8);
    const int blockAlign = channels * (bitsPerSample / 8);
    const int dataSize = static_cast<int>(trimmed.size() * sizeof(int16_t));

    // RIFF Header
    out.write("RIFF", 4);
    int32_t chunkSize = 36 + dataSize;
    out.write(reinterpret_cast<const char*>(&chunkSize), 4);
    out.write("WAVE", 4);

    // fmt Subchunk
    out.write("fmt ", 4);
    int32_t subchunk1Size = 16;
    out.write(reinterpret_cast<const char*>(&subchunk1Size), 4);
    int16_t audioFormat = 1; // PCM
    out.write(reinterpret_cast<const char*>(&audioFormat), 2);
    int16_t numChannels = static_cast<int16_t>(channels);
    out.write(reinterpret_cast<const char*>(&numChannels), 2);
    int32_t sampleRate32 = sampleRate;
    out.write(reinterpret_cast<const char*>(&sampleRate32), 4);
    int32_t byteRate32 = byteRate;
    out.write(reinterpret_cast<const char*>(&byteRate32), 4);
    int16_t blockAlign16 = static_cast<int16_t>(blockAlign);
    out.write(reinterpret_cast<const char*>(&blockAlign16), 2);
    int16_t bitsPerSample16 = bitsPerSample;
    out.write(reinterpret_cast<const char*>(&bitsPerSample16), 2);

    // data Subchunk
    out.write("data", 4);
    int32_t dataSize32 = dataSize;
    out.write(reinterpret_cast<const char*>(&dataSize32), 4);

    // PCM-Daten schreiben
    for (float s : trimmed) {
        float clamped = std::max(-1.0f, std::min(1.0f, s));
        int16_t sample = static_cast<int16_t>(clamped * 32767.0f);
        out.write(reinterpret_cast<const char*>(&sample), sizeof(int16_t));
    }

    out.close();
    return true;
}

std::vector<MediaMetadata> AudioAnalyzer::analyzeBatch(
    const std::vector<std::string>& filepaths,
    const std::function<void(size_t, size_t)>& progressCallback) {
    
    std::vector<MediaMetadata> results;
    size_t completed = 0;
    
    // TODO: Multi-Threading für Batch-Processing
    for (const auto& filepath : filepaths) {
        MediaMetadata meta;
        if (analyze(filepath, meta)) {
            results.push_back(meta);
        }
        
        completed++;
        if (progressCallback) {
            progressCallback(completed, filepaths.size());
        }
    }
    
    return results;
}

float AudioAnalyzer::detectBPM(const std::vector<float>& samples, int sampleRate) {
    if (samples.empty()) return 120.0f;
    
    // Energy-basierte Beat-Detection mit Autocorrelation
    size_t hopSize = 512;
    size_t windowSize = 2048;
    std::vector<float> energy;
    
    // Berechne Energy pro Frame
    for (size_t i = 0; i + windowSize < samples.size(); i += hopSize) {
        float frameEnergy = 0.0f;
        for (size_t j = 0; j < windowSize; ++j) {
            frameEnergy += samples[i + j] * samples[i + j];
        }
        energy.push_back(frameEnergy / windowSize);
    }
    
    if (energy.size() < 100) return 120.0f;
    
    // Onset-Detection (Energieänderung)
    std::vector<float> onsets;
    for (size_t i = 1; i < energy.size(); ++i) {
        float diff = energy[i] - energy[i-1];
        onsets.push_back(diff > 0 ? diff : 0);
    }
    
    // Autocorrelation für periodische Peaks
    std::vector<float> autocorr(300, 0.0f);  // ~2 Sekunden bei 150 BPM
    for (size_t lag = 20; lag < autocorr.size() && lag < onsets.size(); ++lag) {
        for (size_t i = 0; i + lag < onsets.size(); ++i) {
            autocorr[lag] += onsets[i] * onsets[i + lag];
        }
    }
    
    // Finde Peak in Autocorrelation
    float maxCorr = 0.0f;
    size_t peakLag = 60;
    for (size_t i = 20; i < autocorr.size(); ++i) {
        if (autocorr[i] > maxCorr) {
            maxCorr = autocorr[i];
            peakLag = i;
        }
    }
    
    // Konvertiere Lag zu BPM
    if (peakLag == 0) return 120.0f;  // Fallback bei ungültigem Peak
    
    float framesPerSecond = static_cast<float>(sampleRate) / hopSize;
    float beatsPerSecond = framesPerSecond / peakLag;
    float bpm = beatsPerSecond * 60.0f;
    
    // Validiere BPM-Range
    if (bpm < 60.0f || bpm > 200.0f) {
        return 120.0f;  // Fallback
    }
    
    return bpm;
}

std::vector<std::string> AudioAnalyzer::detectInstruments(const std::vector<float>& samples, int sampleRate) {
    // TODO: ML-basierte Instrument-Erkennung
    // Für jetzt: Dummy-Implementation
    std::vector<std::string> instruments;
    
    // Basierend auf spektralen Features einfache Heuristik
    auto spectrum = performFFT(samples);
    float lowEnergy = 0.0f, midEnergy = 0.0f, highEnergy = 0.0f;
    
    size_t lowBound = spectrum.size() / 10;
    size_t midBound = spectrum.size() / 3;
    
    for (size_t i = 0; i < spectrum.size(); ++i) {
        float mag = std::abs(spectrum[i]);
        if (i < lowBound) lowEnergy += mag;
        else if (i < midBound) midEnergy += mag;
        else highEnergy += mag;
    }
    
    if (lowEnergy > midEnergy * 1.5f) instruments.push_back("bass");
    if (midEnergy > lowEnergy && midEnergy > highEnergy) instruments.push_back("guitar");
    if (highEnergy > midEnergy * 1.2f) instruments.push_back("synth");
    
    if (instruments.empty()) instruments.push_back("unknown");
    
    return instruments;
}

std::string AudioAnalyzer::classifyGenre(const MediaMetadata& meta) {
    // Multi-Feature Genre-Klassifikation
    
    float bpm = meta.bpm;
    float spectralCentroid = meta.spectralCentroid;
    float zeroCrossing = meta.zeroCrossingRate;
    std::string intensity = meta.intensity;
    std::string bassLevel = meta.bassLevel;
    
    // Trap: 130-150 BPM, basslastig, mittlere Spektral-Features
    if (bpm >= 130 && bpm <= 150 && bassLevel == "basslastig") {
        return "Trap";
    }
    
    // Techno: 120-135 BPM, hart, high energy
    if (bpm >= 120 && bpm <= 135 && intensity == "hart") {
        return "Techno";
    }
    
    // Trance: 130-160 BPM, high spectral centroid
    if (bpm >= 130 && bpm <= 160 && spectralCentroid > 0.5f) {
        return "Trance";
    }
    
    // Metal: 120-180 BPM, hart, high zero-crossing (Distortion)
    if (bpm >= 120 && bpm <= 180 && intensity == "hart" && zeroCrossing > 0.15f) {
        return "Metal";
    }
    
    // Rock: 90-140 BPM, mittlere Intensität
    if (bpm >= 90 && bpm <= 140 && intensity == "mittel") {
        return "Rock";
    }
    
    // Pop: 100-130 BPM, soft-mittel
    if (bpm >= 100 && bpm <= 130 && (intensity == "soft" || intensity == "mittel")) {
        return "Pop";
    }
    
    // Klassik: < 100 BPM, soft
    if (bpm < 100 && intensity == "soft") {
        return "Klassik";
    }
    
    // Dubstep: 140 BPM (half-time 70), basslastig
    if ((bpm >= 135 && bpm <= 145) || (bpm >= 65 && bpm <= 75)) {
        if (bassLevel == "basslastig") return "Dubstep";
    }
    
    // House: 120-130 BPM
    if (bpm >= 120 && bpm <= 130) {
        return "House";
    }
    
    return "Electronic";
}

std::string AudioAnalyzer::detectIntensity(const std::vector<float>& samples) {
    if (samples.empty()) return "mittel";
    
    // Berechne RMS (Root Mean Square) als Intensitäts-Maß
    float rms = 0.0f;
    for (float sample : samples) {
        rms += sample * sample;
    }
    if (samples.size() == 0) return "mittel";
    rms = std::sqrt(rms / samples.size());
    
    if (rms > 0.5f) return "hart";
    if (rms < 0.2f) return "soft";
    return "mittel";
}

std::string AudioAnalyzer::detectBassLevel(const std::vector<float>& samples, int sampleRate) {
    auto spectrum = performFFT(samples);
    
    // Bass = 20-250 Hz
    size_t bassEnd = static_cast<size_t>(250.0f * spectrum.size() / (sampleRate / 2.0f));
    
    float bassEnergy = 0.0f;
    float totalEnergy = 0.0f;
    
    for (size_t i = 0; i < spectrum.size(); ++i) {
        float mag = std::abs(spectrum[i]);
        totalEnergy += mag;
        if (i < bassEnd) bassEnergy += mag;
    }
    
    if (totalEnergy == 0.0f) return "mittel";
    float bassRatio = bassEnergy / totalEnergy;
    
    if (bassRatio > 0.4f) return "basslastig";
    if (bassRatio < 0.2f) return "soft";
    return "mittel";
}

float AudioAnalyzer::calculateSpectralCentroid(const std::vector<std::complex<float>>& spectrum) {
    if (spectrum.empty()) return 0.0f;
    
    float weightedSum = 0.0f;
    float magnitudeSum = 0.0f;
    
    for (size_t i = 0; i < spectrum.size(); ++i) {
        float magnitude = std::abs(spectrum[i]);
        weightedSum += i * magnitude;
        magnitudeSum += magnitude;
    }
    
    return magnitudeSum > 0 ? weightedSum / magnitudeSum : 0.0f;
}

// Overload with sampleRate for frequency-based centroid calculation
float AudioAnalyzer::calculateSpectralCentroid(const std::vector<float>& spectrum, int sampleRate) {
    if (spectrum.empty()) return 0.0f;
    
    float weightedSum = 0.0f;
    float magnitudeSum = 0.0f;
    float binWidth = static_cast<float>(sampleRate) / (2.0f * spectrum.size());
    
    for (size_t i = 0; i < spectrum.size(); ++i) {
        float frequency = i * binWidth;
        float magnitude = spectrum[i];
        weightedSum += frequency * magnitude;
        magnitudeSum += magnitude;
    }
    
    return magnitudeSum > 0 ? weightedSum / magnitudeSum : 0.0f;
}

float AudioAnalyzer::calculateSpectralRolloff(const std::vector<std::complex<float>>& spectrum, float threshold) {
    if (spectrum.empty()) return 0.0f;
    
    float totalEnergy = 0.0f;
    for (const auto& bin : spectrum) {
        totalEnergy += std::abs(bin);
    }
    
    float energySum = 0.0f;
    float targetEnergy = totalEnergy * threshold;
    
    for (size_t i = 0; i < spectrum.size(); ++i) {
        energySum += std::abs(spectrum[i]);
        if (energySum >= targetEnergy) {
            return static_cast<float>(i) / spectrum.size();
        }
    }
    
    return 1.0f;
}

float AudioAnalyzer::calculateZeroCrossingRate(const std::vector<float>& samples) {
    if (samples.size() < 2) return 0.0f;
    
    int crossings = 0;
    for (size_t i = 1; i < samples.size(); ++i) {
        if ((samples[i] >= 0 && samples[i-1] < 0) || (samples[i] < 0 && samples[i-1] >= 0)) {
            crossings++;
        }
    }
    
    return static_cast<float>(crossings) / samples.size();
}

float AudioAnalyzer::calculateMFCCHash(const std::vector<float>& samples, int sampleRate) {
    // Vereinfachter MFCC-Hash (eigentlich braucht man richtige MFCC-Berechnung)
    auto mfcc = extractMFCC(samples, sampleRate);
    
    float hash = 0.0f;
    for (size_t i = 0; i < mfcc.size(); ++i) {
        hash += mfcc[i] * (i + 1);
    }
    
    return hash;
}

std::vector<std::complex<float>> AudioAnalyzer::performFFT(const std::vector<float>& samples) {
#ifdef WITH_FFTW3
    // FFTW3-basierte FFT (schnell!)
    size_t N = std::min(samples.size(), size_t(8192));
    std::vector<std::complex<float>> spectrum(N / 2 + 1);
    
    fftwf_complex* out = reinterpret_cast<fftwf_complex*>(spectrum.data());
    float* in = const_cast<float*>(samples.data());
    
    fftwf_plan plan = fftwf_plan_dft_r2c_1d(N, in, out, FFTW_ESTIMATE);
    fftwf_execute(plan);
    fftwf_destroy_plan(plan);
    
    return spectrum;
#else
    // Fallback: Vereinfachte DFT
    size_t N = std::min(samples.size(), size_t(2048));  // Kleinere Größe ohne FFTW3
    std::vector<std::complex<float>> spectrum(N);
    
    const float pi = 3.14159265358979323846f;
    
    for (size_t k = 0; k < N; ++k) {
        std::complex<float> sum(0, 0);
        for (size_t n = 0; n < N; ++n) {
            float angle = 2 * pi * k * n / N;
            sum += samples[n] * std::complex<float>(std::cos(angle), -std::sin(angle));
        }
        spectrum[k] = sum;
    }
    
    return spectrum;
#endif
}

std::vector<float> AudioAnalyzer::extractMFCC(const std::vector<float>& samples, int sampleRate, int numCoeffs) {
    // MFCC-Berechnung (vereinfacht)
    auto spectrum = performFFT(samples);
    
    // Mel-Filterbank (26 Filter, typisch)
    const int numFilters = 26;
    std::vector<float> melEnergies(numFilters, 0.0f);
    
    // Vereinfachte Mel-Filter
    for (int i = 0; i < numFilters; ++i) {
        float centerFreq = 300.0f + (i * 200.0f);  // 300-5500 Hz
        size_t centerBin = static_cast<size_t>(centerFreq * spectrum.size() / (sampleRate / 2.0f));
        
        // Summiere Energie um Center-Bin
        for (size_t j = std::max(size_t(0), centerBin - 10); 
             j < std::min(spectrum.size(), centerBin + 10); ++j) {
            melEnergies[i] += std::abs(spectrum[j]);
        }
        
        // Log-Energie
        melEnergies[i] = std::log(melEnergies[i] + 1e-10f);
    }
    
    // DCT für MFCC-Koeffizienten
    std::vector<float> mfcc(numCoeffs, 0.0f);
    const float pi = 3.14159265358979323846f;
    
    for (int i = 0; i < numCoeffs; ++i) {
        for (int j = 0; j < numFilters; ++j) {
            mfcc[i] += melEnergies[j] * std::cos(pi * i * (j + 0.5f) / numFilters);
        }
    }
    
    return mfcc;
}

bool AudioAnalyzer::loadAudioFile(const std::string& filepath, std::vector<float>& samples, int& sampleRate) {
#ifdef WITH_SNDFILE
    // Use libsndfile for all formats (WAV, MP3, FLAC, OGG, etc.)
    SF_INFO sfinfo;
    std::memset(&sfinfo, 0, sizeof(sfinfo));
    
    SNDFILE* file = sf_open(filepath.c_str(), SFM_READ, &sfinfo);
    if (!file) {
        return false;  // Silently fail for unsupported formats
    }
    
    sampleRate = sfinfo.samplerate;
    size_t numSamples = sfinfo.frames * sfinfo.channels;
    
    std::vector<float> tempBuffer(numSamples);
    sf_count_t framesRead = sf_readf_float(file, tempBuffer.data(), sfinfo.frames);
    sf_close(file);
    
    if (framesRead <= 0) {
        return false;
    }
    
    // Convert to mono if stereo
    samples.clear();
    samples.reserve(framesRead);
    
    if (sfinfo.channels == 1) {
        samples.assign(tempBuffer.begin(), tempBuffer.begin() + framesRead);
    } else {
        // Mix down to mono (average channels)
        for (sf_count_t i = 0; i < framesRead; ++i) {
            float sum = 0.0f;
            for (int ch = 0; ch < sfinfo.channels; ++ch) {
                sum += tempBuffer[i * sfinfo.channels + ch];
            }
            samples.push_back(sum / sfinfo.channels);
        }
    }
    
    return true;
#else
    // Fallback: WAV-only support
    if (filepath.find(".wav") != std::string::npos || filepath.find(".WAV") != std::string::npos) {
        return loadWAV(filepath, samples, sampleRate);
    }
    return false;  // Silently skip unsupported formats
#endif
}

bool AudioAnalyzer::loadWAV(const std::string& filepath, std::vector<float>& samples, int& sampleRate) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    // WAV-Header lesen
    char header[44];
    file.read(header, 44);
    
    if (!file.good() || file.gcount() != 44) {
        file.close();
        return false;
    }
    
    // Validate RIFF header
    if (strncmp(header, "RIFF", 4) != 0 || strncmp(header + 8, "WAVE", 4) != 0) {
        file.close();
        return false;
    }
    
    // Extract header info
    short numChannels = *reinterpret_cast<short*>(&header[22]);
    sampleRate = *reinterpret_cast<int*>(&header[24]);
    short bitsPerSample = *reinterpret_cast<short*>(&header[34]);
    
    // Validate critical values
    if (numChannels <= 0 || numChannels > 16 || 
        sampleRate <= 0 || sampleRate > 192000 ||
        bitsPerSample <= 0 || bitsPerSample > 32) {
        file.close();
        return false;
    }
    
    // Get file size
    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    if (fileSize <= 44) {
        file.close();
        return false;
    }
    file.seekg(44);
    
    size_t dataSize = fileSize - 44;
    size_t bytesPerSample = bitsPerSample / 8;
    size_t totalSamples = dataSize / bytesPerSample;
    size_t monoSamples = totalSamples / numChannels;
    
    if (totalSamples == 0 || monoSamples == 0) {
        file.close();
        return false;
    }
    
    samples.clear();
    samples.reserve(monoSamples);
    
    if (bitsPerSample == 16) {
        std::vector<short> rawSamples(totalSamples);
        file.read(reinterpret_cast<char*>(rawSamples.data()), dataSize);
        
        if (!file.good()) {
            file.close();
            return false;
        }
        
        // Convert to mono float [-1, 1]
        for (size_t i = 0; i < totalSamples; i += numChannels) {
            float monoSample = 0.0f;
            for (short ch = 0; ch < numChannels; ++ch) {
                monoSample += rawSamples[i + ch] / 32768.0f;
            }
            samples.push_back(monoSample / numChannels);
        }
    } else {
        // Unsupported bit depth
        file.close();
        return false;
    }
    
    file.close();
    return true;
}

std::vector<float> AudioAnalyzer::runNPUInference(const std::vector<float>& features) {
    if (!useNPU_) {
        return features;  // CPU-Fallback
    }
    
#ifdef WITH_OPENVINO
    try {
        // OpenVINO Inferenz (Beispiel-Code - benötigt trainiertes Modell)
        Core core;
        
        // TODO: Lade vortrainiertes Modell für Genre-Klassifikation
        // auto model = core.read_model("models/genre_classifier.xml");
        // auto compiled = core.compile_model(model, "NPU");
        // auto infer_request = compiled.create_infer_request();
        
        // Für jetzt: Nutze optimierte CPU-Berechnung
        std::vector<float> npuOutput = features;
        for (auto& val : npuOutput) {
            val = std::tanh(val * 1.2f);  // Aktivierungsfunktion
        }
        return npuOutput;
        
    } catch (const std::exception& e) {
        std::cerr << "OpenVINO Inferenz Fehler: " << e.what() << "\n";
    }
#endif
    
    // Fallback: CPU-basierte Verarbeitung
    std::vector<float> npuOutput = features;
    for (auto& val : npuOutput) {
        val = std::tanh(val * 1.2f);
    }
    return npuOutput;
}

// ===== RHYTHMUS-ANALYSE =====

std::vector<float> AudioAnalyzer::detectOnsets(const std::vector<float>& samples, int sampleRate) {
    // Onset-Detection via Energie-Differenz
    std::vector<float> onsets;
    const int windowSize = sampleRate / 20; // 50ms Windows
    const int hopSize = windowSize / 2;
    
    for (size_t i = 0; i + windowSize < samples.size(); i += hopSize) {
        float energy = 0.0f;
        for (int j = 0; j < windowSize; j++) {
            energy += samples[i + j] * samples[i + j];
        }
        energy /= windowSize;
        
        if (onsets.empty() || energy > onsets.back() * 1.3f) {
            onsets.push_back(static_cast<float>(i) / sampleRate);
        }
    }
    
    return onsets;
}

std::vector<int> AudioAnalyzer::analyzeTimingPattern(const std::vector<float>& onsets, float bpm) {
    // Berechne Timing-Pattern relativ zum Beat
    std::vector<int> pattern;
    if (onsets.empty() || bpm < 60) return pattern;
    
    float beatDuration = 60.0f / bpm; // Sekunden pro Beat
    
    for (size_t i = 1; i < onsets.size(); i++) {
        float interval = onsets[i] - onsets[i-1];
        int beatPosition = static_cast<int>((interval / beatDuration) * 4); // Quantize zu 16tel
        pattern.push_back(beatPosition);
    }
    
    return pattern;
}

std::string AudioAnalyzer::classifyRhythmComplexity(const std::vector<int>& pattern) {
    if (pattern.empty()) return "Unknown";
    
    // Berechne Varianz
    float mean = 0.0f;
    for (int p : pattern) mean += p;
    mean /= pattern.size();
    
    float variance = 0.0f;
    for (int p : pattern) {
        float diff = p - mean;
        variance += diff * diff;
    }
    variance /= pattern.size();
    
    if (variance < 0.5f) return "Simple";      // 4/4 straight
    if (variance < 2.0f) return "Moderate";    // Leichte Synkopen
    if (variance < 5.0f) return "Complex";     // Viele Variationen
    return "Very Complex";                     // Jazz/Prog
}

std::string AudioAnalyzer::analyzeRhythmPattern(const std::vector<float>& samples, int sampleRate, float bpm) {
    auto onsets = detectOnsets(samples, sampleRate);
    auto pattern = analyzeTimingPattern(onsets, bpm);
    std::string complexity = classifyRhythmComplexity(pattern);
    
    // Erkenne spezifische Patterns
    if (complexity == "Simple" && bpm >= 120 && bpm <= 140) {
        return "Four-on-the-floor"; // Techno/House
    }
    if (complexity == "Moderate" && bpm >= 160 && bpm <= 180) {
        return "Breakbeat"; // Drum & Bass
    }
    if (complexity == "Complex" && bpm >= 80 && bpm <= 100) {
        return "Hip-Hop/Trap";
    }
    if (complexity == "Very Complex") {
        return "Jazz/Progressive";
    }
    
    return complexity;
}

// ===== STIL-ERKENNUNG =====

std::string AudioAnalyzer::detectMusicalStyle(const MediaMetadata& meta) {
    // Kombiniere alle Features für detaillierte Stil-Erkennung
    std::string style = meta.genre;
    
    // Sub-Genre basierend auf Features
    if (meta.genre == "Electronic" || meta.genre == "Techno") {
        if (meta.bpm >= 125 && meta.bpm <= 135 && meta.spectralCentroid > 2000) {
            style += " / Trance";
        } else if (meta.bpm >= 140 && meta.bpm <= 150) {
            style += " / Hardstyle";
        } else if (meta.bpm >= 120 && meta.bpm <= 130) {
            style += " / House";
        }
    }
    
    if (meta.genre == "Metal" || meta.genre == "Rock") {
        if (meta.zeroCrossingRate > 0.15f) {
            style += " / Thrash";
        } else if (meta.bpm < 100) {
            style += " / Doom";
        }
    }
    
    return style;
}

std::vector<std::string> AudioAnalyzer::extractStyleTags(const MediaMetadata& meta) {
    std::vector<std::string> tags;
    
    // Genre-Tags
    tags.push_back(meta.genre);
    
    // BPM-basierte Tags
    if (meta.bpm < 90) tags.push_back("Slow");
    else if (meta.bpm < 120) tags.push_back("Medium");
    else if (meta.bpm < 140) tags.push_back("Fast");
    else tags.push_back("Very Fast");
    
    // Spektrale Tags
    if (meta.spectralCentroid < 1500) tags.push_back("Dark");
    else if (meta.spectralCentroid > 3000) tags.push_back("Bright");
    
    if (meta.zeroCrossingRate > 0.12f) tags.push_back("Aggressive");
    else tags.push_back("Smooth");
    
    // Energie-Tags (basierend auf Genre)
    if (meta.genre == "Techno" || meta.genre == "Metal") {
        tags.push_back("High Energy");
    } else if (meta.genre == "Klassik" || meta.genre == "Jazz") {
        tags.push_back("Low Energy");
    }
    
    return tags;
}

// ===== AUTOMATISCHE SORTIERUNG =====

std::string AudioAnalyzer::suggestCategory(const MediaMetadata& meta) {
    // Generiere Pfad-Struktur: Genre/Style/BPM-Range
    std::string category = meta.genre;
    
    // BPM-Ranges
    std::string bpmRange;
    if (meta.bpm < 90) bpmRange = "Slow (60-90 BPM)";
    else if (meta.bpm < 120) bpmRange = "Medium (90-120 BPM)";
    else if (meta.bpm < 140) bpmRange = "Fast (120-140 BPM)";
    else if (meta.bpm < 160) bpmRange = "Very Fast (140-160 BPM)";
    else bpmRange = "Ultra Fast (160+ BPM)";
    
    category += "/" + bpmRange;
    
    // Style-Modifier
    if (meta.spectralCentroid > 3000) {
        category += "/Bright";
    } else if (meta.spectralCentroid < 1500) {
        category += "/Dark";
    }
    
    return category;
}

std::vector<AudioAnalyzer::SortCategory> AudioAnalyzer::generateSortStructure(
    const std::vector<MediaMetadata>& files) {
    
    std::vector<SortCategory> categories;
    std::map<std::string, std::vector<std::string>> genreMap;
    
    // Gruppiere nach Genre
    for (const auto& file : files) {
        std::string category = suggestCategory(file);
        genreMap[category].push_back(file.filepath);
    }
    
    // Erstelle SortCategory-Strukturen
    for (const auto& [path, filepaths] : genreMap) {
        SortCategory cat;
        cat.name = path;
        cat.path = "Sorted Music/" + path;
        cat.criteria = filepaths;
        categories.push_back(cat);
    }
    
    return categories;
}

// === Genre Detection from Audio Features ===
std::string AudioAnalyzer::detectGenreFromAudio(const std::vector<float>& samples, int sampleRate, float bpm) {
    if (samples.empty()) return "Unknown";
    
    // Calculate spectral centroid
    std::vector<std::complex<float>> complexSpectrum = performFFT(samples);
    
    // Convert to magnitude spectrum
    std::vector<float> spectrum;
    spectrum.reserve(complexSpectrum.size());
    for (const auto& c : complexSpectrum) {
        spectrum.push_back(std::abs(c));
    }
    
    float spectralCentroid = calculateSpectralCentroid(spectrum, sampleRate);
    
    // Calculate energy
    float energy = 0.0f;
    for (float s : samples) energy += s * s;
    energy = std::sqrt(energy / samples.size());
    
    // BPM-based primary classification
    if (bpm >= 165 && bpm <= 185) {
        // Could be DnB, Salsa, or Samba
        if (spectralCentroid > 2500) return "Drum'n'Bass";
        return "Salsa";
    }
    
    if (bpm >= 175 && bpm <= 190) {
        // Walzer is typically 3/4 time signature around 180 BPM (60 bars/min)
        return "Walzer";
    }
    
    if (bpm >= 140 && bpm <= 160) {
        if (energy > 0.4f) return "Techno";
        return "Trance";
    }
    
    if (bpm >= 120 && bpm <= 135) {
        if (spectralCentroid > 2000) return "House";
        return "Techno";
    }
    
    if (bpm >= 85 && bpm <= 115) {
        if (spectralCentroid < 1500) return "Jazz";
        if (energy < 0.3f) return "RnB";
        return "Hip-Hop";
    }
    
    if (bpm >= 60 && bpm <= 85) {
        if (energy < 0.25f) return "Ambient";
        if (spectralCentroid < 1800) return "Chillout";
        return "Electronic";
    }
    
    // Spectral-based fallback
    if (spectralCentroid > 3000) return "Electronic";
    if (spectralCentroid < 1200) return "Classical";
    
    return "Unknown";
}

// === Clipping Detection ===
AudioAnalyzer::ClippingInfo AudioAnalyzer::detectClipping(const std::vector<float>& samples) {
    ClippingInfo info;
    info.hasClipping = false;
    info.clippingPercentage = 0.0f;
    info.clippedSamples = 0;
    info.peakLevel = 0.0f;
    info.recommendedGain = 1.0f;
    
    if (samples.empty()) return info;
    
    const float clippingThreshold = 0.99f;
    
    for (float sample : samples) {
        float absSample = std::abs(sample);
        
        if (absSample > info.peakLevel) {
            info.peakLevel = absSample;
        }
        
        if (absSample >= clippingThreshold) {
            info.clippedSamples++;
        }
    }
    
    info.clippingPercentage = (100.0f * info.clippedSamples) / samples.size();
    info.hasClipping = info.clippedSamples > 0;
    
    if (info.peakLevel > 0.0f) {
        info.recommendedGain = 0.95f / info.peakLevel;
    }
    
    return info;
}

// === Apply Declipping Algorithm ===
std::vector<float> AudioAnalyzer::applyDeclipping(const std::vector<float>& samples) {
    if (samples.size() < 3) return samples;
    
    std::vector<float> repaired = samples;
    const float clippingThreshold = 0.99f;
    
    for (size_t i = 1; i < samples.size() - 1; i++) {
        float absSample = std::abs(samples[i]);
        
        if (absSample >= clippingThreshold) {
            // Use cubic interpolation with neighbors
            float prev = (i > 0) ? samples[i-1] : 0.0f;
            float next = (i < samples.size() - 1) ? samples[i+1] : 0.0f;
            
            // Simple linear interpolation
            repaired[i] = (prev + next) * 0.5f;
            
            // If neighbors are also clipped, use average of surrounding area
            if (std::abs(prev) >= clippingThreshold || std::abs(next) >= clippingThreshold) {
                int windowSize = 5;
                float sum = 0.0f;
                int count = 0;
                
                for (int j = -windowSize; j <= windowSize; j++) {
                    int idx = i + j;
                    if (idx >= 0 && idx < (int)samples.size() && std::abs(samples[idx]) < clippingThreshold) {
                        sum += samples[idx];
                        count++;
                    }
                }
                
                if (count > 0) {
                    repaired[i] = sum / count;
                }
            }
        }
    }
    
    return repaired;
}

// === Repair Clipping in Audio File ===
bool AudioAnalyzer::repairClipping(const std::string& inputPath, const std::string& outputPath, float targetPeak) {
    // Load audio file
    std::vector<float> samples;
    int sampleRate = 0;
    
    if (!loadAudioFile(inputPath, samples, sampleRate)) {
        return false;
    }
    
    // Apply declipping
    std::vector<float> repaired = applyDeclipping(samples);
    
    // Normalize to target peak
    float currentPeak = 0.0f;
    for (float sample : repaired) {
        float absSample = std::abs(sample);
        if (absSample > currentPeak) {
            currentPeak = absSample;
        }
    }
    
    if (currentPeak > 0.0f) {
        float gain = targetPeak / currentPeak;
        for (float& sample : repaired) {
            sample *= gain;
        }
    }
    
    // Save repaired audio
    // Note: This requires a WAV writer implementation
    // For now, we'll use a simple approach - you may need to add proper WAV writing
    
    // Check if we can use libsndfile or similar
    #ifdef USE_LIBSNDFILE
    SF_INFO sfInfo;
    sfInfo.samplerate = sampleRate;
    sfInfo.channels = 1; // Mono for now
    sfInfo.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
    
    SNDFILE* outFile = sf_open(outputPath.c_str(), SFM_WRITE, &sfInfo);
    if (!outFile) return false;
    
    sf_write_float(outFile, repaired.data(), repaired.size());
    sf_close(outFile);
    return true;
    #else
    // Basic WAV writer fallback
    std::ofstream out(outputPath, std::ios::binary);
    if (!out.is_open()) return false;
    
    // WAV header
    int dataSize = repaired.size() * sizeof(int16_t);
    out.write("RIFF", 4);
    int chunkSize = 36 + dataSize;
    out.write(reinterpret_cast<char*>(&chunkSize), 4);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    int subchunk1Size = 16;
    out.write(reinterpret_cast<char*>(&subchunk1Size), 4);
    int16_t audioFormat = 1; // PCM
    out.write(reinterpret_cast<char*>(&audioFormat), 2);
    int16_t numChannels = 1;
    out.write(reinterpret_cast<char*>(&numChannels), 2);
    out.write(reinterpret_cast<char*>(&sampleRate), 4);
    int byteRate = sampleRate * numChannels * sizeof(int16_t);
    out.write(reinterpret_cast<char*>(&byteRate), 4);
    int16_t blockAlign = numChannels * sizeof(int16_t);
    out.write(reinterpret_cast<char*>(&blockAlign), 2);
    int16_t bitsPerSample = 16;
    out.write(reinterpret_cast<char*>(&bitsPerSample), 2);
    out.write("data", 4);
    out.write(reinterpret_cast<char*>(&dataSize), 4);
    
    // Convert float to int16 and write
    for (float sample : repaired) {
        int16_t intSample = static_cast<int16_t>(sample * 32767.0f);
        out.write(reinterpret_cast<char*>(&intSample), sizeof(int16_t));
    }
    
    out.close();
    return true;
    #endif
}
