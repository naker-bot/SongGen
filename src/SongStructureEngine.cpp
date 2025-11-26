#include "../include/SongStructureEngine.h"
#include <algorithm>

namespace SongGen {

std::string SongSection::getSectionName(SectionType type) {
    switch (type) {
        case SectionType::INTRO: return "Intro";
        case SectionType::VERSE: return "Verse";
        case SectionType::PRE_CHORUS: return "Pre-Chorus";
        case SectionType::CHORUS: return "Chorus";
        case SectionType::BRIDGE: return "Bridge";
        case SectionType::BREAKDOWN: return "Breakdown";
        case SectionType::BUILD_UP: return "Build-Up";
        case SectionType::DROP: return "Drop";
        case SectionType::OUTRO: return "Outro";
        case SectionType::INSTRUMENTAL: return "Instrumental";
        default: return "Section";
    }
}

void SongStructure::addSection(const SongSection& section) {
    sections.push_back(section);
    totalDuration = std::max(totalDuration, section.getEndTime());
}

SongSection SongStructure::getSectionAtTime(float time) const {
    int idx = getSectionIndexAtTime(time);
    if (idx >= 0 && idx < (int)sections.size()) {
        return sections[idx];
    }
    return SongSection();  // Default
}

int SongStructure::getSectionIndexAtTime(float time) const {
    for (size_t i = 0; i < sections.size(); ++i) {
        if (time >= sections[i].startTime && time < sections[i].getEndTime()) {
            return i;
        }
    }
    return sections.empty() ? -1 : sections.size() - 1;
}

float SongStructure::getEnergyAtTime(float time) const {
    SongSection section = getSectionAtTime(time);
    return section.energy;
}

float SongStructure::getComplexityAtTime(float time) const {
    SongSection section = getSectionAtTime(time);
    return section.complexity;
}

// SongStructureEngine implementation
SongStructureEngine::SongStructureEngine() {
    initializeGenreStructures();
    initializeSectionLengths();
}

void SongStructureEngine::initializeGenreStructures() {
    using ST = SectionType;
    
    // Pop: Intro-Verse-Chorus-Verse-Chorus-Bridge-Chorus-Outro
    genreStructures_["Pop"] = {
        ST::INTRO, ST::VERSE, ST::CHORUS, ST::VERSE, ST::CHORUS, 
        ST::BRIDGE, ST::CHORUS, ST::OUTRO
    };
    
    // EDM: Intro-Build-Drop-Break-Build-Drop-Outro
    genreStructures_["EDM"] = {
        ST::INTRO, ST::BUILD_UP, ST::DROP, ST::BREAKDOWN, 
        ST::BUILD_UP, ST::DROP, ST::OUTRO
    };
    
    // Trap: Intro-Verse-Chorus-Verse-Chorus-Bridge-Chorus-Outro
    genreStructures_["Trap"] = {
        ST::INTRO, ST::VERSE, ST::CHORUS, ST::VERSE, ST::CHORUS, 
        ST::BRIDGE, ST::CHORUS, ST::OUTRO
    };
    
    // Rock: Intro-Verse-Chorus-Verse-Chorus-Solo-Chorus-Outro
    genreStructures_["Rock"] = {
        ST::INTRO, ST::VERSE, ST::CHORUS, ST::VERSE, ST::CHORUS, 
        ST::INSTRUMENTAL, ST::CHORUS, ST::OUTRO
    };
    
    // Metal: Intro-Verse-Chorus-Verse-Chorus-Breakdown-Solo-Chorus-Outro
    genreStructures_["Metal"] = {
        ST::INTRO, ST::VERSE, ST::CHORUS, ST::VERSE, ST::CHORUS, 
        ST::BREAKDOWN, ST::INSTRUMENTAL, ST::CHORUS, ST::OUTRO
    };
    
    // Jazz: Head-Solo1-Solo2-Solo3-Head
    genreStructures_["Jazz"] = {
        ST::INTRO, ST::VERSE, ST::INSTRUMENTAL, ST::INSTRUMENTAL, 
        ST::INSTRUMENTAL, ST::VERSE, ST::OUTRO
    };
}

void SongStructureEngine::initializeSectionLengths() {
    // Length in bars (4/4 time)
    sectionLengths_["Pop"][SectionType::INTRO] = 4;
    sectionLengths_["Pop"][SectionType::VERSE] = 8;
    sectionLengths_["Pop"][SectionType::CHORUS] = 8;
    sectionLengths_["Pop"][SectionType::BRIDGE] = 8;
    sectionLengths_["Pop"][SectionType::OUTRO] = 4;
    
    sectionLengths_["EDM"][SectionType::INTRO] = 8;
    sectionLengths_["EDM"][SectionType::BUILD_UP] = 8;
    sectionLengths_["EDM"][SectionType::DROP] = 16;
    sectionLengths_["EDM"][SectionType::BREAKDOWN] = 8;
    sectionLengths_["EDM"][SectionType::OUTRO] = 8;
    
    sectionLengths_["Trap"][SectionType::INTRO] = 4;
    sectionLengths_["Trap"][SectionType::VERSE] = 8;
    sectionLengths_["Trap"][SectionType::CHORUS] = 8;
    sectionLengths_["Trap"][SectionType::BRIDGE] = 4;
    sectionLengths_["Trap"][SectionType::OUTRO] = 4;
    
    sectionLengths_["Rock"][SectionType::INTRO] = 4;
    sectionLengths_["Rock"][SectionType::VERSE] = 8;
    sectionLengths_["Rock"][SectionType::CHORUS] = 8;
    sectionLengths_["Rock"][SectionType::INSTRUMENTAL] = 16;
    sectionLengths_["Rock"][SectionType::OUTRO] = 4;
    
    sectionLengths_["Metal"][SectionType::INTRO] = 8;
    sectionLengths_["Metal"][SectionType::VERSE] = 8;
    sectionLengths_["Metal"][SectionType::CHORUS] = 8;
    sectionLengths_["Metal"][SectionType::BREAKDOWN] = 8;
    sectionLengths_["Metal"][SectionType::INSTRUMENTAL] = 16;
    sectionLengths_["Metal"][SectionType::OUTRO] = 4;
    
    sectionLengths_["Jazz"][SectionType::INTRO] = 8;
    sectionLengths_["Jazz"][SectionType::VERSE] = 16;  // "Head"
    sectionLengths_["Jazz"][SectionType::INSTRUMENTAL] = 16;  // Solo
    sectionLengths_["Jazz"][SectionType::OUTRO] = 8;
}

SongStructure SongStructureEngine::generateStructure(const std::string& genre, 
                                                       float targetDuration, 
                                                       float tempo) {
    if (genre == "Pop") return popStructure(tempo);
    if (genre == "EDM" || genre == "House" || genre == "Techno") return edmStructure(tempo);
    if (genre == "Trap" || genre == "Hip-Hop") return trapStructure(tempo);
    if (genre == "Rock") return rockStructure(tempo);
    if (genre == "Metal") return metalStructure(tempo);
    if (genre == "Jazz") return jazzStructure(tempo);
    
    // Default
    return popStructure(tempo);
}

SongStructure SongStructureEngine::popStructure(float tempo) {
    SongStructure structure;
    structure.tempo = tempo;
    structure.genre = "Pop";
    
    float currentTime = 0.0f;
    
    // Intro - 4 bars
    auto intro = createSection(SectionType::INTRO, currentTime, 4, tempo, "Pop");
    intro.energy = 0.3f;
    intro.complexity = 0.3f;
    structure.addSection(intro);
    currentTime += intro.duration;
    
    // Verse 1 - 8 bars
    auto verse1 = createSection(SectionType::VERSE, currentTime, 8, tempo, "Pop");
    verse1.energy = 0.5f;
    verse1.complexity = 0.4f;
    structure.addSection(verse1);
    currentTime += verse1.duration;
    
    // Chorus 1 - 8 bars
    auto chorus1 = createSection(SectionType::CHORUS, currentTime, 8, tempo, "Pop");
    chorus1.energy = 0.8f;
    chorus1.complexity = 0.6f;
    chorus1.transitionIn = TransitionType::FILL;
    structure.addSection(chorus1);
    currentTime += chorus1.duration;
    
    // Verse 2 - 8 bars
    auto verse2 = createSection(SectionType::VERSE, currentTime, 8, tempo, "Pop");
    verse2.energy = 0.5f;
    verse2.complexity = 0.5f;
    structure.addSection(verse2);
    currentTime += verse2.duration;
    
    // Chorus 2 - 8 bars
    auto chorus2 = createSection(SectionType::CHORUS, currentTime, 8, tempo, "Pop");
    chorus2.energy = 0.85f;
    chorus2.complexity = 0.7f;
    chorus2.transitionIn = TransitionType::FILL;
    structure.addSection(chorus2);
    currentTime += chorus2.duration;
    
    // Bridge - 8 bars
    auto bridge = createSection(SectionType::BRIDGE, currentTime, 8, tempo, "Pop");
    bridge.energy = 0.6f;
    bridge.complexity = 0.7f;
    bridge.transitionIn = TransitionType::BREAKDOWN;
    bridge.transitionOut = TransitionType::BUILD;
    structure.addSection(bridge);
    currentTime += bridge.duration;
    
    // Final Chorus - 8 bars
    auto chorus3 = createSection(SectionType::CHORUS, currentTime, 8, tempo, "Pop");
    chorus3.energy = 0.9f;
    chorus3.complexity = 0.8f;
    structure.addSection(chorus3);
    currentTime += chorus3.duration;
    
    // Outro - 4 bars
    auto outro = createSection(SectionType::OUTRO, currentTime, 4, tempo, "Pop");
    outro.energy = 0.3f;
    outro.complexity = 0.3f;
    outro.transitionIn = TransitionType::FADE;
    structure.addSection(outro);
    
    return structure;
}

SongStructure SongStructureEngine::edmStructure(float tempo) {
    SongStructure structure;
    structure.tempo = tempo;
    structure.genre = "EDM";
    
    float currentTime = 0.0f;
    
    // Intro - 8 bars
    auto intro = createSection(SectionType::INTRO, currentTime, 8, tempo, "EDM");
    intro.energy = 0.2f;
    intro.complexity = 0.3f;
    structure.addSection(intro);
    currentTime += intro.duration;
    
    // Build-up 1 - 8 bars
    auto build1 = createSection(SectionType::BUILD_UP, currentTime, 8, tempo, "EDM");
    build1.energy = 0.4f;
    build1.complexity = 0.5f;
    structure.addSection(build1);
    currentTime += build1.duration;
    
    // Drop 1 - 16 bars
    auto drop1 = createSection(SectionType::DROP, currentTime, 16, tempo, "EDM");
    drop1.energy = 1.0f;
    drop1.complexity = 0.8f;
    drop1.transitionIn = TransitionType::DIRECT;
    structure.addSection(drop1);
    currentTime += drop1.duration;
    
    // Breakdown - 8 bars
    auto breakdown = createSection(SectionType::BREAKDOWN, currentTime, 8, tempo, "EDM");
    breakdown.energy = 0.3f;
    breakdown.complexity = 0.4f;
    breakdown.transitionIn = TransitionType::BREAKDOWN;
    structure.addSection(breakdown);
    currentTime += breakdown.duration;
    
    // Build-up 2 - 8 bars
    auto build2 = createSection(SectionType::BUILD_UP, currentTime, 8, tempo, "EDM");
    build2.energy = 0.5f;
    build2.complexity = 0.6f;
    structure.addSection(build2);
    currentTime += build2.duration;
    
    // Drop 2 - 16 bars
    auto drop2 = createSection(SectionType::DROP, currentTime, 16, tempo, "EDM");
    drop2.energy = 1.0f;
    drop2.complexity = 0.9f;
    structure.addSection(drop2);
    currentTime += drop2.duration;
    
    // Outro - 8 bars
    auto outro = createSection(SectionType::OUTRO, currentTime, 8, tempo, "EDM");
    outro.energy = 0.2f;
    outro.complexity = 0.3f;
    outro.transitionIn = TransitionType::FADE;
    structure.addSection(outro);
    
    return structure;
}

SongStructure SongStructureEngine::trapStructure(float tempo) {
    // Similar to pop but with shorter sections
    SongStructure structure;
    structure.tempo = tempo;
    structure.genre = "Trap";
    
    float currentTime = 0.0f;
    
    structure.addSection(createSection(SectionType::INTRO, currentTime, 4, tempo, "Trap"));
    currentTime += 16.0f;
    
    structure.addSection(createSection(SectionType::VERSE, currentTime, 8, tempo, "Trap"));
    currentTime += 32.0f;
    
    structure.addSection(createSection(SectionType::CHORUS, currentTime, 8, tempo, "Trap"));
    currentTime += 32.0f;
    
    structure.addSection(createSection(SectionType::VERSE, currentTime, 8, tempo, "Trap"));
    currentTime += 32.0f;
    
    structure.addSection(createSection(SectionType::CHORUS, currentTime, 8, tempo, "Trap"));
    currentTime += 32.0f;
    
    structure.addSection(createSection(SectionType::BRIDGE, currentTime, 4, tempo, "Trap"));
    currentTime += 16.0f;
    
    structure.addSection(createSection(SectionType::CHORUS, currentTime, 8, tempo, "Trap"));
    currentTime += 32.0f;
    
    structure.addSection(createSection(SectionType::OUTRO, currentTime, 4, tempo, "Trap"));
    
    return structure;
}

SongStructure SongStructureEngine::rockStructure(float tempo) {
    SongStructure structure;
    structure.tempo = tempo;
    structure.genre = "Rock";
    
    float currentTime = 0.0f;
    
    structure.addSection(createSection(SectionType::INTRO, currentTime, 4, tempo, "Rock"));
    currentTime += 16.0f;
    
    structure.addSection(createSection(SectionType::VERSE, currentTime, 8, tempo, "Rock"));
    currentTime += 32.0f;
    
    structure.addSection(createSection(SectionType::CHORUS, currentTime, 8, tempo, "Rock"));
    currentTime += 32.0f;
    
    structure.addSection(createSection(SectionType::VERSE, currentTime, 8, tempo, "Rock"));
    currentTime += 32.0f;
    
    structure.addSection(createSection(SectionType::CHORUS, currentTime, 8, tempo, "Rock"));
    currentTime += 32.0f;
    
    // Guitar solo
    auto solo = createSection(SectionType::INSTRUMENTAL, currentTime, 16, tempo, "Rock");
    solo.energy = 0.9f;
    solo.complexity = 0.9f;
    structure.addSection(solo);
    currentTime += 64.0f;
    
    structure.addSection(createSection(SectionType::CHORUS, currentTime, 8, tempo, "Rock"));
    currentTime += 32.0f;
    
    structure.addSection(createSection(SectionType::OUTRO, currentTime, 4, tempo, "Rock"));
    
    return structure;
}

SongStructure SongStructureEngine::metalStructure(float tempo) {
    SongStructure structure;
    structure.tempo = tempo;
    structure.genre = "Metal";
    
    float currentTime = 0.0f;
    
    structure.addSection(createSection(SectionType::INTRO, currentTime, 8, tempo, "Metal"));
    currentTime += 32.0f;
    
    structure.addSection(createSection(SectionType::VERSE, currentTime, 8, tempo, "Metal"));
    currentTime += 32.0f;
    
    structure.addSection(createSection(SectionType::CHORUS, currentTime, 8, tempo, "Metal"));
    currentTime += 32.0f;
    
    structure.addSection(createSection(SectionType::VERSE, currentTime, 8, tempo, "Metal"));
    currentTime += 32.0f;
    
    structure.addSection(createSection(SectionType::CHORUS, currentTime, 8, tempo, "Metal"));
    currentTime += 32.0f;
    
    // Breakdown
    auto breakdown = createSection(SectionType::BREAKDOWN, currentTime, 8, tempo, "Metal");
    breakdown.energy = 0.7f;
    breakdown.complexity = 0.6f;
    structure.addSection(breakdown);
    currentTime += 32.0f;
    
    // Guitar solo
    auto solo = createSection(SectionType::INSTRUMENTAL, currentTime, 16, tempo, "Metal");
    solo.energy = 1.0f;
    solo.complexity = 1.0f;
    structure.addSection(solo);
    currentTime += 64.0f;
    
    structure.addSection(createSection(SectionType::CHORUS, currentTime, 8, tempo, "Metal"));
    currentTime += 32.0f;
    
    structure.addSection(createSection(SectionType::OUTRO, currentTime, 4, tempo, "Metal"));
    
    return structure;
}

SongStructure SongStructureEngine::jazzStructure(float tempo) {
    SongStructure structure;
    structure.tempo = tempo;
    structure.genre = "Jazz";
    
    float currentTime = 0.0f;
    
    // Intro
    structure.addSection(createSection(SectionType::INTRO, currentTime, 8, tempo, "Jazz"));
    currentTime += 32.0f;
    
    // Head (melody)
    auto head1 = createSection(SectionType::VERSE, currentTime, 16, tempo, "Jazz");
    head1.name = "Head";
    head1.energy = 0.6f;
    head1.complexity = 0.7f;
    structure.addSection(head1);
    currentTime += 64.0f;
    
    // Solos
    for (int i = 0; i < 3; ++i) {
        auto solo = createSection(SectionType::INSTRUMENTAL, currentTime, 16, tempo, "Jazz");
        solo.name = "Solo " + std::to_string(i + 1);
        solo.energy = 0.7f + (i * 0.1f);
        solo.complexity = 0.8f + (i * 0.05f);
        structure.addSection(solo);
        currentTime += 64.0f;
    }
    
    // Head out
    auto head2 = createSection(SectionType::VERSE, currentTime, 16, tempo, "Jazz");
    head2.name = "Head Out";
    head2.energy = 0.6f;
    head2.complexity = 0.7f;
    structure.addSection(head2);
    currentTime += 64.0f;
    
    // Outro
    structure.addSection(createSection(SectionType::OUTRO, currentTime, 8, tempo, "Jazz"));
    
    return structure;
}

SongStructure SongStructureEngine::buildCustomStructure(
    const std::vector<SectionType>& sectionTypes,
    const std::vector<float>& durations,
    float tempo) {
    
    SongStructure structure;
    structure.tempo = tempo;
    
    float currentTime = 0.0f;
    for (size_t i = 0; i < sectionTypes.size(); ++i) {
        float duration = i < durations.size() ? durations[i] : 16.0f;
        SongSection section(sectionTypes[i], currentTime, duration);
        structure.addSection(section);
        currentTime += duration;
    }
    
    return structure;
}

float SongStructureEngine::calculateSectionEnergy(SectionType type, const std::string& genre) {
    switch (type) {
        case SectionType::INTRO: return 0.3f;
        case SectionType::VERSE: return 0.5f;
        case SectionType::PRE_CHORUS: return 0.6f;
        case SectionType::CHORUS: return 0.85f;
        case SectionType::BRIDGE: return 0.6f;
        case SectionType::BREAKDOWN: return 0.3f;
        case SectionType::BUILD_UP: return 0.5f;
        case SectionType::DROP: return 1.0f;
        case SectionType::OUTRO: return 0.2f;
        case SectionType::INSTRUMENTAL: return 0.8f;
        default: return 0.5f;
    }
}

float SongStructureEngine::calculateSectionComplexity(SectionType type, const std::string& genre) {
    switch (type) {
        case SectionType::INTRO: return 0.3f;
        case SectionType::VERSE: return 0.4f;
        case SectionType::PRE_CHORUS: return 0.5f;
        case SectionType::CHORUS: return 0.7f;
        case SectionType::BRIDGE: return 0.7f;
        case SectionType::BREAKDOWN: return 0.3f;
        case SectionType::BUILD_UP: return 0.6f;
        case SectionType::DROP: return 0.8f;
        case SectionType::OUTRO: return 0.3f;
        case SectionType::INSTRUMENTAL: return 0.9f;
        default: return 0.5f;
    }
}

TransitionType SongStructureEngine::suggestTransition(SectionType from, SectionType to, 
                                                       const std::string& genre) {
    // Verse to Chorus: usually a fill
    if (from == SectionType::VERSE && to == SectionType::CHORUS) {
        return TransitionType::FILL;
    }
    
    // Build-up to Drop: direct (the tension is the transition)
    if (from == SectionType::BUILD_UP && to == SectionType::DROP) {
        return TransitionType::DIRECT;
    }
    
    // To breakdown: remove elements
    if (to == SectionType::BREAKDOWN) {
        return TransitionType::BREAKDOWN;
    }
    
    // From bridge: build energy
    if (from == SectionType::BRIDGE) {
        return TransitionType::BUILD;
    }
    
    // To outro: fade
    if (to == SectionType::OUTRO) {
        return TransitionType::FADE;
    }
    
    return TransitionType::DIRECT;
}

float SongStructureEngine::getTransitionDuration(TransitionType type, float tempo) {
    float beatDuration = 60.0f / tempo;
    
    switch (type) {
        case TransitionType::DIRECT: return 0.0f;
        case TransitionType::FADE: return 2.0f * beatDuration;  // 2 beats
        case TransitionType::FILL: return 1.0f * beatDuration;  // 1 bar
        case TransitionType::BUILD: return 4.0f * beatDuration;  // 4 beats
        case TransitionType::BREAKDOWN: return 2.0f * beatDuration;
        case TransitionType::SILENCE: return 0.5f * beatDuration;
        default: return 0.0f;
    }
}

bool SongStructureEngine::shouldRepeatSection(SectionType type, int occurrenceCount) {
    switch (type) {
        case SectionType::CHORUS: return occurrenceCount < 3;
        case SectionType::VERSE: return occurrenceCount < 2;
        default: return false;
    }
}

int SongStructureEngine::getTypicalSectionLength(SectionType type, const std::string& genre) {
    if (sectionLengths_.find(genre) != sectionLengths_.end()) {
        auto& genreLengths = sectionLengths_[genre];
        if (genreLengths.find(type) != genreLengths.end()) {
            return genreLengths[type];
        }
    }
    
    // Default lengths
    switch (type) {
        case SectionType::INTRO: return 4;
        case SectionType::VERSE: return 8;
        case SectionType::CHORUS: return 8;
        case SectionType::BRIDGE: return 8;
        case SectionType::BREAKDOWN: return 4;
        case SectionType::BUILD_UP: return 8;
        case SectionType::DROP: return 16;
        case SectionType::OUTRO: return 4;
        case SectionType::INSTRUMENTAL: return 16;
        default: return 8;
    }
}

SongSection SongStructureEngine::createSection(SectionType type, float startTime, 
                                                 int bars, float tempo, const std::string& genre) {
    float duration = bars * 4.0f;  // 4 beats per bar
    SongSection section(type, startTime, duration);
    section.energy = calculateSectionEnergy(type, genre);
    section.complexity = calculateSectionComplexity(type, genre);
    return section;
}

} // namespace SongGen
