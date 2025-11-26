#pragma once

#include <vector>
#include <string>
#include <map>

namespace SongGen {

// Song section types
enum class SectionType {
    INTRO,
    VERSE,
    PRE_CHORUS,
    CHORUS,
    BRIDGE,
    BREAKDOWN,
    BUILD_UP,
    DROP,
    OUTRO,
    INSTRUMENTAL
};

// Transition type between sections
enum class TransitionType {
    DIRECT,      // No transition
    FADE,        // Crossfade
    FILL,        // Drum fill
    BUILD,       // Energy build-up
    BREAKDOWN,   // Remove elements
    SILENCE      // Brief pause
};

// Single section in the song
struct SongSection {
    SectionType type;
    float startTime;        // In beats
    float duration;         // In beats
    float energy;           // 0.0 to 1.0
    float complexity;       // 0.0 to 1.0
    TransitionType transitionIn;
    TransitionType transitionOut;
    std::string name;
    
    SongSection() : type(SectionType::VERSE), startTime(0.0f), 
                    duration(16.0f), energy(0.5f), complexity(0.5f),
                    transitionIn(TransitionType::DIRECT),
                    transitionOut(TransitionType::DIRECT) {}
    
    SongSection(SectionType t, float start, float dur) 
        : type(t), startTime(start), duration(dur), 
          energy(0.5f), complexity(0.5f),
          transitionIn(TransitionType::DIRECT),
          transitionOut(TransitionType::DIRECT) {
        name = getSectionName(t);
    }
    
    static std::string getSectionName(SectionType type);
    float getEndTime() const { return startTime + duration; }
};

// Complete song structure
struct SongStructure {
    std::vector<SongSection> sections;
    float totalDuration;    // In beats
    float tempo;
    std::string genre;
    
    SongStructure() : totalDuration(0.0f), tempo(120.0f) {}
    
    void addSection(const SongSection& section);
    SongSection getSectionAtTime(float time) const;
    int getSectionIndexAtTime(float time) const;
    
    // Get energy curve over time
    float getEnergyAtTime(float time) const;
    float getComplexityAtTime(float time) const;
};

class SongStructureEngine {
public:
    SongStructureEngine();
    ~SongStructureEngine() = default;
    
    // Generate complete song structure by genre
    SongStructure generateStructure(const std::string& genre, 
                                     float targetDuration = 180.0f,  // seconds
                                     float tempo = 120.0f);
    
    // Standard song structures
    SongStructure popStructure(float tempo);        // Intro-V-C-V-C-Bridge-C-Outro
    SongStructure edmStructure(float tempo);        // Intro-Build-Drop-Break-Build-Drop-Outro
    SongStructure trapStructure(float tempo);       // Intro-V-C-V-C-Bridge-C-Outro (shorter sections)
    SongStructure rockStructure(float tempo);       // Intro-V-C-V-C-Solo-C-Outro
    SongStructure metalStructure(float tempo);      // Intro-V-C-V-C-Breakdown-Solo-C-Outro
    SongStructure jazzStructure(float tempo);       // Head-Solo-Solo-Solo-Head
    
    // Build custom structure
    SongStructure buildCustomStructure(const std::vector<SectionType>& sectionTypes,
                                        const std::vector<float>& durations,
                                        float tempo);
    
    // Energy analysis
    float calculateSectionEnergy(SectionType type, const std::string& genre);
    float calculateSectionComplexity(SectionType type, const std::string& genre);
    
    // Transitions
    TransitionType suggestTransition(SectionType from, SectionType to, const std::string& genre);
    float getTransitionDuration(TransitionType type, float tempo);
    
    // Arrangement helpers
    bool shouldRepeatSection(SectionType type, int occurrenceCount);
    int getTypicalSectionLength(SectionType type, const std::string& genre);  // In bars
    
private:
    std::map<std::string, std::vector<SectionType>> genreStructures_;
    std::map<std::string, std::map<SectionType, int>> sectionLengths_;
    
    void initializeGenreStructures();
    void initializeSectionLengths();
    
    SongSection createSection(SectionType type, float startTime, 
                               int bars, float tempo, const std::string& genre);
};

} // namespace SongGen
