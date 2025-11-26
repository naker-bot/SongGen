#pragma once

#include "MediaDatabase.h"
#include "PatternCaptureEngine.h"
#include <string>
#include <vector>
#include <map>

namespace SongGen {

/**
 * Data Quality Metrics - Was fehlt für perfekte KI-Generierung?
 */
struct DataQualityMetrics {
    // Datenbank-Vollständigkeit
    float genreCoverage;           // 0-1: Wie viele Genres haben genug Samples?
    float tempoRangeCoverage;      // 0-1: BPM-Bereiche abgedeckt?
    float instrumentDiversity;     // 0-1: Verschiedene Instrumente erkannt?
    float structuralVariety;       // 0-1: Song-Strukturen (Intro/Verse/Chorus)?
    
    // Pattern-Bibliothek
    float rhythmPatternCount;      // 0-1: Genug gelernte Rhythmus-Patterns?
    float melodyPatternCount;      // 0-1: Genug gelernte Melodie-Patterns?
    float patternQualityAvg;       // 0-1: Durchschnittliche User-Bewertung
    
    // Feature-Extraktion
    float tracksWithMFCC;          // 0-1: % Tracks mit MFCC analysiert
    float tracksWithSpectral;      // 0-1: % Tracks mit Spektral-Features
    float tracksWithChords;        // 0-1: % Tracks mit Chord-Analyse
    float tracksWithStructure;     // 0-1: % Tracks mit Struktur-Analyse
    
    // Genre-Balance
    std::map<std::string, int> genreDistribution;
    std::vector<std::string> underrepresentedGenres;
    std::vector<std::string> wellRepresentedGenres;
    
    // Tempo-Balance
    int tracksSlow;      // < 90 BPM
    int tracksMedium;    // 90-130 BPM
    int tracksFast;      // > 130 BPM
    
    // Training-Qualität
    float modelConfidence;         // 0-1: Wie sicher ist das ML-Modell?
    int trainingEpochs;
    float lastTrainingLoss;
    
    // Overall Score
    float overallQuality;          // 0-1: Gesamt-Score
    
    // Recommendations
    std::vector<std::string> recommendations;  // Was sollte verbessert werden?
    std::vector<std::string> missingData;      // Was fehlt konkret?
    
    DataQualityMetrics() : 
        genreCoverage(0.0f), tempoRangeCoverage(0.0f), instrumentDiversity(0.0f),
        structuralVariety(0.0f), rhythmPatternCount(0.0f), melodyPatternCount(0.0f),
        patternQualityAvg(0.0f), tracksWithMFCC(0.0f), tracksWithSpectral(0.0f),
        tracksWithChords(0.0f), tracksWithStructure(0.0f), tracksSlow(0), 
        tracksMedium(0), tracksFast(0), modelConfidence(0.0f), trainingEpochs(0),
        lastTrainingLoss(0.0f), overallQuality(0.0f) {}
};

/**
 * Genre Completeness - Wie vollständig ist ein Genre?
 */
struct GenreCompleteness {
    std::string genre;
    int trackCount;
    int minRecommended;           // Empfohlene Mindestanzahl
    float completeness;           // 0-1: trackCount / minRecommended
    
    // Feature-Abdeckung für dieses Genre
    float avgMFCCQuality;
    float avgSpectralQuality;
    int tracksWithGoodFeatures;
    
    std::vector<std::string> missingElements;  // Was fehlt noch?
    
    GenreCompleteness() : trackCount(0), minRecommended(100), completeness(0.0f),
                         avgMFCCQuality(0.0f), avgSpectralQuality(0.0f),
                         tracksWithGoodFeatures(0) {}
};

/**
 * DataQualityAnalyzer - Analysiert Datenqualität und zeigt Lücken
 */
class DataQualityAnalyzer {
public:
    DataQualityAnalyzer(MediaDatabase& db);
    
    /**
     * Vollständige Analyse durchführen
     */
    DataQualityMetrics analyze();
    
    /**
     * Analysiere Genre-Vollständigkeit
     */
    std::vector<GenreCompleteness> analyzeGenreCoverage();
    
    /**
     * Analysiere Pattern-Bibliothek
     */
    void analyzePatterns(PatternCaptureEngine* patternEngine, DataQualityMetrics& metrics);
    
    /**
     * Generiere Empfehlungen basierend auf Lücken
     */
    std::vector<std::string> generateRecommendations(const DataQualityMetrics& metrics);
    
    /**
     * Finde die wichtigsten fehlenden Daten
     */
    std::vector<std::string> findCriticalGaps(const DataQualityMetrics& metrics);
    
    /**
     * Berechne Overall Quality Score
     */
    float calculateOverallScore(const DataQualityMetrics& metrics);
    
    /**
     * Priorisierte To-Do Liste für Datenverbesserung
     */
    std::vector<std::pair<std::string, float>> getPriorityActions();
    
private:
    MediaDatabase& db_;
    
    // Helper functions
    float analyzeGenreBalance();
    float analyzeTempoRange();
    float analyzeInstrumentDiversity();
    float analyzeFeatureCompleteness();
    void analyzeTempoDistribution(DataQualityMetrics& metrics);
    std::vector<std::string> identifyUnderrepresentedGenres();
};

} // namespace SongGen
