#include "DataQualityAnalyzer.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>

namespace SongGen {

DataQualityAnalyzer::DataQualityAnalyzer(MediaDatabase& db) : db_(db) {
}

DataQualityMetrics DataQualityAnalyzer::analyze() {
    DataQualityMetrics metrics;
    
    std::cout << "🔍 Analyzing data quality...\n";
    
    auto allMedia = db_.getAll();
    int totalTracks = allMedia.size();
    
    if (totalTracks == 0) {
        metrics.overallQuality = 0.0f;
        metrics.recommendations.push_back("❌ CRITICAL: Keine Tracks in Datenbank!");
        metrics.missingData.push_back("Import HVSC oder lokale Musikdateien");
        return metrics;
    }
    
    // Analysiere Genre-Verteilung
    std::map<std::string, int> genreCount;
    for (const auto& track : allMedia) {
        if (!track.genre.empty()) {
            genreCount[track.genre]++;
        }
    }
    
    metrics.genreDistribution = genreCount;
    
    // Genre Coverage: Wie viele Genres haben mindestens 50 Tracks?
    int wellCoveredGenres = 0;
    int minTracksPerGenre = 50;
    for (const auto& [genre, count] : genreCount) {
        if (count >= minTracksPerGenre) {
            wellCoveredGenres++;
            metrics.wellRepresentedGenres.push_back(genre + " (" + std::to_string(count) + ")");
        } else if (count < 20) {
            metrics.underrepresentedGenres.push_back(genre + " (" + std::to_string(count) + ")");
        }
    }
    
    metrics.genreCoverage = genreCount.empty() ? 0.0f : 
        static_cast<float>(wellCoveredGenres) / genreCount.size();
    
    // Analysiere Tempo-Verteilung
    analyzeTempoDistribution(metrics);
    
    // Tempo Range Coverage
    int totalWithTempo = metrics.tracksSlow + metrics.tracksMedium + metrics.tracksFast;
    float slowRatio = totalWithTempo > 0 ? static_cast<float>(metrics.tracksSlow) / totalWithTempo : 0.0f;
    float mediumRatio = totalWithTempo > 0 ? static_cast<float>(metrics.tracksMedium) / totalWithTempo : 0.0f;
    float fastRatio = totalWithTempo > 0 ? static_cast<float>(metrics.tracksFast) / totalWithTempo : 0.0f;
    
    // Ideal wäre 33% / 33% / 33%
    float tempoBalance = 1.0f - (std::abs(slowRatio - 0.33f) + 
                                  std::abs(mediumRatio - 0.33f) + 
                                  std::abs(fastRatio - 0.33f));
    metrics.tempoRangeCoverage = std::max(0.0f, tempoBalance);
    
    // Feature-Vollständigkeit
    int tracksWithMFCC = 0;
    int tracksWithSpectral = 0;
    int tracksWithChords = 0;
    int tracksWithStructure = 0;
    
    for (const auto& track : allMedia) {
        // Prüfe ob Features existieren (simplified - real check würde in DB schauen)
        if (track.bpm > 0) tracksWithMFCC++;
        if (track.spectralCentroid > 0) tracksWithSpectral++;
        if (!track.genreTags.empty()) tracksWithChords++;
        if (track.duration > 0) tracksWithStructure++;
    }
    
    metrics.tracksWithMFCC = static_cast<float>(tracksWithMFCC) / totalTracks;
    metrics.tracksWithSpectral = static_cast<float>(tracksWithSpectral) / totalTracks;
    metrics.tracksWithChords = static_cast<float>(tracksWithChords) / totalTracks;
    metrics.tracksWithStructure = static_cast<float>(tracksWithStructure) / totalTracks;
    
    // Instrument Diversity (simplified)
    std::set<std::string> instruments;
    for (const auto& track : allMedia) {
        if (!track.genreTags.empty()) {
            // Parse instruments from tags
            instruments.insert(track.genreTags);
        }
    }
    metrics.instrumentDiversity = std::min(1.0f, static_cast<float>(instruments.size()) / 20.0f);
    
    // Overall Score berechnen
    metrics.overallQuality = calculateOverallScore(metrics);
    
    // Empfehlungen generieren
    metrics.recommendations = generateRecommendations(metrics);
    metrics.missingData = findCriticalGaps(metrics);
    
    std::cout << "✅ Quality Analysis complete: " << (metrics.overallQuality * 100) << "%\n";
    
    return metrics;
}

std::vector<GenreCompleteness> DataQualityAnalyzer::analyzeGenreCoverage() {
    std::vector<GenreCompleteness> results;
    
    auto allMedia = db_.getAll();
    std::map<std::string, GenreCompleteness> genreMap;
    
    // Empfohlene Mindestanzahl pro Genre
    std::map<std::string, int> minRecommended = {
        {"Electronic", 200}, {"Techno", 150}, {"House", 150}, {"Trap", 100},
        {"Hip-Hop", 100}, {"Pop", 150}, {"Rock", 150}, {"Metal", 100},
        {"Jazz", 100}, {"Classical", 100}, {"Ambient", 80}, {"Trance", 100}
    };
    
    for (const auto& track : allMedia) {
        if (track.genre.empty()) continue;
        
        auto& gc = genreMap[track.genre];
        gc.genre = track.genre;
        gc.trackCount++;
        gc.minRecommended = minRecommended.count(track.genre) ? 
                           minRecommended[track.genre] : 100;
        
        // Bewerte Feature-Qualität
        if (track.bpm > 0 && track.spectralCentroid > 0) {
            gc.tracksWithGoodFeatures++;
            gc.avgMFCCQuality += 1.0f;
        }
    }
    
    // Berechne Completeness
    for (auto& [genre, gc] : genreMap) {
        gc.completeness = std::min(1.0f, 
            static_cast<float>(gc.trackCount) / gc.minRecommended);
        
        if (gc.trackCount > 0) {
            gc.avgMFCCQuality /= gc.trackCount;
        }
        
        // Identifiziere fehlende Elemente
        if (gc.trackCount < gc.minRecommended) {
            gc.missingElements.push_back(
                std::to_string(gc.minRecommended - gc.trackCount) + " mehr Tracks benötigt"
            );
        }
        
        if (gc.tracksWithGoodFeatures < gc.trackCount * 0.8) {
            gc.missingElements.push_back(
                "Feature-Extraktion für " + 
                std::to_string(gc.trackCount - gc.tracksWithGoodFeatures) + " Tracks"
            );
        }
        
        results.push_back(gc);
    }
    
    // Sortiere nach Completeness (niedrigste zuerst = höchste Priorität)
    std::sort(results.begin(), results.end(),
        [](const GenreCompleteness& a, const GenreCompleteness& b) {
            return a.completeness < b.completeness;
        });
    
    return results;
}

void DataQualityAnalyzer::analyzePatterns(PatternCaptureEngine* patternEngine, 
                                         DataQualityMetrics& metrics) {
    if (!patternEngine) {
        metrics.rhythmPatternCount = 0.0f;
        metrics.melodyPatternCount = 0.0f;
        metrics.patternQualityAvg = 0.0f;
        return;
    }
    
    auto patterns = patternEngine->getAllPatterns();
    
    int rhythmCount = 0;
    int melodyCount = 0;
    float totalRating = 0.0f;
    
    for (const auto& pattern : patterns) {
        if (pattern.type == "rhythm") rhythmCount++;
        else if (pattern.type == "melody") melodyCount++;
        totalRating += pattern.userRating;
    }
    
    // Ideal: mindestens 50 Rhythm + 50 Melody Patterns
    metrics.rhythmPatternCount = std::min(1.0f, rhythmCount / 50.0f);
    metrics.melodyPatternCount = std::min(1.0f, melodyCount / 50.0f);
    metrics.patternQualityAvg = patterns.empty() ? 0.0f : totalRating / patterns.size();
}

std::vector<std::string> DataQualityAnalyzer::generateRecommendations(
    const DataQualityMetrics& metrics) {
    
    std::vector<std::string> recommendations;
    
    // Genre Coverage
    if (metrics.genreCoverage < 0.3f) {
        recommendations.push_back("🎭 WICHTIG: Mehr Genre-Vielfalt benötigt! Nur " + 
            std::to_string(static_cast<int>(metrics.genreCoverage * 100)) + "% der Genres gut abgedeckt");
    }
    
    // Tempo Balance
    if (metrics.tempoRangeCoverage < 0.5f) {
        recommendations.push_back("🎵 Tempo-Balance verbessern: ");
        if (metrics.tracksSlow < metrics.tracksMedium * 0.5f) {
            recommendations.push_back("  → Mehr langsame Tracks (< 90 BPM) hinzufügen");
        }
        if (metrics.tracksFast < metrics.tracksMedium * 0.5f) {
            recommendations.push_back("  → Mehr schnelle Tracks (> 130 BPM) hinzufügen");
        }
    }
    
    // Pattern Learning
    if (metrics.rhythmPatternCount < 0.3f) {
        recommendations.push_back("🥁 Mehr Rhythmus-Patterns lernen über Mikrofon-Eingabe (aktuell: " +
            std::to_string(static_cast<int>(metrics.rhythmPatternCount * 50)) + "/50)");
    }
    
    if (metrics.melodyPatternCount < 0.3f) {
        recommendations.push_back("🎹 Mehr Melodie-Patterns lernen über Mikrofon-Eingabe (aktuell: " +
            std::to_string(static_cast<int>(metrics.melodyPatternCount * 50)) + "/50)");
    }
    
    // Feature Extraction
    if (metrics.tracksWithMFCC < 0.7f) {
        recommendations.push_back("📊 Feature-Extraktion durchführen für " +
            std::to_string(static_cast<int>((1.0f - metrics.tracksWithMFCC) * 100)) + "% der Tracks");
    }
    
    // Unterrepräsentierte Genres
    if (!metrics.underrepresentedGenres.empty()) {
        recommendations.push_back("📚 Unterrepräsentierte Genres verstärken:");
        for (const auto& genre : metrics.underrepresentedGenres) {
            recommendations.push_back("  → " + genre);
        }
    }
    
    return recommendations;
}

std::vector<std::string> DataQualityAnalyzer::findCriticalGaps(
    const DataQualityMetrics& metrics) {
    
    std::vector<std::string> gaps;
    
    if (metrics.genreCoverage < 0.2f) {
        gaps.push_back("KRITISCH: Zu wenig Genre-Diversität");
    }
    
    if (metrics.rhythmPatternCount == 0.0f && metrics.melodyPatternCount == 0.0f) {
        gaps.push_back("KRITISCH: Keine gelernten Patterns - System kann nicht von User lernen");
    }
    
    if (metrics.tracksWithMFCC < 0.3f) {
        gaps.push_back("KRITISCH: Features fehlen für ML-Training");
    }
    
    if (metrics.instrumentDiversity < 0.2f) {
        gaps.push_back("Wenig Instrument-Vielfalt erkannt");
    }
    
    return gaps;
}

float DataQualityAnalyzer::calculateOverallScore(const DataQualityMetrics& metrics) {
    // Gewichteter Score
    float score = 0.0f;
    
    score += metrics.genreCoverage * 0.20f;           // 20%
    score += metrics.tempoRangeCoverage * 0.10f;      // 10%
    score += metrics.instrumentDiversity * 0.10f;     // 10%
    score += metrics.rhythmPatternCount * 0.15f;      // 15%
    score += metrics.melodyPatternCount * 0.15f;      // 15%
    score += metrics.tracksWithMFCC * 0.15f;          // 15%
    score += metrics.tracksWithSpectral * 0.10f;      // 10%
    score += metrics.patternQualityAvg * 0.05f;       // 5%
    
    return std::clamp(score, 0.0f, 1.0f);
}

std::vector<std::pair<std::string, float>> DataQualityAnalyzer::getPriorityActions() {
    auto metrics = analyze();
    std::vector<std::pair<std::string, float>> actions;
    
    // Berechne Prioritäten basierend auf Lücken
    if (metrics.genreCoverage < 0.5f) {
        actions.push_back({"Mehr Tracks verschiedener Genres importieren", 
                          1.0f - metrics.genreCoverage});
    }
    
    if (metrics.rhythmPatternCount < 0.5f) {
        actions.push_back({"Rhythmus-Patterns über Mikrofon lernen",
                          1.0f - metrics.rhythmPatternCount});
    }
    
    if (metrics.melodyPatternCount < 0.5f) {
        actions.push_back({"Melodie-Patterns über Mikrofon lernen",
                          1.0f - metrics.melodyPatternCount});
    }
    
    if (metrics.tracksWithMFCC < 0.7f) {
        actions.push_back({"Feature-Extraktion durchführen",
                          1.0f - metrics.tracksWithMFCC});
    }
    
    if (metrics.tempoRangeCoverage < 0.6f) {
        actions.push_back({"Tempo-Balance verbessern (langsam/mittel/schnell)",
                          1.0f - metrics.tempoRangeCoverage});
    }
    
    // Sortiere nach Priorität (höchste zuerst)
    std::sort(actions.begin(), actions.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });
    
    return actions;
}

void DataQualityAnalyzer::analyzeTempoDistribution(DataQualityMetrics& metrics) {
    auto allMedia = db_.getAll();
    
    for (const auto& track : allMedia) {
        if (track.bpm <= 0) continue;
        
        if (track.bpm < 90) {
            metrics.tracksSlow++;
        } else if (track.bpm <= 130) {
            metrics.tracksMedium++;
        } else {
            metrics.tracksFast++;
        }
    }
}

} // namespace SongGen
