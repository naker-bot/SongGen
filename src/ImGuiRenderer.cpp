#include "ImGuiRenderer.h"
#include "Logger.h"
#include "AudioAnalyzer.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include <SDL.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <thread>
#include <map>
#include <algorithm>

namespace fs = std::filesystem;

ImGuiRenderer::ImGuiRenderer() {
}

ImGuiRenderer::~ImGuiRenderer() {
}

bool ImGuiRenderer::initialize(int width, int height) {
    std::cout << "🎨 Initializing ImGui renderer...\n";
    
    // SDL initialisieren
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::cerr << "SDL_Init Error: " << SDL_GetError() << std::endl;
        return false;
    }
    
    // Fenster erstellen
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    window_ = SDL_CreateWindow(
        "SongGen - KI Song Generator",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height,
        window_flags
    );
    
    if (!window_) {
        std::cerr << "SDL_CreateWindow Error: " << SDL_GetError() << std::endl;
        return false;
    }
    
    // Renderer erstellen
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
    if (!renderer_) {
        std::cerr << "SDL_CreateRenderer Error: " << SDL_GetError() << std::endl;
        return false;
    }
    
    // ImGui Setup
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    
    // ImGui Style - Custom Dark Theme
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    
    // Verbesserte Farben
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.15f, 0.15f, 0.18f, 1.00f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.20f, 0.25f, 0.35f, 1.00f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.35f, 0.50f, 1.00f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.30f, 0.45f, 0.60f, 1.00f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.20f, 0.25f, 0.35f, 1.00f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.35f, 0.50f, 1.00f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.30f, 0.45f, 0.60f, 1.00f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.15f, 0.18f, 1.00f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.20f, 0.25f, 1.00f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.25f, 0.25f, 0.30f, 1.00f);
    
    // Runde Ecken
    style.WindowRounding = 5.0f;
    style.FrameRounding = 3.0f;
    style.ScrollbarRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 3.0f;
    
    // Padding
    style.WindowPadding = ImVec2(10, 10);
    style.FramePadding = ImVec2(8, 4);
    style.ItemSpacing = ImVec2(8, 6);
    
    // ImGui Platform/Renderer Backends
    ImGui_ImplSDL2_InitForSDLRenderer(window_, renderer_);
    ImGui_ImplSDLRenderer2_Init(renderer_);
    
    // Komponenten initialisieren
    std::string dbPath = fs::path(getenv("HOME")) / ".songgen" / "media.db";
    database_ = std::make_unique<MediaDatabase>(dbPath);
    if (!database_->initialize()) {
        std::cerr << "Failed to initialize database" << std::endl;
        return false;
    }
    
    analyzer_ = std::make_unique<AudioAnalyzer>();
    fileBrowser_ = std::make_unique<FileBrowser>();
    generator_ = std::make_unique<SongGenerator>(*database_);
    hvscDownloader_ = std::make_unique<HVSCDownloader>();
    
    audioPlayer_ = std::make_unique<AudioPlayer>();
    if (!audioPlayer_->initialize()) {
        std::cerr << "Failed to initialize audio player" << std::endl;
    }
    
    // Initial Navigation
    const char* home = getenv("HOME");
    if (home) {
        fileBrowser_->navigate(home);
    }
    
    // Lade Datenbank
    filteredMedia_ = database_->getAll();
    
    running_ = true;
    
    // Starte Auto-Sync Thread
    startAutoSync();
    
    // Starte MP3-Counter Thread
    mp3Total_ = 75442; // Bekannte HVSC Gesamtzahl
    std::thread([this]() {
        const char* home = getenv("HOME");
        if (!home) return;
        
        std::string mp3Dir = std::string(home) + "/.songgen/hvsc/mp3/";
        
        while (running_) {
            size_t mp3Count = 0;
            if (fs::exists(mp3Dir)) {
                try {
                    for (const auto& entry : fs::recursive_directory_iterator(mp3Dir)) {
                        if (entry.is_regular_file() && entry.path().extension() == ".mp3") {
                            mp3Count++;
                        }
                    }
                    mp3Extracted_ = mp3Count;
                } catch (...) {
                    // Ignore errors during counting
                }
            }
            // Zähle alle 10 Sekunden neu
            std::this_thread::sleep_for(std::chrono::seconds(10));
        }
    }).detach();
    
    // Logger-Callback setzen
    Logger::instance().setCallback([this](const std::string& msg) {
        addLogMessage(msg);
    });
    
    // Lade gespeicherte Einstellungen
    loadSettings();
    
    std::cout << "✅ ImGui renderer initialized\n";
    Logger::success("ImGui Renderer initialisiert");
    return true;
}

void ImGuiRenderer::startAutoSync() {
    autoSyncRunning_ = true;
    
    autoSyncThread_ = std::thread([this]() {
        const char* home = getenv("HOME");
        if (!home) return;
        
        std::string hvscMp3Dir = std::string(home) + "/.songgen/hvsc/mp3/";
        
        while (autoSyncRunning_) {
            // Prüfe alle 30 Sekunden
            std::this_thread::sleep_for(std::chrono::seconds(30));
            
            if (!fs::exists(hvscMp3Dir) || !fs::is_directory(hvscMp3Dir)) {
                continue;
            }
            
            // Zähle DB-Einträge und MP3-Dateien
            size_t dbCount = 0;
            {
                auto allFiles = database_->getAll();
                for (const auto& meta : allFiles) {
                    if (meta.genre == "SID") dbCount++;
                }
            }
            
            size_t mp3Count = 0;
            try {
                for (const auto& entry : fs::recursive_directory_iterator(hvscMp3Dir)) {
                    if (entry.is_regular_file() && entry.path().extension() == ".mp3") {
                        mp3Count++;
                    }
                }
            } catch (...) {
                continue; // Fehler beim Iterator ignorieren
            }
            
            // Import wenn Differenz (nur SID-Dateien als MP3)
            if (mp3Count > 0 && dbCount < mp3Count) {
                std::string msg = "🔄 Auto-Sync: " + std::to_string(mp3Count) + " MP3s, " + 
                                  std::to_string(dbCount) + " SIDs in DB → Importiere " + 
                                  std::to_string(mp3Count - dbCount);
                addLogMessage(msg);
                
                size_t added = hvscDownloader_->addToDatabase(hvscMp3Dir, *database_, false);
                
                if (added > 0) {
                    filteredMedia_ = database_->getAll();
                    std::string successMsg = "✅ Auto-Sync: " + std::to_string(added) + " neue SID-MP3s importiert";
                    addLogMessage(successMsg);
                }
            }
        }
    });
}

void ImGuiRenderer::stopAutoSync() {
    autoSyncRunning_ = false;
    if (autoSyncThread_.joinable()) {
        autoSyncThread_.join();
    }
}

void ImGuiRenderer::run() {
    while (running_) {
        // Event-Handling
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            handleEvent(event);
            
            // Keyboard Shortcuts
            if (event.type == SDL_KEYDOWN && !ImGui::GetIO().WantCaptureKeyboard) {
                switch (event.key.keysym.sym) {
                    case SDLK_1: currentTab_ = 0; break;  // Datenbank
                    case SDLK_2: currentTab_ = 1; break;  // Browser
                    case SDLK_3: currentTab_ = 2; break;  // Analyse
                    case SDLK_4: currentTab_ = 3; break;  // Generator
                    case SDLK_5: currentTab_ = 4; break;  // HVSC
                    case SDLK_6: currentTab_ = 5; break;  // Sorter
                    case SDLK_7: currentTab_ = 6; break;  // Settings
                    case SDLK_SPACE:  // Play/Pause
                        if (audioPlayer_->isPlaying()) {
                            if (audioPlayer_->isPaused()) audioPlayer_->play();
                            else audioPlayer_->pause();
                        }
                        break;
                    case SDLK_ESCAPE:  // Stop
                        if (audioPlayer_->isPlaying()) {
                            audioPlayer_->stop();
                            currentlyPlaying_.clear();
                        }
                        break;
                    case SDLK_q:  // Quit
                        if (SDL_GetModState() & KMOD_CTRL) running_ = false;
                        break;
                }
            }
        }
        
        // ImGui New Frame
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        
        // Main UI
        renderMainMenu();
        
        // Tab Content
        switch (currentTab_) {
            case 0: renderDatabaseBrowser(); break;
            case 1: renderFileBrowser(); break;
            case 2: renderAnalyzer(); break;
            case 3: renderGenerator(); break;
            case 4: renderHVSC(); break;
            case 5: renderMusicSorter(); break;  // NEU: Automatische Sortierung
            case 6: renderSettings(); break;
        }
        
        // Status Bar (immer sichtbar)
        ImGui::SetNextWindowPos(ImVec2(0, ImGui::GetIO().DisplaySize.y - 30));
        ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x, 30));
        ImGui::Begin("StatusBar", nullptr, 
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
        
        if (!statusMessage_.empty()) {
            ImGui::Text("%s", statusMessage_.c_str());
            if (progress_ >= 0.0f) {
                ImGui::SameLine();
                ImGui::ProgressBar(progress_, ImVec2(200, 0));
            }
        } else {
            // Default Status Info
            ImGui::Text("📊 DB: %zu Einträge", filteredMedia_.size());
            ImGui::SameLine(200);
            
            if (audioPlayer_->isPlaying()) {
                ImGui::Text("🎵 %s | %.1f/%.1fs", 
                    currentlyPlaying_.c_str(),
                    audioPlayer_->getPosition(), 
                    audioPlayer_->getDuration());
            } else {
                ImGui::Text("⏹ Kein Playback");
            }
            
            ImGui::SameLine(ImGui::GetIO().DisplaySize.x - 250);
            ImGui::Text("Tab: %d/6 | FPS: %.1f", currentTab_ + 1, ImGui::GetIO().Framerate);
        }
        
        ImGui::End();
        
        // Rendering
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer_, 25, 25, 30, 255);
        SDL_RenderClear(renderer_);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer_);
        SDL_RenderPresent(renderer_);
    }
}

void ImGuiRenderer::shutdown() {
    stopAutoSync();
    
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
}

void ImGuiRenderer::renderMainMenu() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Datei")) {
            if (ImGui::MenuItem("Beenden")) {
                running_ = false;
            }
            ImGui::EndMenu();
        }
        
        // Tab-Auswahl mit Shortcuts
        ImGui::Separator();
        if (ImGui::MenuItem("Datenbank", "1", currentTab_ == 0)) currentTab_ = 0;
        if (ImGui::MenuItem("Browser", "2", currentTab_ == 1)) currentTab_ = 1;
        if (ImGui::MenuItem("Analyse", "3", currentTab_ == 2)) currentTab_ = 2;
        if (ImGui::MenuItem("Generator", "4", currentTab_ == 3)) currentTab_ = 3;
        if (ImGui::MenuItem("HVSC", "5", currentTab_ == 4)) currentTab_ = 4;
        if (ImGui::MenuItem("🎵 Sorter", "6", currentTab_ == 5)) currentTab_ = 5;
        if (ImGui::MenuItem("Einstellungen", "7", currentTab_ == 6)) currentTab_ = 6;
        
        ImGui::Separator();
        
        // Playback-Shortcuts Hint
        if (!currentlyPlaying_.empty()) {
            ImGui::Text("⌨ Space=Play/Pause ESC=Stop");
        }
        
        ImGui::EndMainMenuBar();
    }
}

void ImGuiRenderer::renderDatabaseBrowser() {
    ImGui::Begin("Datenbank Browser", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    
    ImGui::Text("🗂️ Medien-Datenbank: %zu Einträge", filteredMedia_.size());
    
    // Audio-Player-Controls
    if (!currentlyPlaying_.empty()) {
        ImGui::Separator();
        ImGui::Text("🎵 Spielt: %s", currentlyPlaying_.c_str());
        
        float position = audioPlayer_->getPosition();
        float duration = audioPlayer_->getDuration();
        
        // Progress Bar mit Seek-Funktion
        if (ImGui::SliderFloat("##position", &position, 0.0f, duration, "%.1fs / %.1fs")) {
            audioPlayer_->seek(position);
        }
        
        // Play/Pause/Stop Buttons
        if (audioPlayer_->isPlaying() && !audioPlayer_->isPaused()) {
            if (ImGui::Button("⏸ Pause")) audioPlayer_->pause();
        } else {
            if (ImGui::Button("▶ Play")) audioPlayer_->play();
        }
        ImGui::SameLine();
        if (ImGui::Button("⏹ Stop")) {
            audioPlayer_->stop();
            currentlyPlaying_.clear();
        }
        
        // Volume Control
        ImGui::SameLine();
        float volume = audioPlayer_->getVolume();
        ImGui::SetNextItemWidth(100);
        if (ImGui::SliderFloat("🔊", &volume, 0.0f, 1.0f, "%.2f")) {
            audioPlayer_->setVolume(volume);
        }
    }
    
    ImGui::Separator();
    
    // Suche und Filter
    static char searchBuf[256] = "";
    ImGui::InputText("🔍 Suche", searchBuf, sizeof(searchBuf));
    
    ImGui::SameLine();
    if (ImGui::Button("➕ Zur Datenbank hinzufügen")) {
        // Öffne File-Browser zum Hinzufügen
        ImGui::OpenPopup("Dateien hinzufügen");
    }
    
    ImGui::SameLine();
    if (ImGui::Button("🔄 Fehlende Dateien bereinigen")) {
        // Entferne DB-Einträge für nicht existierende Dateien
        std::thread([this]() {
            auto allMedia = database_->getAll();
            size_t removed = 0;
            
            addLogMessage("🔍 Prüfe " + std::to_string(allMedia.size()) + " Datenbank-Einträge...");
            
            for (const auto& meta : allMedia) {
                if (!std::filesystem::exists(meta.filepath)) {
                    database_->deleteMedia(meta.id);
                    removed++;
                    if (removed % 100 == 0) {
                        addLogMessage("🗑️ Entfernt: " + std::to_string(removed) + " fehlende Dateien");
                    }
                }
            }
            
            if (removed > 0) {
                addLogMessage("✅ " + std::to_string(removed) + " fehlende Einträge aus DB entfernt");
                filteredMedia_ = database_->getAll();
            } else {
                addLogMessage("✅ Alle Dateien vorhanden - keine Bereinigung nötig");
            }
        }).detach();
    }
    
    ImGui::SameLine();
    if (ImGui::Button("🗑️ Datenbank löschen")) {
        showDeleteConfirm_ = true;
        ImGui::OpenPopup("Datenbank wirklich löschen?");
    }
    
    // Sicherheitsabfrage für Datenbank-Löschung
    if (ImGui::BeginPopupModal("Datenbank wirklich löschen?", &showDeleteConfirm_, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!isDeleting_) {
            ImGui::Text("⚠️ WARNUNG: Alle Datenbank-Einträge werden gelöscht!");
            ImGui::Text("Diese Aktion kann nicht rückgängig gemacht werden.");
            ImGui::Separator();
            ImGui::Text("Aktuell: %zu Einträge in der Datenbank", filteredMedia_.size());
            ImGui::Separator();
            
            if (ImGui::Button("✅ Ja, wirklich löschen", ImVec2(200, 0))) {
                isDeleting_ = true;
                deleteProgress_ = 0;
                
                // Lösche alle Einträge asynchron
                std::thread([this]() {
                    auto allMedia = database_->getAll();
                    deleteTotal_ = allMedia.size();
                    
                    addLogMessage("🗑️ Lösche " + std::to_string(deleteTotal_.load()) + " Datenbank-Einträge...");
                    
                    for (const auto& meta : allMedia) {
                        database_->deleteMedia(meta.id);
                        deleteProgress_++;
                    }
                    
                    addLogMessage("✅ Datenbank vollständig geleert!");
                    filteredMedia_.clear();
                    isDeleting_ = false;
                }).detach();
            }
            
            ImGui::SameLine();
            if (ImGui::Button("❌ Nein, abbrechen", ImVec2(200, 0))) {
                showDeleteConfirm_ = false;
                ImGui::CloseCurrentPopup();
            }
        } else {
            // Zeige Fortschritt während Löschung
            ImGui::Text("🗑️ Lösche Datenbank...");
            ImGui::Separator();
            
            float progress = deleteTotal_ > 0 ? (float)deleteProgress_.load() / deleteTotal_.load() : 0.0f;
            ImGui::ProgressBar(progress, ImVec2(300, 0));
            ImGui::Text("%zu / %zu Einträge gelöscht (%.1f%%)", 
                        deleteProgress_.load(), 
                        deleteTotal_.load(),
                        progress * 100.0f);
            
            // Schließe automatisch wenn fertig
            if (deleteProgress_ >= deleteTotal_ && deleteTotal_ > 0) {
                showDeleteConfirm_ = false;
                ImGui::CloseCurrentPopup();
            }
        }
        
        ImGui::EndPopup();
    }
    
    // Popup für Dateien hinzufügen
    if (ImGui::BeginPopupModal("Dateien hinzufügen", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        static char addPath[512] = "";
        ImGui::Text("Wähle Verzeichnis oder einzelne WAV/MP3 Datei:");
        ImGui::InputText("Pfad", addPath, sizeof(addPath));
        ImGui::TextDisabled("Beispiel: /home/user/music oder /home/user/song.wav");
        
        ImGui::Separator();
        
        if (ImGui::Button("✅ Hinzufügen")) {
            std::string pathStr = addPath;
            if (!pathStr.empty()) {
                // Import im Background-Thread
                std::thread([this, pathStr]() {
                    AudioAnalyzer analyzer;
                    
                    if (std::filesystem::is_directory(pathStr)) {
                        // Verzeichnis rekursiv durchsuchen
                        size_t added = 0;
                        addLogMessage("🔍 Durchsuche Verzeichnis: " + pathStr);
                        
                        for (const auto& entry : std::filesystem::recursive_directory_iterator(pathStr)) {
                            if (entry.is_regular_file()) {
                                std::string ext = entry.path().extension().string();
                                if (ext == ".wav" || ext == ".WAV" || ext == ".mp3" || ext == ".MP3") {
                                    MediaMetadata meta;
                                    if (analyzer.analyze(entry.path().string(), meta)) {
                                        database_->addMedia(meta);
                                        added++;
                                        if (added % 100 == 0) {
                                            addLogMessage("🎵 Importiert: " + std::to_string(added) + " Dateien");
                                        }
                                    }
                                }
                            }
                        }
                        addLogMessage("✅ " + std::to_string(added) + " Dateien zur Datenbank hinzugefügt");
                        filteredMedia_ = database_->getAll();
                    } else if (std::filesystem::is_regular_file(pathStr)) {
                        // Einzelne Datei
                        MediaMetadata meta;
                        if (analyzer.analyze(pathStr, meta)) {
                            database_->addMedia(meta);
                            addLogMessage("✅ Datei zur Datenbank hinzugefügt: " + pathStr);
                            filteredMedia_ = database_->getAll();
                        } else {
                            addLogMessage("❌ Fehler beim Analysieren: " + pathStr);
                        }
                    } else {
                        addLogMessage("❌ Pfad nicht gefunden: " + pathStr);
                    }
                }).detach();
                
                ImGui::CloseCurrentPopup();
            }
        }
        
        ImGui::SameLine();
        if (ImGui::Button("❌ Abbrechen")) {
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::EndPopup();
    }
    
    // Genre-Filter
    auto genres = database_->getAllGenres();
    static int selectedGenre = 0;
    std::vector<const char*> genreItems;
    genreItems.push_back("Alle");
    for (const auto& g : genres) genreItems.push_back(g.c_str());
    
    ImGui::Combo("Genre", &selectedGenre, genreItems.data(), genreItems.size());
    
    // Sortierung
    static int sortMode = 0;
    const char* sortItems[] = {"Original", "Titel A-Z", "Titel Z-A", "Genre A-Z", "BPM aufsteigend", "BPM absteigend"};
    ImGui::Combo("Sortierung", &sortMode, sortItems, IM_ARRAYSIZE(sortItems));
    
    // Sortiere filteredMedia_ basierend auf sortMode
    if (sortMode == 1) { // Titel A-Z
        std::sort(filteredMedia_.begin(), filteredMedia_.end(), 
            [](const MediaMetadata& a, const MediaMetadata& b) { return a.title < b.title; });
    } else if (sortMode == 2) { // Titel Z-A
        std::sort(filteredMedia_.begin(), filteredMedia_.end(), 
            [](const MediaMetadata& a, const MediaMetadata& b) { return a.title > b.title; });
    } else if (sortMode == 3) { // Genre A-Z
        std::sort(filteredMedia_.begin(), filteredMedia_.end(), 
            [](const MediaMetadata& a, const MediaMetadata& b) { return a.genre < b.genre; });
    } else if (sortMode == 4) { // BPM aufsteigend
        std::sort(filteredMedia_.begin(), filteredMedia_.end(), 
            [](const MediaMetadata& a, const MediaMetadata& b) { return a.bpm < b.bpm; });
    } else if (sortMode == 5) { // BPM absteigend
        std::sort(filteredMedia_.begin(), filteredMedia_.end(), 
            [](const MediaMetadata& a, const MediaMetadata& b) { return a.bpm > b.bpm; });
    }
    
    // Tabelle mit Play-Button
    if (ImGui::BeginTable("MediaTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("Titel");
        ImGui::TableSetupColumn("Genre");
        ImGui::TableSetupColumn("BPM");
        ImGui::TableSetupColumn("Intensität");
        ImGui::TableSetupColumn("Bass");
        ImGui::TableHeadersRow();
        
        for (size_t i = 0; i < filteredMedia_.size(); ++i) {
            const auto& meta = filteredMedia_[i];
            ImGui::TableNextRow();
            
            // Play-Button
            ImGui::TableNextColumn();
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::SmallButton("▶")) {
                if (audioPlayer_->load(meta.filepath)) {
                    audioPlayer_->play();
                    currentlyPlaying_ = meta.title;
                    selectedMediaIndex_ = static_cast<int>(i);
                }
            }
            ImGui::PopID();
            
            ImGui::TableNextColumn();
            ImGui::Text("%s", meta.title.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s", meta.genre.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%.1f", meta.bpm);
            ImGui::TableNextColumn();
            ImGui::Text("%s", meta.intensity.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s", meta.bassLevel.c_str());
        }
        
        ImGui::EndTable();
    }
    
    ImGui::End();
}

void ImGuiRenderer::renderFileBrowser() {
    ImGui::Begin("Datei Browser", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    
    ImGui::Text("📁 Pfad: %s", fileBrowser_->getCurrentPath().c_str());
    
    if (ImGui::Button("← Zurück")) {
        fileBrowser_->navigateUp();
    }
    ImGui::SameLine();
    if (ImGui::Button("🔄 Aktualisieren")) {
        fileBrowser_->refresh();
    }
    ImGui::SameLine();
    if (ImGui::Button("✓ Alle Auswählen")) {
        fileBrowser_->selectAll();
    }
    ImGui::SameLine();
    if (ImGui::Button("✗ Alle Abwählen")) {
        fileBrowser_->deselectAll();
    }
    
    ImGui::Separator();
    
    // Drag & Drop Zone
    ImGui::BeginChild("DropZone", ImVec2(0, 80), true);
    ImGui::TextColored(ImVec4(0.5f, 0.7f, 1.0f, 1.0f), "💾 Drag & Drop Zone");
    ImGui::Text("Ziehe Dateien hierher um sie zur Datenbank hinzuzufügen");
    ImGui::Text("Oder nutze die Datei-Liste unten");
    ImGui::EndChild();
    
    ImGui::Separator();
    
    // Suche
    static char searchBuf[256] = "";
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 100);
    if (ImGui::InputText("🔍 Suche", searchBuf, sizeof(searchBuf))) {
        fileBrowser_->setSearchQuery(searchBuf);
    }
    ImGui::SameLine();
    if (ImGui::Button("Löschen")) {
        searchBuf[0] = '\0';
        fileBrowser_->setSearchQuery("");
    }
    
    ImGui::Separator();
    
    // Dateiliste
    auto entries = fileBrowser_->getEntries();
    for (auto& entry : entries) {
        bool selected = entry.isSelected;
        
        if (entry.isDirectory) {
            if (ImGui::Selectable(("📁 " + entry.name).c_str(), false, ImGuiSelectableFlags_DontClosePopups)) {
                fileBrowser_->navigate(entry.path);
            }
        } else {
            if (ImGui::Checkbox(("📄 " + entry.name).c_str(), &selected)) {
                if (selected) {
                    fileBrowser_->selectEntry(entry.path);
                } else {
                    fileBrowser_->deselectEntry(entry.path);
                }
            }
        }
    }
    
    ImGui::Separator();
    
    if (ImGui::Button("➕ Zur Datenbank hinzufügen")) {
        addFilesToDatabase();
    }
    
    ImGui::SameLine();
    if (ImGui::Button("🎵 Audio extrahieren → MP3")) {
        extractAudioToMP3();
    }
    
    ImGui::End();
}

void ImGuiRenderer::renderAnalyzer() {
    ImGui::Begin("Audio-Analyse", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    
    auto unanalyzed = database_->getUnanalyzed();
    ImGui::Text("Nicht analysierte Dateien: %zu", unanalyzed.size());
    
    if (ImGui::Button("🔍 Alle analysieren")) {
        updateStatus("Analysiere Dateien...", 0.0f);
        
        // Sammle Pfade
        std::vector<std::string> paths;
        auto unanalyzed = database_->getUnanalyzed();
        for (const auto& meta : unanalyzed) {
            paths.push_back(meta.filepath);
        }
        
        // TODO: Background-Thread für Analyse
        auto results = analyzer_->analyzeBatch(
            paths,
            [this](size_t current, size_t total) {
                progress_ = static_cast<float>(current) / total;
            }
        );
        
        // Update Database
        for (const auto& meta : results) {
            database_->updateMedia(meta);
        }
        
        updateStatus("Analyse abgeschlossen!", 1.0f);
    }
    
    ImGui::End();
}

void ImGuiRenderer::renderGenerator() {
    ImGui::Begin("Song Generator", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    
    ImGui::Text("🎵 Generiere neuen Song");
    ImGui::Separator();
    
    // Genre-Auswahl
    const char* genres[] = { "Trap", "Techno", "Rock", "Pop", "Klassik", "Metal", "New Metal", "Trance", "Dubstep", "House" };
    static int genreIdx = 0;
    ImGui::Combo("Genre", &genreIdx, genres, IM_ARRAYSIZE(genres));
    genParams_.genre = genres[genreIdx];
    
    // BPM
    ImGui::SliderFloat("BPM", &genParams_.bpm, 60.0f, 200.0f);
    
    // Intensität
    const char* intensities[] = { "soft", "mittel", "hart" };
    static int intensityIdx = 1;
    ImGui::Combo("Intensität", &intensityIdx, intensities, IM_ARRAYSIZE(intensities));
    genParams_.intensity = intensities[intensityIdx];
    
    // Bass-Level
    const char* bassLevels[] = { "soft", "mittel", "basslastig" };
    static int bassIdx = 1;
    ImGui::Combo("Bass-Level", &bassIdx, bassLevels, IM_ARRAYSIZE(bassLevels));
    genParams_.bassLevel = bassLevels[bassIdx];
    
    // Dauer
    ImGui::SliderInt("Dauer (Sek)", &genParams_.duration, 30, 600);
    
    // Vocals
    ImGui::Checkbox("Vocals hinzufügen", &genParams_.useVocals);
    
    ImGui::Separator();
    
    // Ausgabe-Pfad
    static char outputBuf[512];
    snprintf(outputBuf, sizeof(outputBuf), "%s", outputPath_.c_str());
    ImGui::InputText("Ausgabe", outputBuf, sizeof(outputBuf));
    outputPath_ = outputBuf;
    
    ImGui::Separator();
    
    if (!isGenerating_) {
        if (ImGui::Button("🎼 Song generieren")) {
            saveSettings();  // Speichere Einstellungen
            startGeneration();
        }
        ImGui::SameLine();
        if (ImGui::Button("🎧 Vorschau (30s)")) {
            genParams_.duration = 30;
            saveSettings();  // Speichere Einstellungen
            startGeneration();
        }
        ImGui::SameLine();
        if (ImGui::Button("💾 Settings speichern")) {
            saveSettings();
        }
    } else {
        ImGui::Text("Generierung läuft...");
        ImGui::ProgressBar(progress_);
    }
    
    // Erweiterte Audio-Effekte für generierte Songs
    ImGui::Separator();
    if (ImGui::CollapsingHeader("🎛️ Audio-Effekte")) {
        // Fade In/Out
        ImGui::Text("⏱️ Fade Effekte:");
        float fadeIn = audioPlayer_->getFadeInDuration();
        float fadeOut = audioPlayer_->getFadeOutDuration();
        
        if (ImGui::SliderFloat("Fade In (s)", &fadeIn, 0.0f, 10.0f, "%.1fs")) {
            audioPlayer_->setFadeIn(fadeIn);
        }
        if (ImGui::SliderFloat("Fade Out (s)", &fadeOut, 0.0f, 10.0f, "%.1fs")) {
            audioPlayer_->setFadeOut(fadeOut);
        }
        
        // Compressor (Softener/Hardener)
        ImGui::Separator();
        ImGui::Text("🔨 Dynamik-Kompressor:");
        float compThreshold = audioPlayer_->getCompressorThreshold();
        float compRatio = audioPlayer_->getCompressorRatio();
        
        if (ImGui::SliderFloat("Threshold (dB)", &compThreshold, -40.0f, 0.0f, "%.1f dB")) {
            audioPlayer_->setCompressor(compThreshold, compRatio);
        }
        if (ImGui::SliderFloat("Ratio (Härte)", &compRatio, 1.0f, 20.0f, "%.1f:1")) {
            audioPlayer_->setCompressor(compThreshold, compRatio);
        }
        ImGui::TextDisabled("  Niedrige Ratio = Soft, Hohe Ratio = Hard");
        
        // Preset Buttons
        if (ImGui::Button("Soft (1:1)")) audioPlayer_->setCompressor(-20.0f, 1.0f);
        ImGui::SameLine();
        if (ImGui::Button("Medium (4:1)")) audioPlayer_->setCompressor(-20.0f, 4.0f);
        ImGui::SameLine();
        if (ImGui::Button("Hard (10:1)")) audioPlayer_->setCompressor(-20.0f, 10.0f);
        ImGui::SameLine();
        if (ImGui::Button("Limiter (20:1)")) audioPlayer_->setCompressor(-10.0f, 20.0f);
        
        // Solo-Kanal
        ImGui::Separator();
        ImGui::Text("🎧 Kanal-Auswahl:");
        int soloChannel = audioPlayer_->getSoloChannel();
        if (ImGui::RadioButton("Stereo", &soloChannel, 0)) audioPlayer_->setSoloChannel(0);
        ImGui::SameLine();
        if (ImGui::RadioButton("Nur Links", &soloChannel, 1)) audioPlayer_->setSoloChannel(1);
        ImGui::SameLine();
        if (ImGui::RadioButton("Nur Rechts", &soloChannel, 2)) audioPlayer_->setSoloChannel(2);
        
        // Normalisierung
        ImGui::Separator();
        bool normalize = audioPlayer_->getNormalization();
        if (ImGui::Checkbox("🔊 Auto-Normalisierung", &normalize)) {
            audioPlayer_->setNormalization(normalize);
        }
        ImGui::TextDisabled("  Automatische Lautstärke-Anpassung");
        
        // Geschwindigkeit und Tonhöhe
        ImGui::Separator();
        ImGui::Text("⚡ Geschwindigkeit & Tonhöhe:");
        
        float speed = audioPlayer_->getSpeed();
        if (ImGui::SliderFloat("Speed", &speed, 0.25f, 4.0f, "%.2fx")) {
            audioPlayer_->setSpeed(speed);
        }
        ImGui::TextDisabled("  0.5x = halb so schnell, 2.0x = doppelt so schnell");
        
        int pitch = audioPlayer_->getPitch();
        if (ImGui::SliderInt("Pitch", &pitch, -12, 12, "%d Semitones")) {
            audioPlayer_->setPitch(pitch);
        }
        ImGui::TextDisabled("  -12 = eine Oktave tiefer, +12 = eine Oktave höher");
        
        bool preserveVocals = audioPlayer_->getPreserveVocals();
        if (ImGui::Checkbox("🎤 Vokal-Erhaltung", &preserveVocals)) {
            audioPlayer_->setPreserveVocals(preserveVocals);
        }
        ImGui::TextDisabled("  Verhindert \"Chipmunk\"-Effekt bei Pitch-Shift");
        
        // Preset Buttons
        if (ImGui::Button("Slow (-25%)")) { audioPlayer_->setSpeed(0.75f); audioPlayer_->setPitch(-3); }
        ImGui::SameLine();
        if (ImGui::Button("Normal")) { audioPlayer_->setSpeed(1.0f); audioPlayer_->setPitch(0); }
        ImGui::SameLine();
        if (ImGui::Button("Fast (+50%)")) { audioPlayer_->setSpeed(1.5f); audioPlayer_->setPitch(+2); }
        ImGui::SameLine();
        if (ImGui::Button("Nightcore (+25%)")) { audioPlayer_->setSpeed(1.25f); audioPlayer_->setPitch(+4); }
    }
    
    ImGui::End();
}

void ImGuiRenderer::renderHVSC() {
    ImGui::Begin("HVSC Download & Extraktion", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    
    ImGui::Text("📦 HVSC - High Voltage SID Collection");
    ImGui::Text("53.000+ Commodore 64 SID Musikdateien");
    ImGui::Separator();
    
    static char hvscPath[512] = "~/.songgen/hvsc/";
    ImGui::InputText("Ziel-Verzeichnis", hvscPath, sizeof(hvscPath));
    
    if (!isDownloadingHVSC_) {
        if (ImGui::Button("📥 HVSC herunterladen")) {
            isDownloadingHVSC_ = true;
            hvscProgress_ = 0;
            hvscTotal_ = 0;
            hvscSpeed_ = 0.0f;
            hvscPhase_ = "Starte Download...";
            stopExtraction_ = false;  // Reset Stop-Flag
            
            // Asynchroner Download im Background-Thread
            hvscThread_ = std::thread([this, hvscPath]() {
                std::string path = hvscPath;
                hvscDownloader_->downloadHVSC(path, 
                    [this](size_t current, size_t total, float speed) {
                        hvscProgress_ = current;
                        hvscTotal_ = total;
                        hvscSpeed_ = speed;
                        
                        // Wenn Total groß ist (>100MB), dann ist es Download (Bytes)
                        // Wenn Total klein ist (<100000), dann ist es Extraktion (Track-Count)
                        if (total > 100000000) {
                            // Download-Phase
                            if (current < total) {
                                hvscPhase_ = "Download läuft...";
                            } else {
                                hvscPhase_ = "Extrahiere Archiv...";
                            }
                        } else {
                            // Extraktions-Phase (Track-Count)
                            mp3Extracted_ = current;
                            hvscPhase_ = "🎵 Extrahiere: " + std::to_string(current) + "/" + std::to_string(total);
                        }
                    },
                    true,           // autoConvertAndImport
                    &stopExtraction_ // Stop-Flag für Abbruch
                );
                isDownloadingHVSC_ = false;
                if (stopExtraction_) {
                    hvscPhase_ = "⏹️ Gestoppt!";
                } else {
                    hvscPhase_ = "Fertig!";
                }
            });
            hvscThread_.detach();
        }
    } else {
        // Zeige Progress während Download
        ImGui::Text("Status: %s", hvscPhase_.c_str());
        if (hvscTotal_ > 0) {
            float progress = static_cast<float>(hvscProgress_) / hvscTotal_;
            ImGui::ProgressBar(progress, ImVec2(400, 0));
            
            // Zeige MB/s oder KB/s
            if (hvscSpeed_ > 1024.0f) {
                ImGui::Text("%.2f MB/s | %zu / %zu MB", 
                    hvscSpeed_ / 1024.0f,
                    hvscProgress_ / (1024*1024),
                    hvscTotal_ / (1024*1024));
            } else {
                ImGui::Text("%.2f KB/s | %zu / %zu KB", 
                    hvscSpeed_.load(),
                    hvscProgress_.load() / 1024,
                    hvscTotal_.load() / 1024);
            }
        }
    }
    
    // Zeige MP3 Extraktions-Progress
    ImGui::Separator();
    ImGui::Text("🎵 MP3 Extraktion");
    
    // Setze Gesamtzahl (bekannt aus HVSC: 75,442 Tracks)
    if (mp3Total_ == 0) {
        mp3Total_ = 75442;
    }
    
    float mp3Progress = mp3Total_ > 0 ? static_cast<float>(mp3Extracted_) / mp3Total_ : 0.0f;
    ImGui::ProgressBar(mp3Progress, ImVec2(400, 0));
    ImGui::Text("%zu / %zu MP3s extrahiert (%.1f%%)", 
                mp3Extracted_.load(), 
                mp3Total_.load(),
                mp3Progress * 100.0f);
    
    // Stop-Button wenn Extraktion läuft
    if (isDownloadingHVSC_ && mp3Extracted_ < mp3Total_ && mp3Extracted_ > 0) {
        ImGui::SameLine();
        if (ImGui::Button("⏹️ Stop")) {
            stopExtraction_ = true;
            hvscPhase_ = "⏹️ Stoppe Extraktion...";
        }
    }
    
    ImGui::Separator();
    if (ImGui::Button("📊 Status prüfen")) {
            std::thread([this, hvscPath]() {
                std::string sidDir = std::string(hvscPath) + "/C64Music/";
                std::string wavDir = std::string(hvscPath) + "/wav/";
                std::string mp3Dir = std::string(hvscPath) + "/mp3/";
                
                hvscPhase_ = "📊 Zähle Dateien...";
                size_t sidCount = 0;
                size_t wavCount = 0;
                size_t mp3Count = 0;
                
                if (fs::exists(sidDir)) {
                    for (const auto& entry : fs::recursive_directory_iterator(sidDir)) {
                        if (entry.is_regular_file()) {
                            std::string ext = entry.path().extension().string();
                            if (ext == ".sid" || ext == ".SID") sidCount++;
                        }
                    }
                }
                
                if (fs::exists(wavDir)) {
                    for (const auto& entry : fs::recursive_directory_iterator(wavDir)) {
                        if (entry.is_regular_file() && entry.path().extension() == ".wav") {
                            wavCount++;
                        }
                    }
                }
                
                if (fs::exists(mp3Dir)) {
                    for (const auto& entry : fs::recursive_directory_iterator(mp3Dir)) {
                        if (entry.is_regular_file() && entry.path().extension() == ".mp3") {
                            mp3Count++;
                        }
                    }
                }
                
                size_t dbCount = 0;
                for (const auto& meta : filteredMedia_) {
                    if (meta.genre == "SID") dbCount++;
                }
                
                hvscPhase_ = "📊 Status: " + std::to_string(sidCount) + " SIDs | " +
                            std::to_string(mp3Count) + " MP3s | " +
                            std::to_string(wavCount) + " WAVs | " +
                            std::to_string(dbCount) + " in DB";
                addLogMessage(hvscPhase_);
                
                // Aktualisiere MP3-Progress
                mp3Extracted_ = mp3Count;
            }).detach();
    }
    
    if (ImGui::Button("🔄 DB Sync - WAVs importieren")) {
        std::thread([this]() {
            std::string hvscPath = "~/.songgen/hvsc/";
            std::string wavDir = hvscPath + "/wav/";
            
            if (!fs::exists(wavDir)) {
                hvscPhase_ = "❌ WAV-Verzeichnis nicht gefunden: " + wavDir;
                addLogMessage(hvscPhase_);
                return;
            }
            
            hvscPhase_ = "🔄 Synchronisiere Datenbank...";
            addLogMessage(hvscPhase_);
            
            size_t added = hvscDownloader_->addToDatabase(wavDir, *database_, false);
            filteredMedia_ = database_->getAll();
            
            size_t dbCount = 0;
            for (const auto& meta : filteredMedia_) {
                if (meta.genre == "SID") dbCount++;
            }
            
            hvscPhase_ = "✅ DB Sync: " + std::to_string(added) + " neue, " +
                        std::to_string(dbCount) + " total in DB";
            addLogMessage(hvscPhase_);
            updateStatus(hvscPhase_, 1.0f);
        }).detach();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(Fügt fehlende WAVs zur DB hinzu)");
    
    ImGui::Separator();
    ImGui::Text("Fortschritt: %zu / %zu", hvscProgress_.load(), hvscTotal_.load());
    
    // Live-Log-Fenster
    renderLogWindow();
    
    ImGui::End();
}

void ImGuiRenderer::addLogMessage(const std::string& msg) {
    std::lock_guard<std::mutex> lock(logMutex_);
    logMessages_.push_back(msg);
    
    // Behalte nur die letzten 500 Zeilen
    if (logMessages_.size() > 500) {
        logMessages_.erase(logMessages_.begin());
    }
    
    // Zeige wichtige Meldungen auch als Desktop-Benachrichtigung (XFCE/Linux)
    if (msg.find("✅") != std::string::npos || 
        msg.find("❌") != std::string::npos ||
        msg.find("⚠️") != std::string::npos) {
        
        // Async notification ohne UI-Blockierung
        std::thread([msg]() {
            std::string cleanMsg = msg;
            // Escape single quotes für Shell
            size_t pos = 0;
            while ((pos = cleanMsg.find("'", pos)) != std::string::npos) {
                cleanMsg.replace(pos, 1, "\\'");
                pos += 2;
            }
            
            std::string cmd = "notify-send -a 'SongGen' -u low '" + cleanMsg + "' 2>/dev/null &";
            system(cmd.c_str());
        }).detach();
    }
}

void ImGuiRenderer::renderLogWindow() {
    ImGui::Separator();
    ImGui::Text("📋 Live-Aktivitäts-Log:");
    
    // Log-Fenster mit Scroll
    ImGui::BeginChild("LogWindow", ImVec2(0, 300), true, ImGuiWindowFlags_HorizontalScrollbar);
    
    std::lock_guard<std::mutex> lock(logMutex_);
    for (const auto& msg : logMessages_) {
        // Farbcodierung nach Icon
        if (msg.find("✅") != std::string::npos) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%s", msg.c_str());
        } else if (msg.find("❌") != std::string::npos || msg.find("Cannot") != std::string::npos) {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "%s", msg.c_str());
        } else if (msg.find("⚠️") != std::string::npos) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "%s", msg.c_str());
        } else if (msg.find("🎵") != std::string::npos) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 1.0f, 1.0f), "%s", msg.c_str());
        } else if (msg.find("🔍") != std::string::npos) {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", msg.c_str());
        } else {
            ImGui::Text("%s", msg.c_str());
        }
    }
    
    // Auto-Scroll zum Ende
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 50.0f) {
        ImGui::SetScrollHereY(1.0f);
    }
    
    ImGui::EndChild();
    
    // Control-Buttons
    if (ImGui::Button("🗑️ Log leeren")) {
        std::lock_guard<std::mutex> lock(logMutex_);
        logMessages_.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("💾 Log speichern")) {
        std::lock_guard<std::mutex> lock(logMutex_);
        std::ofstream logFile(std::string(getenv("HOME")) + "/.songgen/hvsc_log.txt");
        for (const auto& msg : logMessages_) {
            logFile << msg << "\n";
        }
        addLogMessage("✅ Log gespeichert: ~/.songgen/hvsc_log.txt");
    }
}

void ImGuiRenderer::renderSettings() {
    ImGui::Begin("Einstellungen", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    
    ImGui::Text("⚙️ Einstellungen");
    ImGui::Separator();
    
    // System-Info
    ImGui::Text("💻 System-Information:");
    ImGui::Separator();
    
    // NPU-Status prüfen
    AudioAnalyzer analyzer;
    bool hasNPU = false;
    std::string npuType = "Keine";
    
    // Prüfe NPU-Typen
    if (std::system("rocm-smi --showproductname >/dev/null 2>&1") == 0) {
        hasNPU = true;
        npuType = "AMD ROCm";
    } else if (std::system("lspci | grep -i 'neural' >/dev/null 2>&1") == 0) {
        hasNPU = true;
        npuType = "Intel OpenVINO";
    }
    
    ImGui::Text("⚡ NPU: %s", hasNPU ? npuType.c_str() : "❌ Nicht verfügbar");
    if (hasNPU) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "(Aktiv)");
    }
    
    // CPU Info
    int cores = std::thread::hardware_concurrency();
    ImGui::Text("🔢 CPU-Kerne: %d", cores);
    ImGui::Text("🧵 Worker-Threads: %d (8x CPU)", cores * 8);
    
    ImGui::Separator();
    
    // Performance-Einstellungen
    ImGui::Text("🚀 Performance:");
    ImGui::BulletText("SID-Konvertierung: 60s Timeout");
    ImGui::BulletText("Audio-Buffer: 512K Samples");
    ImGui::BulletText("I/O-Buffer: 4MB");
    ImGui::BulletText("Progress-Updates: Alle 250 Dateien");
    
    ImGui::Separator();
    
    // Settings-Management
    ImGui::Text("💾 Einstellungen:");
    if (ImGui::Button("💾 Jetzt speichern")) {
        saveSettings();
    }
    ImGui::SameLine();
    if (ImGui::Button("🔄 Neu laden")) {
        loadSettings();
    }
    ImGui::TextDisabled("  Generator-Einstellungen werden automatisch gespeichert");
    ImGui::TextDisabled("  Speicherort: %s", settingsPath_.c_str());
    
    ImGui::End();
}

void ImGuiRenderer::handleEvent(const SDL_Event& event) {
    if (event.type == SDL_QUIT) {
        running_ = false;
    }
}

void ImGuiRenderer::updateStatus(const std::string& message, float progress) {
    statusMessage_ = message;
    progress_ = progress;
}

void ImGuiRenderer::addFilesToDatabase() {
    auto selected = fileBrowser_->getSelectedEntries();
    
    if (selected.empty()) {
        updateStatus("⚠️ Keine Dateien ausgewählt", 0.0f);
        return;
    }
    
    int added = 0;
    for (const auto& entry : selected) {
        if (entry.isDirectory) continue;  // Skip Verzeichnisse
        
        MediaMetadata meta;
        meta.filepath = entry.path;
        meta.title = entry.name;
        meta.analyzed = false;
        
        if (database_->addMedia(meta)) {
            added++;
        }
    }
    
    fileBrowser_->deselectAll();  // Auswahl zurücksetzen
    updateStatus("✅ Dateien hinzugefügt: " + std::to_string(added), 1.0f);
    filteredMedia_ = database_->getAll();
}

void ImGuiRenderer::extractAudioToMP3() {
    auto selected = fileBrowser_->getSelectedEntries();
    
    if (selected.empty()) {
        updateStatus("⚠️ Keine Dateien ausgewählt", 0.0f);
        return;
    }
    
    // Erstelle Ausgabe-Verzeichnis
    std::string outputDir = std::string(getenv("HOME")) + "/.songgen/extracted/";
    std::string cmd = "mkdir -p " + outputDir;
    system(cmd.c_str());
    
    int extracted = 0;
    int total = selected.size();
    
    for (size_t i = 0; i < selected.size(); ++i) {
        const auto& entry = selected[i];
        if (entry.isDirectory) continue;
        
        // Extrahiere Dateiname ohne Extension
        std::string basename = entry.name;
        size_t dotPos = basename.find_last_of('.');
        if (dotPos != std::string::npos) {
            basename = basename.substr(0, dotPos);
        }
        
        std::string outputFile = outputDir + basename + ".mp3";
        
        // Prüfe ob Datei bereits existiert
        if (std::filesystem::exists(outputFile)) {
            updateStatus("⏭️ Überspringe (existiert): " + entry.name, static_cast<float>(i) / total);
            continue;
        }
        
        // ffmpeg Command: Audio extrahieren, MP3 mit 320kbps
        // -n statt -y: überschreibe nicht, wenn existiert
        std::string ffmpegCmd = "ffmpeg -i \"" + entry.path + "\" -vn -ar 44100 -ac 2 -b:a 320k \"" + outputFile + "\" -n 2>/dev/null";
        
        updateStatus("🎵 Extrahiere: " + entry.name, static_cast<float>(i) / total);
        
        int result = system(ffmpegCmd.c_str());
        if (result == 0) {
            extracted++;
        }
    }
    
    fileBrowser_->deselectAll();
    updateStatus("✅ Audio extrahiert: " + std::to_string(extracted) + "/" + std::to_string(total) + " → " + outputDir, 1.0f);
}

void ImGuiRenderer::startGeneration() {
    isGenerating_ = true;
    
    std::string filename = genParams_.genre + "_" + std::to_string(static_cast<int>(genParams_.bpm)) + ".mp3";
    std::string fullPath = outputPath_ + "/" + filename;
    
    // TODO: Background-Thread
    bool success = generator_->generate(genParams_, fullPath,
        [this](const std::string& phase, float progress) {
            statusMessage_ = phase;
            progress_ = progress;
        }
    );
    
    isGenerating_ = false;
    
    if (success) {
        updateStatus("✅ Song generiert: " + filename, 1.0f);
    } else {
        updateStatus("❌ Generierung fehlgeschlagen", 0.0f);
    }
}

void ImGuiRenderer::renderMusicSorter() {
    ImGui::Begin("🎵 Musik-Sorter - Stil-Erkennung", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    
    ImGui::Text("📚 Zeigt erkannte Stile, Rhythmen und Genre-Kategorien");
    ImGui::Separator();
    
    // Statistiken
    auto allFiles = database_->getAll();
    int analyzed = 0;
    int withMood = 0;
    for (const auto& file : allFiles) {
        if (file.analyzed && !file.genre.empty()) analyzed++;
        if (!file.mood.empty()) withMood++;
    }
    
    ImGui::Text("📊 Analyse-Status:");
    ImGui::Text("  Analysiert: %d / %zu Dateien", analyzed, allFiles.size());
    float analyzedProgress = allFiles.empty() ? 0.0f : static_cast<float>(analyzed) / allFiles.size();
    ImGui::ProgressBar(analyzedProgress, ImVec2(400, 0));
    
    ImGui::Text("  Mit Stil-Info: %d / %zu Dateien", withMood, allFiles.size());
    float moodProgress = allFiles.empty() ? 0.0f : static_cast<float>(withMood) / allFiles.size();
    ImGui::ProgressBar(moodProgress, ImVec2(400, 0));
    
    ImGui::Separator();
    
    // Filter nach Style-Tags
    ImGui::Text("🔍 Filter nach Tags:");
    static char styleFilter[128] = "";
    ImGui::InputText("##stylefilter", styleFilter, sizeof(styleFilter));
    ImGui::SameLine();
    if (ImGui::Button("Suchen")) {
        std::string filter = styleFilter;
        filteredMedia_.clear();
        for (const auto& file : allFiles) {
            if (file.mood.find(filter) != std::string::npos || 
                file.genre.find(filter) != std::string::npos) {
                filteredMedia_.push_back(file);
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        filteredMedia_ = allFiles;
        styleFilter[0] = '\0';
    }
    
    ImGui::Separator();
    
    // Sortierte, abspielfertige Gruppen
    ImGui::Text("📋 Sortiert & Abspielfertig:");
    ImGui::BeginChild("CategoryPreview", ImVec2(800, 400), true);
    
    static int sortMode = 0;
    ImGui::RadioButton("Nach Genre", &sortMode, 0); ImGui::SameLine();
    ImGui::RadioButton("Nach BPM-Range", &sortMode, 1); ImGui::SameLine();
    ImGui::RadioButton("Nach Mood", &sortMode, 2);
    ImGui::Separator();
    
    if (sortMode == 0) {
        // Gruppiert nach Genre
        auto grouped = database_->getGroupedByGenre();
        for (const auto& [genre, files] : grouped) {
            if (ImGui::TreeNode((genre + " (" + std::to_string(files.size()) + ")").c_str())) {
                for (size_t i = 0; i < std::min(files.size(), size_t(5)); i++) {
                    std::filesystem::path p(files[i].filepath);
                    ImGui::BulletText("%s (%.0f BPM)", p.filename().c_str(), files[i].bpm);
                }
                if (files.size() > 5) {
                    ImGui::TextDisabled("... und %zu weitere", files.size() - 5);
                }
                ImGui::TreePop();
            }
        }
    } else if (sortMode == 1) {
        // Gruppiert nach BPM
        auto grouped = database_->getGroupedByBPMRange();
        for (const auto& [range, files] : grouped) {
            if (ImGui::TreeNode((range + " (" + std::to_string(files.size()) + ")").c_str())) {
                for (size_t i = 0; i < std::min(files.size(), size_t(5)); i++) {
                    std::filesystem::path p(files[i].filepath);
                    ImGui::BulletText("%s (%s)", p.filename().c_str(), files[i].genre.c_str());
                }
                if (files.size() > 5) {
                    ImGui::TextDisabled("... und %zu weitere", files.size() - 5);
                }
                ImGui::TreePop();
            }
        }
    } else {
        // Nach Mood sortiert
        auto moodFiles = database_->getAllSortedByMood();
        std::map<std::string, std::vector<MediaMetadata>> moodGroups;
        for (const auto& file : moodFiles) {
            // Extrahiere ersten Tag aus Mood
            size_t pos = file.mood.find('#');
            if (pos != std::string::npos) {
                size_t end = file.mood.find(' ', pos);
                std::string tag = file.mood.substr(pos, end - pos);
                moodGroups[tag].push_back(file);
            }
        }
        for (const auto& [mood, files] : moodGroups) {
            if (ImGui::TreeNode((mood + " (" + std::to_string(files.size()) + ")").c_str())) {
                for (size_t i = 0; i < std::min(files.size(), size_t(5)); i++) {
                    std::filesystem::path p(files[i].filepath);
                    ImGui::BulletText("%s", p.filename().c_str());
                }
                if (files.size() > 5) {
                    ImGui::TextDisabled("... und %zu weitere", files.size() - 5);
                }
                ImGui::TreePop();
            }
        }
    }
    
    if (allFiles.empty()) {
        ImGui::TextDisabled("Keine analysierten Dateien vorhanden.");
        ImGui::TextDisabled("Nutze den Analyse-Tab um Dateien zu analysieren.");
    }
    
    ImGui::EndChild();
    
    ImGui::Separator();
    ImGui::TextDisabled("Tipp: Nutze den Datenbank-Browser um einzelne Dateien anzuzeigen");
    
    ImGui::End();
}

void ImGuiRenderer::loadSettings() {
    settingsPath_ = std::string(getenv("HOME")) + "/.songgen/settings.txt";
    
    std::ifstream file(settingsPath_);
    if (!file.is_open()) {
        Logger::info("Keine Settings gefunden, verwende Defaults");
        return;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        size_t pos = line.find('=');
        if (pos == std::string::npos) continue;
        
        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);
        
        // Generator-Settings
        if (key == "gen_genre") genParams_.genre = value;
        else if (key == "gen_bpm") genParams_.bpm = std::stof(value);
        else if (key == "gen_intensity") genParams_.intensity = value;
        else if (key == "gen_bassLevel") genParams_.bassLevel = value;
        else if (key == "gen_duration") genParams_.duration = std::stoi(value);
        else if (key == "gen_useVocals") genParams_.useVocals = (value == "1");
        else if (key == "output_path") outputPath_ = value;
    }
    
    Logger::success("Einstellungen geladen: " + settingsPath_);
}

void ImGuiRenderer::saveSettings() {
    std::ofstream file(settingsPath_);
    if (!file.is_open()) {
        Logger::error("Konnte Settings nicht speichern: " + settingsPath_);
        return;
    }
    
    // Generator-Settings
    file << "gen_genre=" << genParams_.genre << "\n";
    file << "gen_bpm=" << genParams_.bpm << "\n";
    file << "gen_intensity=" << genParams_.intensity << "\n";
    file << "gen_bassLevel=" << genParams_.bassLevel << "\n";
    file << "gen_duration=" << genParams_.duration << "\n";
    file << "gen_useVocals=" << (genParams_.useVocals ? "1" : "0") << "\n";
    file << "output_path=" << outputPath_ << "\n";
    
    Logger::success("Einstellungen gespeichert");
}
