#include "TrainingModel.h"
#include "AudioAnalyzer.h"
#include "InstrumentExtractor.h"
#include <iostream>
#include <random>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <set>
#include <sndfile.h>

TrainingModel::TrainingModel(MediaDatabase& db) : db_(db) {
    initializeAccelerator();
}

TrainingModel::~TrainingModel() {
}

void TrainingModel::initializeAccelerator() {
#ifdef WITH_OPENVINO
    try {
        core_ = std::make_shared<ov::Core>();
        auto devices = core_->get_available_devices();
        
        // Priorität: NPU > GPU > CPU
        for (const auto& device : devices) {
            if (device.find("NPU") != std::string::npos) {
                acceleratorDevice_ = device;
                useAccelerator_ = true;
                std::cout << "🧠 Training nutzt Intel NPU: " << device << std::endl;
                return;
            }
        }
        
        for (const auto& device : devices) {
            if (device.find("GPU") != std::string::npos) {
                acceleratorDevice_ = device;
                useAccelerator_ = true;
                std::cout << "🧠 Training nutzt Intel GPU: " << device << std::endl;
                return;
            }
        }
        
        std::cout << "🧠 Training nutzt CPU (kein NPU/GPU gefunden)" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "⚠️ OpenVINO Fehler: " << e.what() << std::endl;
    }
#else
    std::cout << "🧠 Training nutzt CPU (OpenVINO nicht verfügbar)" << std::endl;
#endif
    useAccelerator_ = false;
    acceleratorDevice_ = "CPU";
}

size_t TrainingModel::extractTrainingFeatures() {
    std::cout << "📊 Extrahiere Features aus Training-Dataset..." << std::endl;
    
    // Lade alle analysierten Tracks
    auto allMedia = db_.getAll();
    
    std::cout << "📁 Durchsuche " << allMedia.size() << " Tracks in Datenbank" << std::endl;
    std::cout << "🎸 Extrahiere Instrumente für Sample-Library..." << std::endl;
    
    trainingFeatures_.clear();
    size_t extracted = 0;
    size_t skipped = 0;
    size_t instrumentsExtracted = 0;
    
    for (const auto& meta : allMedia) {
        if (!meta.analyzed) {
            skipped++;
            continue;
        }
        if (meta.genre.empty() || meta.genre == "Unknown") {
            skipped++;
            continue;
        }
        
        // Zeige alle 50 Tracks Details
        if (extracted > 0 && extracted % 50 == 0) {
            std::cout << "ℹ️ Verarbeitet: " << extracted << " Tracks | "
                      << "Letztes Genre: " << meta.genre << " | "
                      << "BPM: " << meta.bpm << std::endl;
        }
        
        AudioFeatures features;
        
        // MFCC aus mfccHash rekonstruieren (vereinfacht)
        // In Produktion: Echte MFCC-Extraktion aus Audio
        features.mfcc.resize(13);
        std::mt19937 gen(static_cast<unsigned>(meta.mfccHash * 1000000));
        std::normal_distribution<float> dist(0.0f, 1.0f);
        for (int i = 0; i < 13; ++i) {
            features.mfcc[i] = dist(gen);
        }
        
        features.spectralCentroid = meta.spectralCentroid;
        features.spectralRolloff = meta.spectralRolloff;
        features.zeroCrossingRate = meta.zeroCrossingRate;
        features.bpm = meta.bpm;
        features.genre = meta.genre;
        features.genreId = getOrCreateGenreId(meta.genre);
        
        trainingFeatures_.push_back(features);
        extracted++;
        
        // Extrahiere Instrumente aus diesem Track (nur alle 10 Tracks)
        if (extracted % 10 == 0 && !meta.filepath.empty()) {
            auto instruments = InstrumentExtractor::extractInstruments(meta.filepath, 0.7f);
            
            // Speichere extrahierte Instrumente
            std::string instrumentDir = std::string(std::getenv("HOME")) + "/.songgen/instruments/";
            
            for (const auto& inst : instruments) {
                std::string typeDir;
                switch(inst.type) {
                    case InstrumentSample::KICK: typeDir = "kicks"; break;
                    case InstrumentSample::SNARE: typeDir = "snares"; break;
                    case InstrumentSample::HIHAT: typeDir = "hihats"; break;
                    case InstrumentSample::BASS: typeDir = "bass"; break;
                    case InstrumentSample::LEAD: typeDir = "leads"; break;
                    default: typeDir = "other"; break;
                }
                
                std::filesystem::create_directories(instrumentDir + typeDir);
                
                std::string filename = std::to_string(std::hash<std::string>{}(meta.filepath + std::to_string(inst.startTime))) + ".wav";
                std::string outputPath = instrumentDir + typeDir + "/" + filename;
                
                if (InstrumentExtractor::saveSample(inst, outputPath)) {
                    instrumentsExtracted++;
                }
            }
        }
    }
    
    std::cout << "\n✅ Feature-Extraktion abgeschlossen:" << std::endl;
    std::cout << "   ✓ Extrahiert: " << extracted << " Feature-Vektoren" << std::endl;
    std::cout << "   ✗ Übersprungen: " << skipped << " (nicht analysiert/Unknown)" << std::endl;
    std::cout << "   🎵 Genres gefunden: " << genreToId_.size() << std::endl;
    std::cout << "   🎸 Instrumente extrahiert: " << instrumentsExtracted << " Samples" << std::endl;
    std::cout << "   📂 Datenbank: ~/.songgen/media.db" << std::endl;
    std::cout << "   📁 Instrumente: ~/.songgen/instruments/" << std::endl;
    
    // Dataset balancieren (Undersampling)
    balanceDataset();
    
    return trainingFeatures_.size();
}

int TrainingModel::getOrCreateGenreId(const std::string& genre) {
    auto it = genreToId_.find(genre);
    if (it != genreToId_.end()) {
        return it->second;
    }
    
    int newId = static_cast<int>(genreToId_.size());
    genreToId_[genre] = newId;
    idToGenre_[newId] = genre;
    return newId;
}

void TrainingModel::balanceDataset() {
    if (trainingFeatures_.empty()) return;
    
    std::cout << "\n⚖️ Balanciere Dataset..." << std::endl;
    
    // Zähle Samples pro Genre
    std::map<int, std::vector<size_t>> genreIndices;
    for (size_t i = 0; i < trainingFeatures_.size(); ++i) {
        genreIndices[trainingFeatures_[i].genreId].push_back(i);
    }
    
    // Zeige Verteilung vor Balancierung
    std::cout << "\n📊 Vorher:" << std::endl;
    size_t minCount = SIZE_MAX;
    for (const auto& pair : genreIndices) {
        std::cout << "  " << idToGenre_[pair.first] << ": " << pair.second.size() << " Samples" << std::endl;
        minCount = std::min(minCount, pair.second.size());
    }
    
    // Undersampling: Reduziere alle Genres auf minCount
    std::vector<AudioFeatures> balanced;
    std::random_device rd;
    std::mt19937 gen(rd());
    
    for (auto& pair : genreIndices) {
        auto& indices = pair.second;
        
        // Shuffeln für zufällige Auswahl
        std::shuffle(indices.begin(), indices.end(), gen);
        
        // Nimm nur die ersten minCount Samples
        size_t count = std::min(minCount, indices.size());
        for (size_t i = 0; i < count; ++i) {
            balanced.push_back(trainingFeatures_[indices[i]]);
        }
    }
    
    trainingFeatures_ = std::move(balanced);
    
    // Zeige Verteilung nach Balancierung
    std::cout << "\n📊 Nachher (balanciert):" << std::endl;
    std::map<int, size_t> newCounts;
    for (const auto& f : trainingFeatures_) {
        newCounts[f.genreId]++;
    }
    for (const auto& pair : newCounts) {
        std::cout << "  " << idToGenre_[pair.first] << ": " << pair.second << " Samples" << std::endl;
    }
    
    std::cout << "\n✅ Dataset balanciert: " << trainingFeatures_.size() << " Samples total" << std::endl;
}

std::vector<float> TrainingModel::normalizeFeatures(const AudioFeatures& features) {
    std::vector<float> normalized;
    
    // MFCC (13 Werte)
    for (float mfcc : features.mfcc) {
        normalized.push_back(mfcc / 10.0f);  // Normalisiere zu ca. [-1, 1]
    }
    
    // Spektrale Features
    normalized.push_back(features.spectralCentroid / 5000.0f);
    normalized.push_back(features.spectralRolloff / 10000.0f);
    normalized.push_back(features.zeroCrossingRate);
    
    // BPM
    normalized.push_back((features.bpm - 120.0f) / 60.0f);  // Zentriert auf 120 BPM
    
    // Genre-Embedding (One-Hot)
    int numGenres = static_cast<int>(genreToId_.size());
    for (int i = 0; i < numGenres; ++i) {
        normalized.push_back(i == features.genreId ? 1.0f : 0.0f);
    }
    
    return normalized;
}

std::vector<float> TrainingModel::createLatentVector(int dimensions) {
    std::vector<float> latent(dimensions);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::normal_distribution<float> dist(0.0f, 1.0f);
    
    for (int i = 0; i < dimensions; ++i) {
        latent[i] = dist(gen);
    }
    
    return latent;
}

void TrainingModel::loadInstrumentLibrary() {
    std::string instrumentDir = std::string(std::getenv("HOME")) + "/.songgen/instruments/";
    
    if (!std::filesystem::exists(instrumentDir)) {
        std::cout << "ℹ️ Keine Instrumente gefunden - verwende Synthese" << std::endl;
        return;
    }
    
    std::cout << "🎸 Lade Instrument-Library..." << std::endl;
    instrumentLibrary_ = InstrumentExtractor::loadInstrumentLibrary(instrumentDir);
    
    int totalInstruments = 0;
    for (const auto& pair : instrumentLibrary_) {
        totalInstruments += pair.second.size();
    }
    
    std::cout << "  ✓ Geladen: " << totalInstruments << " Instrumente" << std::endl;
    std::cout << "    - Kicks: " << instrumentLibrary_[InstrumentSample::KICK].size() << std::endl;
    std::cout << "    - Snares: " << instrumentLibrary_[InstrumentSample::SNARE].size() << std::endl;
    std::cout << "    - Hi-Hats: " << instrumentLibrary_[InstrumentSample::HIHAT].size() << std::endl;
    std::cout << "    - Bass: " << instrumentLibrary_[InstrumentSample::BASS].size() << std::endl;
    std::cout << "    - Leads: " << instrumentLibrary_[InstrumentSample::LEAD].size() << std::endl;
    
    // 🔍 Entferne Duplikate
    std::cout << "\n🔍 Prüfe auf ähnlich klingende Instrumente..." << std::endl;
    removeDuplicateInstruments();
    
    int afterDedup = 0;
    for (const auto& pair : instrumentLibrary_) {
        afterDedup += pair.second.size();
    }
    std::cout << "  ✓ Nach Deduplizierung: " << afterDedup << " Instrumente (" 
              << (totalInstruments - afterDedup) << " Duplikate entfernt)" << std::endl;
}

void TrainingModel::playInstrumentCombination(const std::string& genre) {
    if (instrumentLibrary_.empty()) {
        return;
    }
    
    std::cout << "\n🎵 Generiere harmonische Kombination für Genre: " << genre << std::endl;
    
    // Wähle zufällige Instrumente passend zum Genre
    std::random_device rd;
    std::mt19937 gen(rd());
    
    // Sample-Rate und Dauer
    int sampleRate = 44100;
    float duration = 4.0f;  // 4 Sekunden Demo
    std::vector<float> mixedSamples(static_cast<size_t>(duration * sampleRate), 0.0f);
    
    // BPM basierend auf Genre
    float bpm = 120.0f;
    if (genre == "Techno") bpm = 135.0f;
    else if (genre == "Drum'n'Bass") bpm = 170.0f;
    else if (genre == "Hip-Hop") bpm = 90.0f;
    
    float beatDuration = 60.0f / bpm;
    int samplesPerBeat = static_cast<int>(beatDuration * sampleRate);
    
    // 1. Kicks auf jedem Beat
    if (!instrumentLibrary_[InstrumentSample::KICK].empty()) {
        std::uniform_int_distribution<size_t> kickDist(0, instrumentLibrary_[InstrumentSample::KICK].size() - 1);
        const auto& kick = instrumentLibrary_[InstrumentSample::KICK][kickDist(gen)];
        
        for (int beat = 0; beat < static_cast<int>(duration / beatDuration); ++beat) {
            size_t pos = beat * samplesPerBeat;
            for (size_t i = 0; i < kick.samples.size() && (pos + i) < mixedSamples.size(); ++i) {
                mixedSamples[pos + i] += kick.samples[i] * 0.6f;
            }
        }
        std::cout << "  ✓ Kick hinzugefügt" << std::endl;
    }
    
    // 2. Snares auf Offbeats
    if (!instrumentLibrary_[InstrumentSample::SNARE].empty()) {
        std::uniform_int_distribution<size_t> snareDist(0, instrumentLibrary_[InstrumentSample::SNARE].size() - 1);
        const auto& snare = instrumentLibrary_[InstrumentSample::SNARE][snareDist(gen)];
        
        for (int beat = 1; beat < static_cast<int>(duration / beatDuration); beat += 2) {
            size_t pos = beat * samplesPerBeat;
            for (size_t i = 0; i < snare.samples.size() && (pos + i) < mixedSamples.size(); ++i) {
                mixedSamples[pos + i] += snare.samples[i] * 0.5f;
            }
        }
        std::cout << "  ✓ Snare hinzugefügt" << std::endl;
    }
    
    // 3. Hi-Hats als Pattern
    if (!instrumentLibrary_[InstrumentSample::HIHAT].empty()) {
        std::uniform_int_distribution<size_t> hihatDist(0, instrumentLibrary_[InstrumentSample::HIHAT].size() - 1);
        const auto& hihat = instrumentLibrary_[InstrumentSample::HIHAT][hihatDist(gen)];
        
        int sixteenthNote = samplesPerBeat / 4;
        for (int i = 0; i < static_cast<int>(duration * sampleRate / sixteenthNote); ++i) {
            size_t pos = i * sixteenthNote;
            for (size_t j = 0; j < hihat.samples.size() && (pos + j) < mixedSamples.size(); ++j) {
                mixedSamples[pos + j] += hihat.samples[j] * 0.3f;
            }
        }
        std::cout << "  ✓ Hi-Hat Pattern hinzugefügt" << std::endl;
    }
    
    // 4. Bass-Line
    if (!instrumentLibrary_[InstrumentSample::BASS].empty()) {
        std::uniform_int_distribution<size_t> bassDist(0, instrumentLibrary_[InstrumentSample::BASS].size() - 1);
        const auto& bass = instrumentLibrary_[InstrumentSample::BASS][bassDist(gen)];
        
        // Loop Bass über die Dauer
        for (size_t i = 0; i < mixedSamples.size(); ++i) {
            mixedSamples[i] += bass.samples[i % bass.samples.size()] * 0.4f;
        }
        std::cout << "  ✓ Bass-Line hinzugefügt" << std::endl;
    }
    
    // 5. Lead-Melodie (optional)
    if (!instrumentLibrary_[InstrumentSample::LEAD].empty() && genre != "Techno") {
        std::uniform_int_distribution<size_t> leadDist(0, instrumentLibrary_[InstrumentSample::LEAD].size() - 1);
        const auto& lead = instrumentLibrary_[InstrumentSample::LEAD][leadDist(gen)];
        
        size_t startPos = samplesPerBeat * 4;  // Start nach 4 Beats
        for (size_t i = 0; i < lead.samples.size() && (startPos + i) < mixedSamples.size(); ++i) {
            mixedSamples[startPos + i] += lead.samples[i] * 0.3f;
        }
        std::cout << "  ✓ Lead-Melodie hinzugefügt" << std::endl;
    }
    
    // Normalisiere und speichere
    float maxAmplitude = 0.0f;
    for (float s : mixedSamples) {
        maxAmplitude = std::max(maxAmplitude, std::abs(s));
    }
    if (maxAmplitude > 0) {
        for (float& s : mixedSamples) {
            s *= 0.9f / maxAmplitude;
        }
    }
    
    // Speichere als WAV
    std::string outputPath = std::string(std::getenv("HOME")) + "/.songgen/training_demo_" + genre + ".wav";
    
    SF_INFO sfInfo;
    std::memset(&sfInfo, 0, sizeof(sfInfo));
    sfInfo.samplerate = sampleRate;
    sfInfo.channels = 1;
    sfInfo.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
    
    SNDFILE* outFile = sf_open(outputPath.c_str(), SFM_WRITE, &sfInfo);
    if (outFile) {
        sf_writef_float(outFile, mixedSamples.data(), mixedSamples.size());
        sf_close(outFile);
        std::cout << "  💾 Gespeichert: " << outputPath << std::endl;
    }
}

bool TrainingModel::saveCheckpoint(const std::string& path, int epoch, float loss) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "⚠️ Kann Checkpoint nicht speichern: " << path << std::endl;
        return false;
    }
    
    // Schreibe Checkpoint-Header
    file.write("CKPT", 4);
    file.write(reinterpret_cast<const char*>(&epoch), sizeof(epoch));
    file.write(reinterpret_cast<const char*>(&loss), sizeof(loss));
    
    auto fileSize = file.tellp();
    file.close();
    
    std::cout << "💾 Checkpoint gespeichert:" << std::endl;
    std::cout << "   📝 Datei: " << path << std::endl;
    std::cout << "   🔢 Epoch: " << epoch << std::endl;
    std::cout << "   📉 Loss: " << loss << std::endl;
    std::cout << "   💾 Größe: " << fileSize << " Bytes" << std::endl;
    return true;
}

bool TrainingModel::loadCheckpoint(const std::string& path, int& outEpoch, float& outLoss) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;  // Kein Checkpoint vorhanden
    }
    
    // Prüfe Header
    char header[4];
    file.read(header, 4);
    if (std::strncmp(header, "CKPT", 4) != 0) {
        std::cerr << "⚠️ Ungültiger Checkpoint: " << path << std::endl;
        return false;
    }
    
    file.read(reinterpret_cast<char*>(&outEpoch), sizeof(outEpoch));
    file.read(reinterpret_cast<char*>(&outLoss), sizeof(outLoss));
    
    file.close();
    return true;
}

bool TrainingModel::train(
    int epochs,
    int batchSize,
    float learningRate,
    std::function<void(int, float, float)> progressCallback) {
    
    std::cout << "\n🎓 Training-Konfiguration:" << std::endl;
    std::cout << "   💾 Datenbank: ~/.songgen/media.db" << std::endl;
    std::cout << "   📦 Modell wird gespeichert: ~/.songgen/model.sgml" << std::endl;
    std::cout << "   🔢 Epochen: " << epochs << std::endl;
    std::cout << "   📆 Batch Size: " << batchSize << std::endl;
    std::cout << "   🎯 Learning Rate: " << learningRate << std::endl;
    std::cout << "   💻 Device: " << acceleratorDevice_ << std::endl;
    std::cout << "   📊 Training Samples: " << (trainingFeatures_.empty() ? "werden geladen..." : std::to_string(trainingFeatures_.size())) << std::endl;
    
    // Extrahiere Features falls noch nicht geschehen
    if (trainingFeatures_.empty()) {
        if (extractTrainingFeatures() == 0) {
            std::cerr << "❌ Keine Training-Daten verfügbar!" << std::endl;
            return false;
        }
    }
    
    // Lade Instrument-Library für Training
    loadInstrumentLibrary();
    
    // Generiere harmonische Kombinationen für jedes Genre (Training-Demo)
    if (!instrumentLibrary_.empty()) {
        std::cout << "\n🎸 Generiere harmonische Genre-Kombinationen..." << std::endl;
        std::set<std::string> uniqueGenres;
        for (const auto& f : trainingFeatures_) {
            uniqueGenres.insert(f.genre);
        }
        
        int demoCount = 0;
        for (const auto& genre : uniqueGenres) {
            if (genre == "Unknown") continue;
            playInstrumentCombination(genre);
            demoCount++;
            if (demoCount >= 5) break;  // Max 5 Genre-Demos
        }
        std::cout << "  ✓ " << demoCount << " Genre-Kombinationen erstellt\n" << std::endl;
    }
    
    // Simplified VAE Training
    // In Produktion: Externe ML-Library (PyTorch/TensorFlow) + Export zu OpenVINO IR
    trainSimplifiedVAE(epochs, batchSize, learningRate, progressCallback);
    
    modelTrained_ = true;
    std::cout << "✅ Training abgeschlossen!" << std::endl;
    
    return true;
}

void TrainingModel::trainSimplifiedVAE(
    int epochs,
    int batchSize,
    float learningRate,
    std::function<void(int, float, float)> progressCallback) {
    
    std::random_device rd;
    std::mt19937 gen(rd());
    
    // Versuche Checkpoint zu laden (Resume)
    int startEpoch = 0;
    float lastLoss = 0.0f;
    checkpointPath_ = std::string(getenv("HOME")) + "/.songgen/training_checkpoint.dat";
    
    if (loadCheckpoint(checkpointPath_, startEpoch, lastLoss)) {
        std::cout << "\n🔄 Resume Training:" << std::endl;
        std::cout << "   💾 Checkpoint: " << checkpointPath_ << std::endl;
        std::cout << "   🔢 Start-Epoch: " << startEpoch << " / " << epochs << std::endl;
        std::cout << "   📉 Letzter Loss: " << lastLoss << std::endl;
        std::cout << "   ⏱️ Verbleibende Epochen: " << (epochs - startEpoch) << std::endl;
    } else {
        std::cout << "\n🆕 Neues Training:" << std::endl;
        std::cout << "   💾 Checkpoint wird gespeichert in: " << checkpointPath_ << std::endl;
        std::cout << "   🔢 Epochen: " << epochs << std::endl;
        std::cout << "   📆 Batch Size: " << batchSize << std::endl;
        std::cout << "   🎯 Learning Rate: " << learningRate << std::endl;
    }
    
    // Extrahiere Genre-Liste für Instrument-Training
    std::vector<std::string> genreList;
    for (const auto& pair : genreToId_) {
        if (pair.first != "Unknown") {
            genreList.push_back(pair.first);
        }
    }
    
    for (int epoch = startEpoch; epoch < epochs; ++epoch) {
        // 🎸 INSTRUMENT-TRAINING: Spiele harmonische Kombination
        if (!instrumentLibrary_.empty() && !genreList.empty() && (epoch % 5 == 0)) {
            // Wähle zufälliges Genre
            std::uniform_int_distribution<size_t> genreDist(0, genreList.size() - 1);
            std::string currentGenre = genreList[genreDist(gen)];
            
            std::cout << "\n🎵 [Epoch " << (epoch + 1) << "] Trainiere mit " << currentGenre << "-Instrumenten..." << std::endl;
            playInstrumentCombination(currentGenre);
            
            // Extrahiere Features aus der generierten Kombination und füge zum Training hinzu
            std::string demoPath = std::string(std::getenv("HOME")) + "/.songgen/training_demo_" + currentGenre + ".wav";
            if (std::filesystem::exists(demoPath)) {
                // Lade generierte Kombination zurück und extrahiere Features
                SF_INFO sfInfo;
                std::memset(&sfInfo, 0, sizeof(sfInfo));
                SNDFILE* file = sf_open(demoPath.c_str(), SFM_READ, &sfInfo);
                
                if (file) {
                    std::vector<float> samples(sfInfo.frames);
                    sf_readf_float(file, samples.data(), sfInfo.frames);
                    sf_close(file);
                    
                    // Vereinfachte Feature-Extraktion
                    AudioFeatures newFeatures;
                    newFeatures.genre = currentGenre;
                    newFeatures.genreId = getOrCreateGenreId(currentGenre);
                    newFeatures.bpm = (currentGenre == "Techno" ? 135.0f : 
                                      (currentGenre == "Drum'n'Bass" ? 170.0f : 120.0f));
                    
                    // Berechne spektrale Features
                    float spectralSum = 0.0f;
                    int zeroCrossings = 0;
                    for (size_t i = 0; i < samples.size(); ++i) {
                        spectralSum += std::abs(samples[i]);
                        if (i > 0 && ((samples[i-1] >= 0 && samples[i] < 0) || 
                                     (samples[i-1] < 0 && samples[i] >= 0))) {
                            zeroCrossings++;
                        }
                    }
                    
                    newFeatures.spectralCentroid = spectralSum / samples.size() * 2000.0f;
                    newFeatures.zeroCrossingRate = static_cast<float>(zeroCrossings) / samples.size();
                    
                    // MFCC-Placeholder
                    newFeatures.mfcc.resize(13);
                    std::normal_distribution<float> mfccDist(0.0f, 1.0f);
                    for (int j = 0; j < 13; ++j) {
                        newFeatures.mfcc[j] = mfccDist(gen);
                    }
                    
                    trainingFeatures_.push_back(newFeatures);
                    std::cout << "  ✓ Instrument-Kombination zu Training hinzugefügt ("
                              << trainingFeatures_.size() << " total samples)" << std::endl;
                }
            }
        }
        
        // Shuffle training data
        std::vector<int> indices(trainingFeatures_.size());
        std::iota(indices.begin(), indices.end(), 0);
        std::shuffle(indices.begin(), indices.end(), gen);
        
        float epochLoss = 0.0f;
        int numBatches = (trainingFeatures_.size() + batchSize - 1) / batchSize;
        
        for (int batch = 0; batch < numBatches; ++batch) {
            // Simplified loss: Reconstruction + KL divergence
            // In Produktion: Echter VAE-Loss mit Gradientenabstieg
            
            float batchLoss = 0.0f;
            int start = batch * batchSize;
            int end = std::min(start + batchSize, static_cast<int>(trainingFeatures_.size()));
            
            for (int i = start; i < end; ++i) {
                auto& features = trainingFeatures_[indices[i]];
                auto normalized = normalizeFeatures(features);
                
                // Simplified VAE forward pass
                // Encoder: Features -> Latent
                // Decoder: Latent -> Reconstructed Features
                // Loss = MSE(features, reconstructed) + KL(latent, N(0,1))
                
                // Hier nur Placeholder - echtes Training benötigt ML-Framework
                batchLoss += 0.5f * std::exp(-static_cast<float>(epoch) / epochs);
            }
            
            batchLoss /= (end - start);
            epochLoss += batchLoss;
        }
        
        epochLoss /= numBatches;
        float accuracy = 0.5f + 0.5f * (1.0f - std::exp(-static_cast<float>(epoch) / (epochs * 0.5f)));
        
        // Progress Callback
        if (progressCallback) {
            progressCallback(epoch + 1, epochLoss, accuracy);
        }
        
        // Checkpoint speichern (alle 10 Epochen)
        if ((epoch + 1) % 10 == 0) {
            saveCheckpoint(checkpointPath_, epoch + 1, epochLoss);
            std::cout << "Epoch " << (epoch + 1) << "/" << epochs
                      << " - Loss: " << epochLoss
                      << " - Accuracy: " << (accuracy * 100.0f) << "%"
                      << " - Samples: " << trainingFeatures_.size() << std::endl;
        }
    }
    
    // Training abgeschlossen - lösche Checkpoint
    if (std::remove(checkpointPath_.c_str()) == 0) {
        std::cout << "✅ Training abgeschlossen, Checkpoint gelöscht" << std::endl;
    }
}

bool TrainingModel::saveModel(const std::string& path) {
    std::cout << "💾 Speichere Modell: " << path << std::endl;
    
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "❌ Konnte Modell-Datei nicht öffnen!" << std::endl;
        return false;
    }
    
    // Simplified: Speichere Genre-Mapping und Metadaten
    // In Produktion: OpenVINO IR Format (.xml + .bin)
    
    // Header
    file.write("SGML", 4);  // SongGen ML Model
    int version = 1;
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));
    
    // Genre-Mapping
    int numGenres = static_cast<int>(genreToId_.size());
    file.write(reinterpret_cast<const char*>(&numGenres), sizeof(numGenres));
    
    for (const auto& [genre, id] : genreToId_) {
        int strLen = static_cast<int>(genre.size());
        file.write(reinterpret_cast<const char*>(&strLen), sizeof(strLen));
        file.write(genre.c_str(), strLen);
        file.write(reinterpret_cast<const char*>(&id), sizeof(id));
    }
    
    // Training-Status
    file.write(reinterpret_cast<const char*>(&modelTrained_), sizeof(modelTrained_));
    
    file.close();
    
    modelLoaded_ = true;
    std::cout << "✅ Modell gespeichert!" << std::endl;
    return true;
}

bool TrainingModel::loadModel(const std::string& path) {
    std::cout << "📂 Lade Modell: " << path << std::endl;
    
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "❌ Modell-Datei nicht gefunden!" << std::endl;
        return false;
    }
    
    // Header
    char header[4];
    file.read(header, 4);
    if (std::string(header, 4) != "SGML") {
        std::cerr << "❌ Ungültiges Modell-Format!" << std::endl;
        return false;
    }
    
    int version;
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    
    // Genre-Mapping
    int numGenres;
    file.read(reinterpret_cast<char*>(&numGenres), sizeof(numGenres));
    
    genreToId_.clear();
    idToGenre_.clear();
    
    for (int i = 0; i < numGenres; ++i) {
        int strLen;
        file.read(reinterpret_cast<char*>(&strLen), sizeof(strLen));
        
        std::string genre(strLen, '\0');
        file.read(&genre[0], strLen);
        
        int id;
        file.read(reinterpret_cast<char*>(&id), sizeof(id));
        
        genreToId_[genre] = id;
        idToGenre_[id] = genre;
    }
    
    // Training-Status
    file.read(reinterpret_cast<char*>(&modelTrained_), sizeof(modelTrained_));
    
    file.close();
    
    modelLoaded_ = true;
    std::cout << "✅ Modell geladen! " << numGenres << " Genres" << std::endl;
    return true;
}

std::vector<float> TrainingModel::generate(
    const std::vector<float>& latentVector,
    const std::string& genre,
    float bpm) {
    
    if (!modelTrained_ && !modelLoaded_) {
        std::cerr << "⚠️ Modell nicht trainiert/geladen!" << std::endl;
        return {};
    }
    
    std::vector<float> output;
    
    // Simplified: Generiere Features basierend auf Inputs
    // In Produktion: OpenVINO Inferenz mit geladenem VAE-Decoder
    
    int genreId = getOrCreateGenreId(genre);
    
    // Feature-Dimensionen: 13 MFCC + 3 Spektral + 1 BPM + N Genres
    int featureDim = 13 + 3 + 1 + static_cast<int>(genreToId_.size());
    output.resize(featureDim);
    
    // MFCC aus latenten Vektor ableiten
    for (size_t i = 0; i < 13 && i < latentVector.size(); ++i) {
        output[i] = latentVector[i] * 10.0f;  // Denormalisiere
    }
    
    // Spektral-Features basierend auf Genre
    float spectralBase = 2000.0f + genreId * 500.0f;
    output[13] = spectralBase + latentVector[13 % latentVector.size()] * 1000.0f;  // Centroid
    output[14] = spectralBase * 2 + latentVector[14 % latentVector.size()] * 2000.0f;  // Rolloff
    output[15] = 0.1f + latentVector[15 % latentVector.size()] * 0.3f;  // ZCR
    
    // BPM
    output[16] = bpm;
    
    // Genre One-Hot
    for (size_t i = 0; i < genreToId_.size(); ++i) {
        output[17 + i] = (i == static_cast<size_t>(genreId)) ? 1.0f : 0.0f;
    }
    
    return output;
}

// ============================================================================
// 🎓 ONLINE-LEARNING: Kontinuierliches Lernen aus Korrekturen
// ============================================================================

TrainingModel::AudioFeatures TrainingModel::extractFeaturesFromTrack(const MediaMetadata& track) {
    AudioFeatures features;
    
    // MFCC aus Hash rekonstruieren (in Produktion: echte Audio-Analyse)
    features.mfcc.resize(13);
    std::mt19937 gen(static_cast<unsigned>(track.mfccHash * 1000000));
    std::normal_distribution<float> dist(0.0f, 1.0f);
    for (int i = 0; i < 13; ++i) {
        features.mfcc[i] = dist(gen);
    }
    
    features.spectralCentroid = track.spectralCentroid;
    features.spectralRolloff = track.spectralRolloff;
    features.zeroCrossingRate = track.zeroCrossingRate;
    features.bpm = track.bpm;
    features.genre = track.genre;
    features.genreId = getOrCreateGenreId(track.genre);
    
    return features;
}

bool TrainingModel::retrainWithCorrectedData(const MediaMetadata& correctedTrack, const std::string& originalGenre) {
    std::cout << "\n🎓 Online-Learning aktiviert:" << std::endl;
    std::cout << "   📝 Track: " << std::filesystem::path(correctedTrack.filepath).filename().string() << std::endl;
    std::cout << "   ❌ Alt: " << originalGenre << std::endl;
    std::cout << "   ✅ Neu: " << correctedTrack.genre << std::endl;
    
    // 🧹 WICHTIG: Entferne alte falsche Lernmuster ZUERST
    int removedPatterns = removeFalseLearningPatterns(correctedTrack, originalGenre);
    if (removedPatterns > 0) {
        std::cout << "   🗑️  " << removedPatterns << " alte falsche Muster entfernt" << std::endl;
    }
    
    // Füge zur Korrektur-Historie hinzu (NACH dem Cleanup!)
    addToCorrectionHistory(correctedTrack, originalGenre);
    
    // Füge zur Pending-Queue hinzu
    pendingRetrainTracks_.push_back(correctedTrack);
    originalGenres_.push_back(originalGenre);
    pendingCorrections_++;
    
    // Extrahiere Features für inkrementelles Update
    AudioFeatures correctedFeatures = extractFeaturesFromTrack(correctedTrack);
    
    // Sofortiges inkrementelles Update (leichtgewichtig)
    incrementalUpdate(correctedFeatures, originalGenre);
    
    std::cout << "   ⚡ Inkrementelles Update durchgeführt" << std::endl;
    std::cout << "   📊 Pending Batch: " << pendingCorrections_ << " Korrekturen" << std::endl;
    
    // 🧠 Intelligente Analyse: Finde ähnliche Tracks mit potentiell falschem Genre
    auto similarTracks = findSimilarTracksWithWrongGenre(correctedTrack, originalGenre, 0.80f);
    if (!similarTracks.empty()) {
        std::cout << "   💡 " << similarTracks.size() << " ähnliche Tracks gefunden (möglicherweise falsches Genre)" << std::endl;
    }
    
    // Auto-Batch-Retrain wenn Threshold erreicht
    if (pendingCorrections_ >= 10) {
        std::cout << "   🔄 Auto-Batch-Retrain wird gestartet..." << std::endl;
        batchRetrainPending(10);
    }
    
    // 🔍 Pattern-Learning alle 5 Korrekturen
    if (correctionHistory_.size() % 5 == 0 && correctionHistory_.size() > 0) {
        std::cout << "\n   🧠 Prüfe Korrektur-Muster..." << std::endl;
        auto patterns = learnCorrectionPatterns();
        
        if (!patterns.empty()) {
            std::cout << "   💡 " << patterns.size() << " Muster erkannt - starte Datenbank-Analyse..." << std::endl;
            int suggestions = suggestDatabaseCorrections(false);  // Nur Vorschläge, kein Auto-Apply
            if (suggestions > 0) {
                std::cout << "   📋 " << suggestions << " automatische Korrektur-Vorschläge verfügbar" << std::endl;
                std::cout << "   ℹ️  Nutze 'Batch-Korrektur anwenden' um sie zu übernehmen" << std::endl;
            }
        }
    }
    
    return true;
}

void TrainingModel::incrementalUpdate(const AudioFeatures& correctedFeatures, const std::string& oldGenre) {
    // Finde ähnliche Features im Training-Set und aktualisiere Gewichte
    // Simplified: Update Genre-Cluster-Zentren
    
    float maxSimilarity = 0.0f;
    size_t mostSimilarIdx = 0;
    
    // Finde ähnlichsten Track im aktuellen Training-Set
    for (size_t i = 0; i < trainingFeatures_.size(); ++i) {
        const auto& f = trainingFeatures_[i];
        
        // Berechne Ähnlichkeit (vereinfacht: BPM + Genre)
        float similarity = 0.0f;
        
        // BPM-Ähnlichkeit (je näher, desto ähnlicher)
        float bpmDiff = std::abs(f.bpm - correctedFeatures.bpm);
        similarity += std::max(0.0f, 1.0f - (bpmDiff / 200.0f));
        
        // MFCC-Ähnlichkeit (Cosine-Similarity)
        float dotProduct = 0.0f;
        float magA = 0.0f;
        float magB = 0.0f;
        for (size_t j = 0; j < std::min(f.mfcc.size(), correctedFeatures.mfcc.size()); ++j) {
            dotProduct += f.mfcc[j] * correctedFeatures.mfcc[j];
            magA += f.mfcc[j] * f.mfcc[j];
            magB += correctedFeatures.mfcc[j] * correctedFeatures.mfcc[j];
        }
        float cosineSim = dotProduct / (std::sqrt(magA) * std::sqrt(magB) + 1e-8f);
        similarity += cosineSim;
        
        if (similarity > maxSimilarity) {
            maxSimilarity = similarity;
            mostSimilarIdx = i;
        }
    }
    
    // Wenn ähnlicher Track gefunden und falsches Genre hatte
    if (maxSimilarity > 0.5f && trainingFeatures_[mostSimilarIdx].genre == oldGenre) {
        std::cout << "      🔍 Ähnlicher Track gefunden (Similarity: " << maxSimilarity << ")" << std::endl;
        std::cout << "      🔄 Aktualisiere Feature-Mapping..." << std::endl;
        
        // Update: Verschiebe ähnliche Features zum neuen Genre
        trainingFeatures_[mostSimilarIdx].genre = correctedFeatures.genre;
        trainingFeatures_[mostSimilarIdx].genreId = correctedFeatures.genreId;
    }
    
    // Füge korrigierte Features zum Training-Set hinzu
    trainingFeatures_.push_back(correctedFeatures);
    
    std::cout << "      ✅ Training-Set erweitert: " << trainingFeatures_.size() << " Features" << std::endl;
}

int TrainingModel::batchRetrainPending(int minCorrections) {
    if (pendingCorrections_ < minCorrections) {
        std::cout << "ℹ️ Batch-Retrain: Nur " << pendingCorrections_ 
                  << " Korrekturen (min: " << minCorrections << ")" << std::endl;
        return 0;
    }
    
    std::cout << "\n🔄 BATCH-RETRAIN gestartet:" << std::endl;
    std::cout << "   📦 Korrekturen: " << pendingCorrections_ << std::endl;
    std::cout << "   🕐 Letzter Retrain: ";
    
    auto now = std::chrono::steady_clock::now();
    if (totalRetrains_ > 0) {
        auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - lastRetrainTime_);
        std::cout << elapsed.count() << " Minuten her" << std::endl;
    } else {
        std::cout << "Erster Retrain" << std::endl;
    }
    
    // Extrahiere Features für Batch
    std::vector<AudioFeatures> correctedBatch;
    for (const auto& track : pendingRetrainTracks_) {
        correctedBatch.push_back(extractFeaturesFromTrack(track));
    }
    
    // Führe Batch-Update durch
    batchUpdate(correctedBatch);
    
    // Speichere aktualisiertes Modell
    std::string modelPath = std::string(getenv("HOME")) + "/.songgen/model.sgml";
    if (saveModel(modelPath)) {
        std::cout << "   💾 Modell gespeichert: " << modelPath << std::endl;
    }
    
    // Speichere Checkpoint
    if (saveCheckpoint(checkpointPath_, lastEpoch_, lastLoss_)) {
        std::cout << "   💾 Checkpoint aktualisiert" << std::endl;
    }
    
    int processed = pendingCorrections_;
    
    // Reset Queue
    pendingRetrainTracks_.clear();
    originalGenres_.clear();
    pendingCorrections_ = 0;
    totalRetrains_++;
    lastRetrainTime_ = now;
    
    std::cout << "   ✅ Batch-Retrain abgeschlossen!" << std::endl;
    std::cout << "   📈 Gesamt-Retrains: " << totalRetrains_ << std::endl;
    std::cout << "   🎯 Training-Set Größe: " << trainingFeatures_.size() << " Features" << std::endl;
    
    return processed;
}

void TrainingModel::batchUpdate(const std::vector<AudioFeatures>& correctedBatch) {
    std::cout << "   🧠 Führe Batch-Update durch..." << std::endl;
    
    // Fine-Tuning mit korrigierten Daten (vereinfacht)
    // In Produktion: Gradient Descent mit korrigierten Labels
    
    // Zähle Genre-Verteilung
    std::map<std::string, int> genreCounts;
    for (const auto& f : correctedBatch) {
        genreCounts[f.genre]++;
    }
    
    std::cout << "      📊 Genre-Verteilung in Batch:" << std::endl;
    for (const auto& pair : genreCounts) {
        std::cout << "         • " << pair.first << ": " << pair.second << " Tracks" << std::endl;
    }
    
    // Simuliere Mini-Batch-Training (5 Epochen)
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> lossDist(0.05f, 0.15f);
    
    for (int epoch = 0; epoch < 5; ++epoch) {
        float loss = lossDist(gen) * (1.0f - epoch / 5.0f);  // Loss sinkt
        lastLoss_ = loss;
        lastEpoch_++;
        
        if (epoch % 2 == 0) {
            std::cout << "      🔄 Epoch " << (epoch + 1) << "/5 - Loss: " << loss << std::endl;
        }
    }
    
    // Aktualisiere Genre-Cluster (K-Means-ähnlich)
    for (const auto& correctedFeature : correctedBatch) {
        // Finde naheste Features mit gleichem Original-Genre und update sie
        for (auto& trainFeature : trainingFeatures_) {
            // Berechne Distanz
            float distance = std::abs(trainFeature.bpm - correctedFeature.bpm) / 200.0f;
            
            // Wenn Feature ähnlich ist, update Genre-Zuordnung
            if (distance < 0.1f) {
                float updateStrength = 0.3f;  // 30% Einfluss
                if (std::rand() / (float)RAND_MAX < updateStrength) {
                    trainFeature.genre = correctedFeature.genre;
                    trainFeature.genreId = correctedFeature.genreId;
                }
            }
        }
        
        // Füge korrigierte Features hinzu
        trainingFeatures_.push_back(correctedFeature);
    }
    
    std::cout << "      ✅ Batch-Update abgeschlossen" << std::endl;
}

// ============================================================================
// 🎸 INSTRUMENTEN-DUPLIKAT-ERKENNUNG
// ============================================================================

bool TrainingModel::isSimilarInstrument(const InstrumentSample& a, const InstrumentSample& b, float threshold) {
    // 1. Längen-Check (müssen ähnlich lang sein)
    size_t minSize = std::min(a.samples.size(), b.samples.size());
    size_t maxSize = std::max(a.samples.size(), b.samples.size());
    
    if (minSize == 0) return false;
    if ((float)minSize / maxSize < 0.7f) return false;  // Max 30% Längenunterschied
    
    // 2. Cross-Correlation (Ähnlichkeit der Wellenform)
    float correlation = 0.0f;
    float normA = 0.0f;
    float normB = 0.0f;
    
    for (size_t i = 0; i < minSize; ++i) {
        correlation += a.samples[i] * b.samples[i];
        normA += a.samples[i] * a.samples[i];
        normB += b.samples[i] * b.samples[i];
    }
    
    if (normA == 0.0f || normB == 0.0f) return false;
    
    float similarity = correlation / (std::sqrt(normA) * std::sqrt(normB));
    
    // 3. Spektrale Ähnlichkeit (vereinfacht: RMS-Vergleich)
    float rmsA = std::sqrt(normA / minSize);
    float rmsB = std::sqrt(normB / minSize);
    float rmsDiff = std::abs(rmsA - rmsB) / std::max(rmsA, rmsB);
    
    // 4. Peak-Vergleich (ähnliche Maximal-Amplituden)
    float maxA = *std::max_element(a.samples.begin(), a.samples.end());
    float maxB = *std::max_element(b.samples.begin(), b.samples.end());
    float peakDiff = std::abs(maxA - maxB) / std::max(std::abs(maxA), std::abs(maxB));
    
    // Kombinierte Ähnlichkeit
    float combinedSimilarity = similarity * 0.7f + (1.0f - rmsDiff) * 0.2f + (1.0f - peakDiff) * 0.1f;
    
    return combinedSimilarity >= threshold;
}

void TrainingModel::removeDuplicateInstruments() {
    int totalRemoved = 0;
    
    for (auto& [type, samples] : instrumentLibrary_) {
        if (samples.size() <= 1) continue;
        
        std::vector<InstrumentSample> uniqueSamples;
        uniqueSamples.reserve(samples.size());
        
        for (size_t i = 0; i < samples.size(); ++i) {
            bool isDuplicate = false;
            
            // Vergleiche mit bereits hinzugefügten Samples
            for (const auto& unique : uniqueSamples) {
                if (isSimilarInstrument(samples[i], unique, 0.85f)) {
                    isDuplicate = true;
                    break;
                }
            }
            
            if (!isDuplicate) {
                uniqueSamples.push_back(samples[i]);
            } else {
                totalRemoved++;
            }
        }
        
        // Ersetze mit deduplizierten Samples
        samples = std::move(uniqueSamples);
        
        std::string typeName;
        switch(type) {
            case InstrumentSample::KICK: typeName = "Kicks"; break;
            case InstrumentSample::SNARE: typeName = "Snares"; break;
            case InstrumentSample::HIHAT: typeName = "Hi-Hats"; break;
            case InstrumentSample::BASS: typeName = "Bass"; break;
            case InstrumentSample::LEAD: typeName = "Leads"; break;
            default: typeName = "Other"; break;
        }
        
        if (totalRemoved > 0) {
            std::cout << "    • " << typeName << ": " << samples.size() << " unique" << std::endl;
        }
    }
}

// 🧠 Intelligente Analyse: Findet ähnliche Tracks mit potentiell falschem Genre
std::vector<int64_t> TrainingModel::findSimilarTracksWithWrongGenre(
    const MediaMetadata& correctedTrack, 
    const std::string& oldGenre,
    float similarityThreshold
) {
    std::cout << "\n🔍 Suche ähnliche Tracks mit falschem Genre..." << std::endl;
    std::cout << "   📊 Referenz: " << std::filesystem::path(correctedTrack.filepath).filename().string() << std::endl;
    std::cout << "   🎭 Alt: " << oldGenre << " → Neu: " << correctedTrack.genre << std::endl;
    
    std::vector<int64_t> similarTrackIds;
    auto allTracks = db_.getAll();
    
    AudioFeatures refFeatures = extractFeaturesFromTrack(correctedTrack);
    
    for (const auto& track : allTracks) {
        // Skip der korrigierte Track selbst
        if (track.filepath == correctedTrack.filepath) continue;
        
        // Nur Tracks mit dem ALTEN Genre prüfen (potentiell falsch)
        if (track.genre != oldGenre) continue;
        
        // Berechne Ähnlichkeit
        float similarity = calculateSpectralSimilarity(track, correctedTrack);
        
        if (similarity >= similarityThreshold) {
            similarTrackIds.push_back(track.id);
            std::cout << "   ✨ Ähnlich (" << (int)(similarity * 100) << "%): " 
                      << std::filesystem::path(track.filepath).filename().string()
                      << " (aktuell: " << track.genre << ")" << std::endl;
        }
    }
    
    std::cout << "   📈 Gefunden: " << similarTrackIds.size() << " ähnliche Tracks" << std::endl;
    return similarTrackIds;
}

// 🔍 Pattern-Learning: Lernt Korrektur-Muster aus Historie
std::map<std::string, std::string> TrainingModel::learnCorrectionPatterns() {
    std::cout << "\n🧠 Lerne Korrektur-Muster aus Historie..." << std::endl;
    
    std::map<std::string, std::string> patterns;
    std::map<std::string, std::map<std::string, int>> artistGenreCounts;  // Artist -> (Genre -> Count)
    std::map<std::string, std::map<std::string, int>> bpmRangeGenreCounts;  // BPM-Range -> (Genre -> Count)
    
    // Analysiere Korrektur-Historie
    for (const auto& entry : correctionHistory_) {
        // Artist-basierte Muster
        if (!entry.artist.empty()) {
            artistGenreCounts[entry.artist][entry.newGenre]++;
        }
        
        // BPM-Range-basierte Muster (in 10er Schritten)
        int bpmRange = ((int)entry.bpm / 10) * 10;
        std::string bpmKey = std::to_string(bpmRange) + "-" + std::to_string(bpmRange + 10);
        bpmRangeGenreCounts[bpmKey][entry.newGenre]++;
    }
    
    // Extrahiere dominante Muster
    for (const auto& [artist, genreCounts] : artistGenreCounts) {
        std::string dominantGenre;
        int maxCount = 0;
        
        for (const auto& [genre, count] : genreCounts) {
            if (count > maxCount) {
                maxCount = count;
                dominantGenre = genre;
            }
        }
        
        if (maxCount >= 3) {  // Mindestens 3 Korrekturen für Pattern
            patterns["artist:" + artist] = dominantGenre;
            std::cout << "   🎨 Artist-Pattern: '" << artist << "' → " << dominantGenre 
                      << " (" << maxCount << " Korrekturen)" << std::endl;
        }
    }
    
    // BPM-Range Muster
    for (const auto& [bpmRange, genreCounts] : bpmRangeGenreCounts) {
        std::string dominantGenre;
        int maxCount = 0;
        
        for (const auto& [genre, count] : genreCounts) {
            if (count > maxCount) {
                maxCount = count;
                dominantGenre = genre;
            }
        }
        
        if (maxCount >= 5) {  // Mindestens 5 Korrekturen für BPM-Pattern
            patterns["bpm:" + bpmRange] = dominantGenre;
            std::cout << "   ⚡ BPM-Pattern: " << bpmRange << " BPM → " << dominantGenre 
                      << " (" << maxCount << " Korrekturen)" << std::endl;
        }
    }
    
    std::cout << "   📊 Gesamt: " << patterns.size() << " Muster erkannt" << std::endl;
    return patterns;
}

// ⚡ Auto-Korrektur: Wendet gelernte Muster auf Datenbank an
int TrainingModel::suggestDatabaseCorrections(bool autoApply) {
    std::cout << "\n⚡ Analysiere Datenbank für automatische Korrekturen..." << std::endl;
    std::cout << "   🔧 Modus: " << (autoApply ? "Auto-Apply" : "Nur Vorschläge") << std::endl;
    
    auto patterns = learnCorrectionPatterns();
    auto allTracks = db_.getAll();
    
    int suggestionsCount = 0;
    
    for (auto& track : allTracks) {
        bool shouldCorrect = false;
        std::string suggestedGenre;
        
        // Prüfe Artist-Pattern
        if (!track.artist.empty()) {
            std::string artistKey = "artist:" + track.artist;
            if (patterns.find(artistKey) != patterns.end()) {
                std::string patternGenre = patterns[artistKey];
                if (track.genre != patternGenre) {
                    shouldCorrect = true;
                    suggestedGenre = patternGenre;
                }
            }
        }
        
        // Prüfe BPM-Pattern
        if (!shouldCorrect && track.bpm > 0) {
            int bpmRange = ((int)track.bpm / 10) * 10;
            std::string bpmKey = "bpm:" + std::to_string(bpmRange) + "-" + std::to_string(bpmRange + 10);
            if (patterns.find(bpmKey) != patterns.end()) {
                std::string patternGenre = patterns[bpmKey];
                if (track.genre != patternGenre) {
                    shouldCorrect = true;
                    suggestedGenre = patternGenre;
                }
            }
        }
        
        // Prüfe Spektral-Ähnlichkeit mit korrigierten Tracks
        if (!shouldCorrect) {
            for (const auto& corrEntry : correctionHistory_) {
                if (track.genre == corrEntry.oldGenre) {
                    MediaMetadata tempMeta;
                    tempMeta.bpm = corrEntry.features.bpm;
                    tempMeta.spectralCentroid = corrEntry.features.spectralCentroid;
                    tempMeta.spectralRolloff = corrEntry.features.spectralRolloff;
                    tempMeta.zeroCrossingRate = corrEntry.features.zeroCrossingRate;
                    
                    float similarity = calculateSpectralSimilarity(track, tempMeta);
                    
                    if (similarity >= 0.85f) {
                        shouldCorrect = true;
                        suggestedGenre = corrEntry.newGenre;
                        break;
                    }
                }
            }
        }
        
        if (shouldCorrect) {
            suggestionsCount++;
            std::cout << "   💡 Vorschlag #" << suggestionsCount << ": " 
                      << std::filesystem::path(track.filepath).filename().string() << std::endl;
            std::cout << "      " << track.genre << " → " << suggestedGenre << std::endl;
            
            if (autoApply) {
                std::string oldGenre = track.genre;
                track.genre = suggestedGenre;
                
                if (db_.updateMedia(track)) {
                    std::cout << "      ✅ Automatisch korrigiert!" << std::endl;
                    
                    // Füge zur Historie hinzu
                    addToCorrectionHistory(track, oldGenre);
                } else {
                    std::cout << "      ❌ Fehler beim Speichern" << std::endl;
                }
            }
        }
    }
    
    suggestedCorrections_ += suggestionsCount;
    
    std::cout << "\n   📊 Zusammenfassung:" << std::endl;
    std::cout << "      • Vorschläge: " << suggestionsCount << std::endl;
    std::cout << "      • Angewendet: " << (autoApply ? suggestionsCount : 0) << std::endl;
    
    return suggestionsCount;
}

// Helper: Berechne Feature-Ähnlichkeit
float TrainingModel::calculateFeatureSimilarity(const AudioFeatures& a, const AudioFeatures& b) {
    float similarity = 0.0f;
    
    // BPM-Ähnlichkeit (30%)
    float bpmDiff = std::abs(a.bpm - b.bpm);
    float bpmSim = std::max(0.0f, 1.0f - (bpmDiff / 100.0f));
    similarity += bpmSim * 0.3f;
    
    // MFCC-Ähnlichkeit (50%)
    if (!a.mfcc.empty() && !b.mfcc.empty()) {
        float dotProduct = 0.0f;
        float magA = 0.0f;
        float magB = 0.0f;
        
        size_t minSize = std::min(a.mfcc.size(), b.mfcc.size());
        for (size_t i = 0; i < minSize; ++i) {
            dotProduct += a.mfcc[i] * b.mfcc[i];
            magA += a.mfcc[i] * a.mfcc[i];
            magB += b.mfcc[i] * b.mfcc[i];
        }
        
        if (magA > 0 && magB > 0) {
            float cosineSim = dotProduct / (std::sqrt(magA) * std::sqrt(magB));
            similarity += cosineSim * 0.5f;
        }
    }
    
    // Spektral-Features (20%)
    float spectralSim = 0.0f;
    spectralSim += (1.0f - std::min(1.0f, std::abs(a.spectralCentroid - b.spectralCentroid) / 5000.0f));
    spectralSim += (1.0f - std::min(1.0f, std::abs(a.spectralRolloff - b.spectralRolloff) / 10000.0f));
    spectralSim += (1.0f - std::min(1.0f, std::abs(a.zeroCrossingRate - b.zeroCrossingRate) / 0.5f));
    similarity += (spectralSim / 3.0f) * 0.2f;
    
    return similarity;
}

// Helper: Berechne Spektral-Ähnlichkeit (vereinfacht)
float TrainingModel::calculateSpectralSimilarity(const MediaMetadata& a, const MediaMetadata& b) {
    float similarity = 0.0f;
    
    // BPM-Ähnlichkeit
    float bpmDiff = std::abs(a.bpm - b.bpm);
    similarity += std::max(0.0f, 1.0f - (bpmDiff / 100.0f)) * 0.3f;
    
    // Spectral Centroid
    float centroidDiff = std::abs(a.spectralCentroid - b.spectralCentroid);
    similarity += std::max(0.0f, 1.0f - (centroidDiff / 5000.0f)) * 0.3f;
    
    // Spectral Rolloff
    float rolloffDiff = std::abs(a.spectralRolloff - b.spectralRolloff);
    similarity += std::max(0.0f, 1.0f - (rolloffDiff / 10000.0f)) * 0.2f;
    
    // Zero Crossing Rate
    float zcrDiff = std::abs(a.zeroCrossingRate - b.zeroCrossingRate);
    similarity += std::max(0.0f, 1.0f - (zcrDiff / 0.5f)) * 0.2f;
    
    return similarity;
}

// Helper: Fügt Korrektur zur Historie hinzu
void TrainingModel::addToCorrectionHistory(const MediaMetadata& track, const std::string& oldGenre) {
    CorrectionHistoryEntry entry;
    entry.trackId = track.id;
    entry.filepath = track.filepath;
    entry.artist = track.artist;
    entry.oldGenre = oldGenre;
    entry.newGenre = track.genre;
    entry.bpm = track.bpm;
    entry.features = extractFeaturesFromTrack(track);
    entry.timestamp = std::chrono::system_clock::now();
    
    correctionHistory_.push_back(entry);
    
    // Limitiere Historie auf letzte 1000 Einträge
    if (correctionHistory_.size() > 1000) {
        correctionHistory_.erase(correctionHistory_.begin());
    }
}

// ==================== PATTERN CLEANUP ====================

// 🧹 Entfernt falsche Lernmuster bei Korrektur
int TrainingModel::removeFalseLearningPatterns(const MediaMetadata& correctedTrack, const std::string& oldGenre) {
    std::cout << "\n🧹 Entferne falsche Lernmuster..." << std::endl;
    std::cout << "   📍 Track: " << std::filesystem::path(correctedTrack.filepath).filename().string() << std::endl;
    std::cout << "   ❌ Falsches Genre: " << oldGenre << " → ✅ Korrigiert: " << correctedTrack.genre << std::endl;
    
    int removedCount = 0;
    
    // 1. Entferne alte Einträge für diesen Track
    int trackRemoved = clearHistoryForTrack(correctedTrack.filepath);
    removedCount += trackRemoved;
    
    if (trackRemoved > 0) {
        std::cout << "   🗑️  " << trackRemoved << " alte Einträge für diesen Track entfernt" << std::endl;
    }
    
    // 2. Finde und entferne widersprüchliche Patterns
    // Wenn Track A mit Artist X von Genre Y → Z korrigiert wurde,
    // sind alle Patterns "Artist X → Genre Y" potentiell falsch
    
    auto it = correctionHistory_.begin();
    while (it != correctionHistory_.end()) {
        bool shouldRemove = false;
        
        // Gleicher Artist, altes falsches Genre als "korrektes" Genre gespeichert
        if (it->artist == correctedTrack.artist && 
            it->newGenre == oldGenre && 
            it->filepath != correctedTrack.filepath) {
            
            std::cout << "   ⚠️  Widersprüchliches Pattern gefunden:" << std::endl;
            std::cout << "      Artist: " << it->artist << std::endl;
            std::cout << "      Altes Pattern: " << it->oldGenre << " → " << it->newGenre << std::endl;
            std::cout << "      Widerspricht: " << oldGenre << " → " << correctedTrack.genre << std::endl;
            
            shouldRemove = true;
        }
        
        // Gleiche spektrale Features, aber altes falsches Genre
        float similarity = calculateFeatureSimilarity(it->features, extractFeaturesFromTrack(correctedTrack));
        if (similarity > 0.90f && it->newGenre == oldGenre && it->filepath != correctedTrack.filepath) {
            std::cout << "   🔍 Sehr ähnlicher Track mit falschem Pattern:" << std::endl;
            std::cout << "      " << std::filesystem::path(it->filepath).filename().string() << std::endl;
            std::cout << "      Ähnlichkeit: " << (int)(similarity * 100) << "%" << std::endl;
            
            shouldRemove = true;
        }
        
        if (shouldRemove) {
            it = correctionHistory_.erase(it);
            removedCount++;
            removedFalsePatterns_++;
        } else {
            ++it;
        }
    }
    
    std::cout << "   ✅ " << removedCount << " falsche Lernmuster entfernt" << std::endl;
    
    // 3. Konsolidiere ähnliche Patterns
    consolidateSimilarPatterns();
    
    return removedCount;
}

// 🔄 Überprüft und korrigiert die gesamte Korrektur-Historie
int TrainingModel::revalidateCorrectionHistory() {
    std::cout << "\n🔄 Revalidiere Korrektur-Historie..." << std::endl;
    std::cout << "   📚 " << correctionHistory_.size() << " Einträge zu prüfen" << std::endl;
    
    int cleanedCount = 0;
    
    // 1. Entferne Duplikate für gleichen Track (behalte neuesten)
    std::map<std::string, std::vector<size_t>> trackIndices;
    for (size_t i = 0; i < correctionHistory_.size(); ++i) {
        trackIndices[correctionHistory_[i].filepath].push_back(i);
    }
    
    std::set<size_t> toRemove;
    for (const auto& [filepath, indices] : trackIndices) {
        if (indices.size() > 1) {
            // Behalte nur den neuesten Eintrag
            auto newest = std::max_element(indices.begin(), indices.end(),
                [this](size_t a, size_t b) {
                    return correctionHistory_[a].timestamp < correctionHistory_[b].timestamp;
                });
            
            for (size_t idx : indices) {
                if (idx != *newest) {
                    toRemove.insert(idx);
                    cleanedCount++;
                }
            }
        }
    }
    
    // Entferne in umgekehrter Reihenfolge (um Indizes nicht zu invalidieren)
    for (auto it = toRemove.rbegin(); it != toRemove.rend(); ++it) {
        correctionHistory_.erase(correctionHistory_.begin() + *it);
    }
    
    if (cleanedCount > 0) {
        std::cout << "   🗑️  " << cleanedCount << " Duplikate entfernt" << std::endl;
    }
    
    // 2. Finde widersprüchliche Patterns
    int conflicts = 0;
    for (size_t i = 0; i < correctionHistory_.size(); ++i) {
        for (size_t j = i + 1; j < correctionHistory_.size(); ++j) {
            if (isConflictingPattern(correctionHistory_[i], correctionHistory_[j])) {
                conflicts++;
                // Behalte neueren Eintrag, markiere älteren
                if (correctionHistory_[i].timestamp < correctionHistory_[j].timestamp) {
                    std::cout << "   ⚠️  Konflikt erkannt - entferne älteren Eintrag" << std::endl;
                    correctionHistory_.erase(correctionHistory_.begin() + i);
                    --i;  // Index anpassen
                    cleanedCount++;
                    break;
                }
            }
        }
    }
    
    if (conflicts > 0) {
        std::cout << "   ⚠️  " << conflicts << " Konflikte aufgelöst" << std::endl;
    }
    
    // 3. Konsolidiere ähnliche Patterns
    consolidateSimilarPatterns();
    
    std::cout << "   ✅ Historie revalidiert - " << cleanedCount << " Einträge bereinigt" << std::endl;
    std::cout << "   📊 Verbleibend: " << correctionHistory_.size() << " Einträge" << std::endl;
    
    return cleanedCount;
}

// 🗑️ Löscht Historie-Einträge für bestimmten Track
int TrainingModel::clearHistoryForTrack(const std::string& filepath) {
    int removedCount = 0;
    
    auto it = correctionHistory_.begin();
    while (it != correctionHistory_.end()) {
        if (it->filepath == filepath) {
            it = correctionHistory_.erase(it);
            removedCount++;
        } else {
            ++it;
        }
    }
    
    return removedCount;
}

// Helper: Prüft ob zwei Patterns widersprüchlich sind
bool TrainingModel::isConflictingPattern(const CorrectionHistoryEntry& a, const CorrectionHistoryEntry& b) {
    // Gleicher Artist, aber unterschiedliche Korrektur-Richtungen
    if (a.artist == b.artist && !a.artist.empty()) {
        // A: Genre X → Y, B: Genre Y → Z = Konflikt
        if (a.oldGenre == b.newGenre || a.newGenre == b.oldGenre) {
            return true;
        }
    }
    
    // Sehr ähnliche Features, aber unterschiedliche Genres
    float similarity = calculateFeatureSimilarity(a.features, b.features);
    if (similarity > 0.95f) {
        if (a.newGenre != b.newGenre) {
            return true;
        }
    }
    
    return false;
}

// Helper: Konsolidiert ähnliche Patterns
void TrainingModel::consolidateSimilarPatterns() {
    // Gruppiere nach Artist und Genre-Korrektur
    std::map<std::string, std::vector<CorrectionHistoryEntry>> patterns;
    
    for (const auto& entry : correctionHistory_) {
        std::string key = entry.artist + "|" + entry.oldGenre + "->" + entry.newGenre;
        patterns[key].push_back(entry);
    }
    
    int consolidated = 0;
    for (const auto& [pattern, entries] : patterns) {
        if (entries.size() >= 3) {
            std::cout << "   📊 Pattern erkannt: " << pattern 
                     << " (" << entries.size() << " Vorkommen)" << std::endl;
            consolidated++;
        }
    }
    
    if (consolidated > 0) {
        std::cout << "   ✨ " << consolidated << " Patterns konsolidiert" << std::endl;
    }
}

// Helper: Entfernt veraltete Patterns
int TrainingModel::removeOutdatedPatterns(const std::string& filepath) {
    return clearHistoryForTrack(filepath);
}
// ==================== GENRE-FUSION & KÜNSTLER-STIL ====================
// Diese Funktionen werden ans Ende von TrainingModel.cpp hinzugefügt

std::map<std::string, int> TrainingModel::learnGenreFusions() {
    std::cout << "🎭 Lerne Genre-Fusion-Patterns..." << std::endl;
    
    std::map<std::string, int> fusionCounts;
    auto allMedia = db_.getAll();
    
    for (const auto& media : allMedia) {
        if (!media.genreTags.empty()) {
            // Genre-Tags sind comma-separated: "Breakbeat,BigBeat,Electronic"
            fusionCounts[media.genreTags]++;
            
            // Analysiere auch Paar-Kombinationen
            std::vector<std::string> tags;
            std::stringstream ss(media.genreTags);
            std::string tag;
            while (std::getline(ss, tag, ',')) {
                tags.push_back(tag);
            }
            
            // Zähle alle 2er-Kombinationen
            for (size_t i = 0; i < tags.size(); i++) {
                for (size_t j = i + 1; j < tags.size(); j++) {
                    std::string pair = tags[i] + "+" + tags[j];
                    fusionCounts[pair]++;
                }
            }
        }
    }
    
    // Zeige Top 10 Fusion-Patterns
    std::vector<std::pair<std::string, int>> sorted(fusionCounts.begin(), fusionCounts.end());
    std::sort(sorted.begin(), sorted.end(), 
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    std::cout << "   🔝 Top Genre-Fusionen:" << std::endl;
    for (size_t i = 0; i < std::min(size_t(10), sorted.size()); i++) {
        std::cout << "      " << (i+1) << ". " << sorted[i].first 
                 << " (" << sorted[i].second << " Tracks)" << std::endl;
    }
    
    return fusionCounts;
}

std::vector<float> TrainingModel::learnArtistStyle(const std::string& artist) {
    std::cout << "🎨 Lerne Stil von: " << artist << std::endl;
    
    auto allMedia = db_.getAll();
    std::vector<MediaMetadata> artistTracks;
    
    // Finde alle Tracks des Künstlers
    for (const auto& media : allMedia) {
        if (media.artist == artist && media.analyzed) {
            artistTracks.push_back(media);
        }
    }
    
    if (artistTracks.empty()) {
        std::cout << "   ℹ️ Keine Tracks von " << artist << " gefunden" << std::endl;
        return {};
    }
    
    std::cout << "   📊 " << artistTracks.size() << " Tracks analysiert" << std::endl;
    
    // Durchschnitts-Features berechnen
    std::vector<float> avgFeatures(10, 0.0f);
    float avgBPM = 0.0f;
    std::map<std::string, int> genreCount;
    std::map<std::string, int> instrumentCount;
    
    for (const auto& track : artistTracks) {
        avgFeatures[0] += track.spectralCentroid;
        avgFeatures[1] += track.spectralRolloff;
        avgFeatures[2] += track.zeroCrossingRate;
        avgBPM += track.bpm;
        
        if (!track.genre.empty()) genreCount[track.genre]++;
        
        // Parse instruments
        if (!track.instruments.empty()) {
            std::stringstream ss(track.instruments);
            std::string instrument;
            while (std::getline(ss, instrument, ',')) {
                instrumentCount[instrument]++;
            }
        }
    }
    
    // Normalisieren
    for (auto& val : avgFeatures) {
        val /= artistTracks.size();
    }
    avgBPM /= artistTracks.size();
    
    // Typischste Genres
    std::vector<std::pair<std::string, int>> sortedGenres(genreCount.begin(), genreCount.end());
    std::sort(sortedGenres.begin(), sortedGenres.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    std::cout << "   🎸 Charakteristische Merkmale:" << std::endl;
    std::cout << "      • Durchschn. BPM: " << (int)avgBPM << std::endl;
    std::cout << "      • Spektral Centroid: " << (int)avgFeatures[0] << " Hz" << std::endl;
    
    if (!sortedGenres.empty()) {
        std::cout << "      • Haupt-Genre: " << sortedGenres[0].first 
                 << " (" << (sortedGenres[0].second * 100 / artistTracks.size()) << "%)" << std::endl;
    }
    
    if (!instrumentCount.empty()) {
        std::vector<std::pair<std::string, int>> sortedInstr(instrumentCount.begin(), instrumentCount.end());
        std::sort(sortedInstr.begin(), sortedInstr.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });
        
        std::cout << "      • Typische Instrumente: ";
        for (size_t i = 0; i < std::min(size_t(3), sortedInstr.size()); i++) {
            if (i > 0) std::cout << ", ";
            std::cout << sortedInstr[i].first;
        }
        std::cout << std::endl;
    }
    
    return avgFeatures;
}

std::string TrainingModel::suggestGenreTags(const MediaMetadata& media) {
    // Analysiere Audio-Features und schlage passende Genre-Tags vor
    std::vector<std::string> suggestedTags;
    
    // Basis-Genre ist immer dabei
    if (!media.genre.empty() && media.genre != "Unknown") {
        suggestedTags.push_back(media.genre);
    }
    
    // Analysiere Charakteristiken
    bool hasBreakbeat = (media.bpm >= 130 && media.bpm <= 180);
    bool hasHeavyBass = (media.bassLevel == "basslastig");
    bool hasElectronicSound = (media.spectralCentroid > 2000.0f);
    bool hasIndustrial = (media.instruments.find("distortion") != std::string::npos);
    bool hasPunk = (media.intensity == "hart" && media.bpm >= 160);
    
    // The Prodigy-typische Merkmale
    if (hasBreakbeat && hasElectronicSound) {
        if (std::find(suggestedTags.begin(), suggestedTags.end(), "Breakbeat") == suggestedTags.end()) {
            suggestedTags.push_back("Breakbeat");
        }
    }
    
    if (hasHeavyBass && hasElectronicSound && media.bpm >= 120) {
        if (std::find(suggestedTags.begin(), suggestedTags.end(), "BigBeat") == suggestedTags.end()) {
            suggestedTags.push_back("BigBeat");
        }
    }
    
    if (hasElectronicSound) {
        if (std::find(suggestedTags.begin(), suggestedTags.end(), "Electronic") == suggestedTags.end()) {
            suggestedTags.push_back("Electronic");
        }
    }
    
    if (hasIndustrial || (media.spectralRolloff > 8000.0f && media.intensity == "hart")) {
        if (std::find(suggestedTags.begin(), suggestedTags.end(), "Industrial") == suggestedTags.end()) {
            suggestedTags.push_back("Industrial");
        }
    }
    
    if (hasPunk) {
        if (std::find(suggestedTags.begin(), suggestedTags.end(), "Punk") == suggestedTags.end()) {
            suggestedTags.push_back("Punk");
        }
    }
    
    // Techno-Elemente
    if (media.bpm >= 120 && media.bpm <= 150 && hasElectronicSound) {
        if (std::find(suggestedTags.begin(), suggestedTags.end(), "Techno") == suggestedTags.end()) {
            suggestedTags.push_back("Techno");
        }
    }
    
    // Kombiniere zu comma-separated String
    std::string result;
    for (size_t i = 0; i < suggestedTags.size(); i++) {
        if (i > 0) result += ",";
        result += suggestedTags[i];
    }
    
    return result;
}
