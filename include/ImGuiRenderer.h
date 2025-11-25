#ifndef IMGUIRENDERER_H
#define IMGUIRENDERER_H

#include "MediaDatabase.h"
#include "AudioAnalyzer.h"
#include "FileBrowser.h"
#include "SongGenerator.h"
#include "HVSCDownloader.h"
#include "AudioPlayer.h"
#include <memory>
#include <string>

struct SDL_Window;
struct SDL_Renderer;
union SDL_Event;

/**
 * ImGuiRenderer - Haupt-UI für SongGen
 * 
 * Tabs:
 * 1. Datenbank-Browser: Zeigt alle Medien, Filter, Suche
 * 2. File-Browser: Lokale/Netzwerk-Dateien, Mehrfachauswahl
 * 3. Analyse: Audio-Analyse-Status, Batch-Processing
 * 4. Generator: Song-Generierungs-Controls
 * 5. HVSC: HVSC-Download und SID-Extraktion
 * 6. Einstellungen: NPU, Threads, Pfade
 */
class ImGuiRenderer {
public:
    ImGuiRenderer();
    ~ImGuiRenderer();
    
    bool initialize(int width = 1280, int height = 720);
    void run();  // Main loop
    void shutdown();
    
private:
    // SDL
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    bool running_ = false;
    
    // Components
    std::unique_ptr<MediaDatabase> database_;
    std::unique_ptr<AudioAnalyzer> analyzer_;
    std::unique_ptr<FileBrowser> fileBrowser_;
    std::unique_ptr<SongGenerator> generator_;
    std::unique_ptr<HVSCDownloader> hvscDownloader_;
    std::unique_ptr<AudioPlayer> audioPlayer_;
    
    // UI-State
    int currentTab_ = 0;
    std::string statusMessage_;
    float progress_ = 0.0f;
    
    // Render-Funktionen
    void renderMainMenu();
    void renderDatabaseBrowser();
    void renderFileBrowser();
    void renderAnalyzer();
    void renderGenerator();
    void renderHVSC();
    void renderMusicSorter();  // NEU: Automatische Musik-Sortierung
    void renderSettings();
    
    // Database-Browser
    std::vector<MediaMetadata> filteredMedia_;
    std::string searchQuery_;
    std::string genreFilter_;
    std::string intensityFilter_;
    int selectedMediaIndex_ = -1;
    std::string currentlyPlaying_;
    
    // Generator-State
    GenerationParams genParams_;
    std::string outputPath_ = "~/.songgen/generated/";
    bool isGenerating_ = false;
    
    // HVSC-State
    bool isDownloadingHVSC_ = false;
    std::thread hvscThread_;
    std::thread autoSyncThread_;
    std::atomic<bool> autoSyncRunning_{false};
    std::string hvscPhase_;
    std::atomic<size_t> hvscProgress_{0};
    std::atomic<size_t> hvscTotal_{0};
    std::atomic<float> hvscSpeed_{0.0f};
    
    // MP3-Extraktion Progress (getrennt vom Download)
    std::atomic<size_t> mp3Extracted_{0};
    std::atomic<size_t> mp3Total_{0};
    std::atomic<bool> stopExtraction_{false};
    
    // Datenbank-Lösch-Bestätigung
    bool showDeleteConfirm_ = false;
    std::atomic<bool> isDeleting_{false};
    std::atomic<size_t> deleteProgress_{0};
    std::atomic<size_t> deleteTotal_{0};
    
    // Live-Log-System
    std::vector<std::string> logMessages_;
    std::mutex logMutex_;
    void addLogMessage(const std::string& msg);
    void renderLogWindow();
    
    // Auto-Sync
    void startAutoSync();
    void stopAutoSync();
    
    // File-Browser-State
    std::vector<FileEntry> selectedFiles_;
    
    // Settings persistence
    void loadSettings();
    void saveSettings();
    std::string settingsPath_;
    
    // Helper
    void updateStatus(const std::string& message, float progress = -1.0f);
    void handleEvent(const SDL_Event& event);
    void addFilesToDatabase();
    void extractAudioToMP3();
    void startGeneration();
};

#endif // IMGUIRENDERER_H
