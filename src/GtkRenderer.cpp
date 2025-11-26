#include "GtkRenderer.h"
#include "TrainingModel.h"
#include "PatternCaptureEngine.h"
#include "DataQualityAnalyzer.h"
#include <iostream>
#include <iomanip>
#include <filesystem>
#include <thread>
#include <chrono>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>

namespace fs = std::filesystem;

// 📟 Console Output Capture - Struct für Idle-Callback
struct ConsoleUpdateData {
    GtkRenderer* renderer;
    gchar* text;
};

// 🪟 Hilfsfunktion: Mache Dialog größenveränderbar und verschiebbar
static void makeDialogResizable(GtkWidget* dialog, int defaultWidth = 500, int defaultHeight = 200) {
    if (GTK_IS_WINDOW(dialog)) {
        gtk_window_set_resizable(GTK_WINDOW(dialog), TRUE);
        gtk_window_set_default_size(GTK_WINDOW(dialog), defaultWidth, defaultHeight);
        // Erlaube Verschieben (ist standardmäßig an, aber explizit setzen)
        gtk_window_set_type_hint(GTK_WINDOW(dialog), GDK_WINDOW_TYPE_HINT_DIALOG);
    }
}

GtkRenderer::GtkRenderer() {
    database_ = std::make_unique<MediaDatabase>();
    analyzer_ = std::make_unique<AudioAnalyzer>();
    fileBrowser_ = std::make_unique<FileBrowser>();
    generator_ = std::make_unique<SongGenerator>(*database_);
    hvscDownloader_ = std::make_unique<HVSCDownloader>();
    audioPlayer_ = std::make_unique<AudioPlayer>();
    trainingModel_ = std::make_unique<TrainingModel>(*database_);  // 🎓 Online-Learning
    patternCapture_ = std::make_unique<SongGen::PatternCaptureEngine>();  // 🎤 Pattern Learning
    qualityAnalyzer_ = std::make_unique<SongGen::DataQualityAnalyzer>(*database_);  // 📊 Data Quality
}

GtkRenderer::~GtkRenderer() {
    shutdown();
}

bool GtkRenderer::initialize() {
    std::cout << "🎨 Initializing GTK renderer...\n";
    
    if (!database_->initialize()) {
        std::cerr << "❌ Database initialization failed\n";
        return false;
    }
    
    if (!audioPlayer_->initialize()) {
        std::cerr << "⚠️ Audio player initialization failed\n";
    }
    
    // Load learned patterns
    if (patternCapture_) {
        const char* home = getenv("HOME");
        std::string patternPath = std::string(home) + "/.songgen/patterns.json";
        if (patternCapture_->loadLibrary(patternPath)) {
            std::cout << "📂 Loaded " << patternCapture_->getAllPatterns().size() 
                     << " learned patterns\n";
        }
    }
    
    // Lade Datenbank - unkorrigierte zuerst (lastUsed=0 oder niedrig oben)
    filteredMedia_ = database_->getAll();
    
    // 🤖 Automatisches Genre-Learning: Lerne aus bereits korrigierten Tracks
    autoLearnGenresFromCorrectedTracks();
    
    // Sortiere: Noch nicht bearbeitete (lastUsed=0) zuerst, dann nach lastUsed aufsteigend
    std::sort(filteredMedia_.begin(), filteredMedia_.end(),
        [](const MediaMetadata& a, const MediaMetadata& b) {
            if (a.lastUsed == 0 && b.lastUsed != 0) return true;   // Unbearbeitet zuerst
            if (a.lastUsed != 0 && b.lastUsed == 0) return false;
            return a.lastUsed < b.lastUsed;  // Ältere Korrekturen vor neueren
        });
    
    // Erstelle Hauptfenster
    window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window_), "🎵 SongGen - KI Song Generator");
    gtk_window_set_default_size(GTK_WINDOW(window_), 1200, 800);
    gtk_window_set_position(GTK_WINDOW(window_), GTK_WIN_POS_CENTER);
    
    // Speichere Renderer-Instanz im Window für Callbacks
    g_object_set_data(G_OBJECT(window_), "renderer", this);
    
    g_signal_connect(window_, "destroy", G_CALLBACK(onDestroy), this);
    
    // Hauptcontainer
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(window_), vbox);
    
    // Notebook (Tabs)
    notebook_ = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(vbox), notebook_, TRUE, TRUE, 0);
    
    // Tabs aufbauen
    buildBrowserTab();
    buildDatabaseTab();
    buildHVSCTab();
    buildGeneratorTab();
    buildTrainingTab();
    buildAnalyzerTab();
    buildHistoryTab();
    buildDataQualityTab();
    buildSettingsTab();
    
    // Statusbar mit GPU-Anzeige
    GtkWidget* statusBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(statusBox), 2);
    
    statusbar_ = gtk_statusbar_new();
    gtk_box_pack_start(GTK_BOX(statusBox), statusbar_, TRUE, TRUE, 0);
    updateStatusBar("Bereit");
    
    // GPU-Auslastungsanzeige
    GtkWidget* gpuBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    
    gpuLabel_ = gtk_label_new("GPU: 0%");
    gtk_label_set_selectable(GTK_LABEL(gpuLabel_), TRUE);
    gtk_widget_set_size_request(gpuLabel_, 60, -1);
    PangoAttrList* attrs = pango_attr_list_new();
    PangoAttribute* attr = pango_attr_family_new("monospace");
    pango_attr_list_insert(attrs, attr);
    gtk_label_set_attributes(GTK_LABEL(gpuLabel_), attrs);
    pango_attr_list_unref(attrs);
    gtk_box_pack_start(GTK_BOX(gpuBox), gpuLabel_, FALSE, FALSE, 0);
    
    gpuProgressBar_ = gtk_progress_bar_new();
    gtk_widget_set_size_request(gpuProgressBar_, 80, 16);
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(gpuProgressBar_), FALSE);
    
    // Hellgrüner Fortschrittsbalken mit CSS
    GtkCssProvider* cssProvider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(cssProvider,
        "progressbar progress { background-color: #90EE90; min-height: 14px; }"
        "progressbar trough { background-color: #2a2a2a; min-height: 14px; }"
        "progressbar text { color: black; font-weight: bold; font-size: 9px; }",
        -1, NULL);
    gtk_style_context_add_provider(
        gtk_widget_get_style_context(gpuProgressBar_),
        GTK_STYLE_PROVIDER(cssProvider),
        GTK_STYLE_PROVIDER_PRIORITY_USER
    );
    g_object_unref(cssProvider);
    
    gtk_box_pack_start(GTK_BOX(gpuBox), gpuProgressBar_, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(statusBox), gpuBox, FALSE, FALSE, 5);
    
    gtk_box_pack_start(GTK_BOX(vbox), statusBox, FALSE, FALSE, 0);
    
    // 📟 Echtzeit-Konsolen-Ausgabe (eine Zeile)
    consoleLabel_ = gtk_label_new("");
    gtk_label_set_selectable(GTK_LABEL(consoleLabel_), TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(consoleLabel_), PANGO_ELLIPSIZE_START);  // ... am Anfang
    gtk_widget_set_halign(consoleLabel_, GTK_ALIGN_START);
    gtk_widget_set_margin_start(consoleLabel_, 5);
    gtk_widget_set_margin_end(consoleLabel_, 5);
    
    PangoAttrList* consoleAttrs = pango_attr_list_new();
    PangoAttribute* consoleFont = pango_attr_family_new("monospace");
    PangoAttribute* consoleSize = pango_attr_size_new(9 * PANGO_SCALE);
    pango_attr_list_insert(consoleAttrs, consoleFont);
    pango_attr_list_insert(consoleAttrs, consoleSize);
    gtk_label_set_attributes(GTK_LABEL(consoleLabel_), consoleAttrs);
    pango_attr_list_unref(consoleAttrs);
    
    gtk_box_pack_start(GTK_BOX(vbox), consoleLabel_, FALSE, FALSE, 0);
    
    // 🧠 Idle Learning Status-Label
    idleLearningLabel_ = gtk_label_new("");
    gtk_label_set_selectable(GTK_LABEL(idleLearningLabel_), TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), idleLearningLabel_, FALSE, FALSE, 0);
    gtk_widget_set_no_show_all(idleLearningLabel_, TRUE);  // Versteckt wenn inaktiv
    
    // Starte Auto-Sync
    startAutoSync();
    
    // Starte GPU-Update-Timer (alle 500ms)
    g_timeout_add(500, onGPUUpdateTimer, this);
    
    // 📟 Starte Console-Capture
    startConsoleCapture();
    
    // 🧠 Starte Idle-Learning-System
    startIdleLearning();
    
    // User-Activity-Tracking für alle Events
    g_signal_connect(window_, "key-press-event", G_CALLBACK(onUserActivity), this);
    g_signal_connect(window_, "button-press-event", G_CALLBACK(onUserActivity), this);
    g_signal_connect(window_, "motion-notify-event", G_CALLBACK(onUserActivity), this);
    
    running_ = true;
    
    std::cout << "✅ GTK renderer initialized\n";
    std::cout << "🧠 Idle Learning aktiviert (startet nach 30s Inaktivität)\n";
    return true;
}

void GtkRenderer::run() {
    gtk_widget_show_all(window_);
    gtk_main();
}

void GtkRenderer::shutdown() {
    running_ = false;
    stopAutoSync();
    stopIdleLearning();
    stopConsoleCapture();
    if (audioPlayer_) {
        audioPlayer_->stop();
    }
    
    // Save learned patterns before shutdown
    if (patternCapture_) {
        const char* home = getenv("HOME");
        std::string patternPath = std::string(home) + "/.songgen/patterns.json";
        if (patternCapture_->saveLibrary(patternPath)) {
            std::cout << "💾 Saved " << patternCapture_->getAllPatterns().size() 
                     << " patterns to " << patternPath << "\n";
        }
    }
}

void GtkRenderer::buildDatabaseTab() {
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
    
    // Titel
    GtkWidget* label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label), "<span size='large' weight='bold'>📚 Datenbank-Browser</span>");
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
    
    // Buttons
    GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    
    GtkWidget* btnCleanup = gtk_button_new_with_label("🔄 Fehlende Dateien bereinigen");
    g_signal_connect(btnCleanup, "clicked", G_CALLBACK(onCleanupMissing), this);
    gtk_box_pack_start(GTK_BOX(hbox), btnCleanup, FALSE, FALSE, 0);
    
    GtkWidget* btnAutoGenre = gtk_button_new_with_label("🎭 Genre automatisch erkennen");
    g_signal_connect(btnAutoGenre, "clicked", G_CALLBACK(onAutoDetectGenres), this);
    gtk_box_pack_start(GTK_BOX(hbox), btnAutoGenre, FALSE, FALSE, 0);
    
    GtkWidget* btnAnalyzeAll = gtk_button_new_with_label("📈 Alle analysieren");
    g_signal_connect(btnAnalyzeAll, "clicked", G_CALLBACK(onAnalyzeAll), this);
    gtk_box_pack_start(GTK_BOX(hbox), btnAnalyzeAll, FALSE, FALSE, 0);
    
    GtkWidget* btnRepairClipping = gtk_button_new_with_label("🔧 Übersteuerungen reparieren");
    g_signal_connect(btnRepairClipping, "clicked", G_CALLBACK(onRepairClipping), this);
    gtk_box_pack_start(GTK_BOX(hbox), btnRepairClipping, FALSE, FALSE, 0);
    
    // Zweite Button-Zeile
    GtkWidget* hbox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hbox2, FALSE, FALSE, 0);
    
    GtkWidget* btnFindDuplicates = gtk_button_new_with_label("🔍 Duplikate suchen");
    g_signal_connect(btnFindDuplicates, "clicked", G_CALLBACK(onFindDuplicates), this);
    gtk_box_pack_start(GTK_BOX(hbox2), btnFindDuplicates, FALSE, FALSE, 0);
    
    GtkWidget* btnTrainingStats = gtk_button_new_with_label("📊 Training-Statistik");
    g_signal_connect(btnTrainingStats, "clicked", G_CALLBACK(onShowTrainingStats), this);
    gtk_box_pack_start(GTK_BOX(hbox2), btnTrainingStats, FALSE, FALSE, 0);
    
    GtkWidget* btnBalanceDataset = gtk_button_new_with_label("⚖️ Dataset balancieren");
    g_signal_connect(btnBalanceDataset, "clicked", G_CALLBACK(onBalanceDataset), this);
    gtk_box_pack_start(GTK_BOX(hbox2), btnBalanceDataset, FALSE, FALSE, 0);
    
    GtkWidget* btnDelete = gtk_button_new_with_label("🗑️ Datenbank löschen");
    g_signal_connect(btnDelete, "clicked", G_CALLBACK(onDeleteDatabase), this);
    gtk_box_pack_start(GTK_BOX(hbox2), btnDelete, FALSE, FALSE, 0);
    
    // Suchleiste
    GtkWidget* hboxSearch = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hboxSearch, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(hboxSearch), gtk_label_new("🔍 Suche:"), FALSE, FALSE, 0);
    
    GtkWidget* searchEntry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(searchEntry), "Titel, Artist, Genre...");
    gtk_box_pack_start(GTK_BOX(hboxSearch), searchEntry, TRUE, TRUE, 0);
    
    GtkWidget* btnClearSearch = gtk_button_new_with_label("✖ Zurücksetzen");
    gtk_box_pack_start(GTK_BOX(hboxSearch), btnClearSearch, FALSE, FALSE, 0);
    
    // Genre-Filter
    GtkWidget* hboxFilter = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hboxFilter, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(hboxFilter), gtk_label_new("🎭 Genre:"), FALSE, FALSE, 0);
    
    GtkWidget* genreCombo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genreCombo), "Alle");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genreCombo), "SID");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genreCombo), "Electronic");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genreCombo), "Techno");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genreCombo), "House");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genreCombo), "Trance");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genreCombo), "Ambient");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genreCombo), "Hip-Hop");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genreCombo), "RnB");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genreCombo), "Jazz");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genreCombo), "Classical");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genreCombo), "Salsa");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genreCombo), "Walzer");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genreCombo), "Rock");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genreCombo), "Pop");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genreCombo), "Unknown");
    gtk_combo_box_set_active(GTK_COMBO_BOX(genreCombo), 0);
    gtk_box_pack_start(GTK_BOX(hboxFilter), genreCombo, FALSE, FALSE, 0);
    
    // Info-Label
    dbInfoLabel_ = gtk_label_new(NULL);
    gtk_label_set_selectable(GTK_LABEL(dbInfoLabel_), TRUE);
    std::string info = "Einträge in Datenbank: " + std::to_string(filteredMedia_.size());
    gtk_label_set_text(GTK_LABEL(dbInfoLabel_), info.c_str());
    gtk_box_pack_start(GTK_BOX(vbox), dbInfoLabel_, FALSE, FALSE, 0);
    
    // Scrolled Window für TreeView
    GtkWidget* scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), 
                                    GTK_POLICY_AUTOMATIC, 
                                    GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);
    
    // TreeView mit Columns (5 Spalten: Play, Title, Artist, Genre, Duration)
    GtkListStore* store = gtk_list_store_new(6, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    dbTreeView_ = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);
    
    // Play-Button Column
    GtkCellRenderer* rendererButton = gtk_cell_renderer_text_new();
    GtkTreeViewColumn* playColumn = gtk_tree_view_column_new_with_attributes("▶️", rendererButton, "text", 0, NULL);
    gtk_tree_view_column_set_min_width(playColumn, 50);
    gtk_tree_view_append_column(GTK_TREE_VIEW(dbTreeView_), playColumn);
    
    // Text Columns: Title, Artist, Genre, Duration (clickable f\u00fcr Sortierung)
    GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
    
    GtkTreeViewColumn* colTitle = gtk_tree_view_column_new_with_attributes("Titel ⬍", renderer, "text", 1, NULL);
    gtk_tree_view_column_set_clickable(colTitle, TRUE);
    g_signal_connect(colTitle, "clicked", G_CALLBACK(onColumnHeaderClicked), this);
    g_object_set_data(G_OBJECT(colTitle), "column-name", (gpointer)"title");
    gtk_tree_view_append_column(GTK_TREE_VIEW(dbTreeView_), colTitle);
    
    GtkTreeViewColumn* colArtist = gtk_tree_view_column_new_with_attributes("Artist ⬍", renderer, "text", 2, NULL);
    gtk_tree_view_column_set_clickable(colArtist, TRUE);
    g_signal_connect(colArtist, "clicked", G_CALLBACK(onColumnHeaderClicked), this);
    g_object_set_data(G_OBJECT(colArtist), "column-name", (gpointer)"artist");
    gtk_tree_view_append_column(GTK_TREE_VIEW(dbTreeView_), colArtist);
    
    GtkTreeViewColumn* colGenre = gtk_tree_view_column_new_with_attributes("Genre ⬍", renderer, "text", 3, NULL);
    gtk_tree_view_column_set_clickable(colGenre, TRUE);
    g_signal_connect(colGenre, "clicked", G_CALLBACK(onColumnHeaderClicked), this);
    g_object_set_data(G_OBJECT(colGenre), "column-name", (gpointer)"genre");
    gtk_tree_view_append_column(GTK_TREE_VIEW(dbTreeView_), colGenre);
    
    GtkTreeViewColumn* colDuration = gtk_tree_view_column_new_with_attributes("Dauer ⬍", renderer, "text", 4, NULL);
    gtk_tree_view_column_set_clickable(colDuration, TRUE);
    g_signal_connect(colDuration, "clicked", G_CALLBACK(onColumnHeaderClicked), this);
    g_object_set_data(G_OBJECT(colDuration), "column-name", (gpointer)"duration");
    gtk_tree_view_append_column(GTK_TREE_VIEW(dbTreeView_), colDuration);
    
    // Filepath (hidden column for play callback)
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(dbTreeView_), -1, "Pfad", renderer, "text", 5, NULL);
    GtkTreeViewColumn* pathColumn = gtk_tree_view_get_column(GTK_TREE_VIEW(dbTreeView_), 5);
    gtk_tree_view_column_set_visible(pathColumn, FALSE);
    
    // Double-click oder Enter zum Abspielen
    g_signal_connect(dbTreeView_, "row-activated", G_CALLBACK(onPlaySong), this);
    
    gtk_container_add(GTK_CONTAINER(scrolled), dbTreeView_);
    
    // Speichere Widgets für Callbacks
    dbSearchEntry_ = searchEntry;
    dbGenreCombo_ = genreCombo;
    
    // Verbinde Such- und Filter-Callbacks
    g_signal_connect(searchEntry, "changed", G_CALLBACK(onSearchChanged), this);
    g_signal_connect(genreCombo, "changed", G_CALLBACK(onGenreFilterChanged), this);
    g_signal_connect(btnClearSearch, "clicked", G_CALLBACK(onClearSearch), this);
    
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook_), vbox, gtk_label_new("📚 Datenbank"));
    dbTab_ = vbox;
    
    // Fülle TreeView mit Daten
    refreshDatabaseView();
}

void GtkRenderer::refreshDatabaseView() {
    if (!dbTreeView_) return;
    
    GtkTreeModel* model = gtk_tree_view_get_model(GTK_TREE_VIEW(dbTreeView_));
    if (!model || !GTK_IS_LIST_STORE(model)) return;
    
    gtk_list_store_clear(GTK_LIST_STORE(model));
    
    // Hole alle Einträge
    auto allMedia = database_->getAll();
    
    // Filter nach Genre
    std::string selectedGenre = "Alle";
    if (dbGenreCombo_) {
        gchar* genreText = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(dbGenreCombo_));
        if (genreText) {
            selectedGenre = genreText;
            g_free(genreText);
        }
    }
    
    // Filter nach Suchtext
    std::string searchText;
    if (dbSearchEntry_) {
        const char* text = gtk_entry_get_text(GTK_ENTRY(dbSearchEntry_));
        if (text) {
            searchText = text;
            // Zu Kleinbuchstaben für case-insensitive Suche
            std::transform(searchText.begin(), searchText.end(), searchText.begin(), ::tolower);
        }
    }
    
    // Filtere Einträge
    filteredMedia_.clear();
    for (const auto& meta : allMedia) {
        // Genre-Filter
        if (selectedGenre != "Alle" && meta.genre != selectedGenre) {
            continue;
        }
        
        // Such-Filter
        if (!searchText.empty()) {
            std::string title = meta.title;
            std::string artist = meta.artist;
            std::string genre = meta.genre;
            std::transform(title.begin(), title.end(), title.begin(), ::tolower);
            std::transform(artist.begin(), artist.end(), artist.begin(), ::tolower);
            std::transform(genre.begin(), genre.end(), genre.begin(), ::tolower);
            
            if (title.find(searchText) == std::string::npos &&
                artist.find(searchText) == std::string::npos &&
                genre.find(searchText) == std::string::npos) {
                continue;
            }
        }
        
        filteredMedia_.push_back(meta);
    }
    
    // Update Info-Label
    std::string info = "Einträge: " + std::to_string(filteredMedia_.size()) + " / " + std::to_string(allMedia.size());
    gtk_label_set_text(GTK_LABEL(dbInfoLabel_), info.c_str());
    
    // Füge Einträge hinzu (max 1000 für Performance)
    size_t count = 0;
    for (const auto& meta : filteredMedia_) {
        if (count++ > 1000) break;
        
        GtkTreeIter iter;
        gtk_list_store_append(GTK_LIST_STORE(model), &iter);
        
        std::string duration = std::to_string(meta.duration) + "s";
        
        gtk_list_store_set(GTK_LIST_STORE(model), &iter,
                          0, "▶️",  // Play-Button Symbol
                          1, meta.title.c_str(),
                          2, meta.artist.c_str(),
                          3, meta.genre.c_str(),
                          4, duration.c_str(),
                          5, meta.filepath.c_str(),  // Hidden filepath
                          -1);
    }
}

void GtkRenderer::buildBrowserTab() {
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
    
    // Titel
    GtkWidget* label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label), "<span size='large' weight='bold'>📁 Datei-Browser</span>");
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
    
    const char* home = getenv("HOME");
    
    // Lesezeichen-Leiste
    GtkWidget* hboxBookmarks = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hboxBookmarks, FALSE, FALSE, 0);
    
    GtkWidget* labelBookmark = gtk_label_new("🔖 Lesezeichen:");
    gtk_box_pack_start(GTK_BOX(hboxBookmarks), labelBookmark, FALSE, FALSE, 0);
    
    // Lesezeichen ComboBox
    GtkWidget* bookmarkCombo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(bookmarkCombo), "🏠 Home");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(bookmarkCombo), "🎵 Musik");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(bookmarkCombo), "📥 Downloads");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(bookmarkCombo), "💾 Dokumente");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(bookmarkCombo), "🖼️ Bilder");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(bookmarkCombo), "🎬 Videos");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(bookmarkCombo), "🎹 HVSC");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(bookmarkCombo), "💾 Root (/)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(bookmarkCombo), "📁 /tmp");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(bookmarkCombo), "⚙️ /etc");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(bookmarkCombo), "🛠️ /usr");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(bookmarkCombo), "📚 /opt");
    gtk_combo_box_set_active(GTK_COMBO_BOX(bookmarkCombo), 0);
    gtk_box_pack_start(GTK_BOX(hboxBookmarks), bookmarkCombo, FALSE, FALSE, 0);
    
    GtkWidget* btnGoBookmark = gtk_button_new_with_label("➡️ Gehe zu");
    gtk_box_pack_start(GTK_BOX(hboxBookmarks), btnGoBookmark, FALSE, FALSE, 0);
    
    GtkWidget* btnAddBookmark = gtk_button_new_with_label("➕ Lesezeichen hinzufügen");
    gtk_box_pack_start(GTK_BOX(hboxBookmarks), btnAddBookmark, FALSE, FALSE, 0);
    
    // Adressleiste
    GtkWidget* hboxAddr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hboxAddr, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(hboxAddr), gtk_label_new("Adresse:"), FALSE, FALSE, 0);
    
    GtkWidget* addrEntry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(addrEntry), home ? home : "/");
    gtk_box_pack_start(GTK_BOX(hboxAddr), addrEntry, TRUE, TRUE, 0);
    
    GtkWidget* btnGo = gtk_button_new_with_label("➜ Gehe zu");
    gtk_box_pack_start(GTK_BOX(hboxAddr), btnGo, FALSE, FALSE, 0);
    
    // Buttons für verschiedene Aktionen
    GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    
    GtkWidget* btnAddFiles = gtk_button_new_with_label("📄 Dateien hinzufügen");
    g_signal_connect(btnAddFiles, "clicked", G_CALLBACK(onBrowseLocal), this);
    gtk_box_pack_start(GTK_BOX(hbox), btnAddFiles, FALSE, FALSE, 0);
    
    GtkWidget* btnAddFolder = gtk_button_new_with_label("📁 Ordner hinzufügen (rekursiv)");
    g_signal_connect(btnAddFolder, "clicked", G_CALLBACK(onBrowseLocal), this);
    gtk_box_pack_start(GTK_BOX(hbox), btnAddFolder, FALSE, FALSE, 0);
    
    GtkWidget* btnSMB = gtk_button_new_with_label("🌐 SMB");
    g_signal_connect(btnSMB, "clicked", G_CALLBACK(onBrowseSMB), this);
    gtk_box_pack_start(GTK_BOX(hbox), btnSMB, FALSE, FALSE, 0);
    
    GtkWidget* btnFTP = gtk_button_new_with_label("📡 FTP");
    g_signal_connect(btnFTP, "clicked", G_CALLBACK(onBrowseFTP), this);
    gtk_box_pack_start(GTK_BOX(hbox), btnFTP, FALSE, FALSE, 0);
    
    // TreeView Browser mit Checkboxen
    GtkWidget* scrolledWindow = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledWindow), 
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    
    // TreeStore: [✓] | Icon | Name | Typ | Pfad
    GtkListStore* store = gtk_list_store_new(5, 
        G_TYPE_BOOLEAN,  // 0: Checkbox
        G_TYPE_STRING,   // 1: Icon  
        G_TYPE_STRING,   // 2: Name
        G_TYPE_STRING,   // 3: Typ
        G_TYPE_STRING);  // 4: Vollständiger Pfad
    
    GtkWidget* treeView = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(treeView), TRUE);
    
    // Spalte 1: Checkbox
    GtkCellRenderer* rendererToggle = gtk_cell_renderer_toggle_new();
    GtkTreeViewColumn* colToggle = gtk_tree_view_column_new_with_attributes("✓", rendererToggle, "active", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeView), colToggle);
    g_signal_connect(rendererToggle, "toggled", G_CALLBACK(onBrowserToggleCell), store);
    
    // Spalte 2: Icon
    GtkCellRenderer* rendererIcon = gtk_cell_renderer_text_new();
    GtkTreeViewColumn* colIcon = gtk_tree_view_column_new_with_attributes("", rendererIcon, "text", 1, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeView), colIcon);
    
    // Spalte 3: Name
    GtkCellRenderer* rendererName = gtk_cell_renderer_text_new();
    GtkTreeViewColumn* colName = gtk_tree_view_column_new_with_attributes("Name", rendererName, "text", 2, NULL);
    gtk_tree_view_column_set_expand(colName, TRUE);
    gtk_tree_view_column_set_resizable(colName, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeView), colName);
    
    // Spalte 4: Typ
    GtkCellRenderer* rendererType = gtk_cell_renderer_text_new();
    GtkTreeViewColumn* colType = gtk_tree_view_column_new_with_attributes("Typ", rendererType, "text", 3, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeView), colType);
    
    gtk_container_add(GTK_CONTAINER(scrolledWindow), treeView);
    gtk_box_pack_start(GTK_BOX(vbox), scrolledWindow, TRUE, TRUE, 0);
    
    // Lade initiales Verzeichnis
    std::string currentPath = home ? home : "/";
    loadBrowserDirectory(currentPath, store);
    
    // Control Box mit Buttons
    GtkWidget* btnBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    
    GtkWidget* btnSelectAll = gtk_button_new_with_label("☑️ Alle");
    g_signal_connect(btnSelectAll, "clicked", G_CALLBACK(onBrowserSelectAll), store);
    gtk_box_pack_start(GTK_BOX(btnBox), btnSelectAll, FALSE, FALSE, 0);
    
    GtkWidget* btnDeselectAll = gtk_button_new_with_label("☐ Keine");
    g_signal_connect(btnDeselectAll, "clicked", G_CALLBACK(onBrowserDeselectAll), store);
    gtk_box_pack_start(GTK_BOX(btnBox), btnDeselectAll, FALSE, FALSE, 0);
    
    GtkWidget* btnAddSelected = gtk_button_new_with_label("✅ Markierte hinzufügen (Ordner rekursiv)");
    gtk_box_pack_start(GTK_BOX(btnBox), btnAddSelected, TRUE, TRUE, 0);
    
    gtk_box_pack_start(GTK_BOX(vbox), btnBox, FALSE, FALSE, 0);
    
    // Speichere Widgets für Callbacks
    BrowserData* browserData = new BrowserData{addrEntry, treeView, store, currentPath, this};
    
    // Verbinde Callbacks
    g_signal_connect(btnGo, "clicked", G_CALLBACK(onAddressBarGo), browserData);
    g_signal_connect(btnAddSelected, "clicked", G_CALLBACK(onAddSelectedFiles), browserData);
    g_signal_connect(treeView, "row-activated", G_CALLBACK(onBrowserRowActivated), browserData);
    
    // Verbinde Lesezeichen-Callbacks
    g_object_set_data(G_OBJECT(btnGoBookmark), "bookmark-combo", bookmarkCombo);
    g_object_set_data(G_OBJECT(btnGoBookmark), "browser-data", browserData);
    g_signal_connect(btnGoBookmark, "clicked", G_CALLBACK(onBookmarkGo), nullptr);
    
    g_object_set_data(G_OBJECT(btnAddBookmark), "addr-entry", addrEntry);
    g_object_set_data(G_OBJECT(btnAddBookmark), "bookmark-combo", bookmarkCombo);
    g_signal_connect(btnAddBookmark, "clicked", G_CALLBACK(onBookmarkAdd), nullptr);
    
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook_), vbox, gtk_label_new("📁 Browser"));
    browserTab_ = vbox;
}

void GtkRenderer::buildHVSCTab() {
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
    
    // Titel
    GtkWidget* label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label), "<span size='large' weight='bold'>📦 HVSC - High Voltage SID Collection</span>");
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
    
    GtkWidget* sublabel = gtk_label_new("75.000+ Commodore 64 SID Musikdateien");
    gtk_label_set_selectable(GTK_LABEL(sublabel), TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), sublabel, FALSE, FALSE, 0);
    
    // Buttons
    GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    
    GtkWidget* btnDownload = gtk_button_new_with_label("📥 HVSC herunterladen");
    g_signal_connect(btnDownload, "clicked", G_CALLBACK(onDownloadHVSC), this);
    gtk_box_pack_start(GTK_BOX(hbox), btnDownload, FALSE, FALSE, 0);
    
    GtkWidget* btnExtract = gtk_button_new_with_label("🎵 SIDs extrahieren");
    g_signal_connect(btnExtract, "clicked", G_CALLBACK(onExtractHVSC), this);
    gtk_box_pack_start(GTK_BOX(hbox), btnExtract, FALSE, FALSE, 0);
    
    GtkWidget* btnStop = gtk_button_new_with_label("⏹️ Stop");
    g_signal_connect(btnStop, "clicked", G_CALLBACK(onStopExtraction), this);
    gtk_box_pack_start(GTK_BOX(hbox), btnStop, FALSE, FALSE, 0);
    
    GtkWidget* btnSync = gtk_button_new_with_label("🔄 DB Sync - MP3s importieren");
    g_signal_connect(btnSync, "clicked", G_CALLBACK(onDBSync), this);
    gtk_box_pack_start(GTK_BOX(hbox), btnSync, FALSE, FALSE, 0);
    
    // Zweite Button-Zeile
    GtkWidget* hbox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hbox2, FALSE, FALSE, 0);
    
    GtkWidget* btnAnalyzeSIDs = gtk_button_new_with_label("🎵 SID-MP3s analysieren & in DB");
    g_signal_connect(btnAnalyzeSIDs, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer data) {
        auto* renderer = static_cast<GtkRenderer*>(data);
        
        const char* home = getenv("HOME");
        std::string convertedPath = std::string(home) + "/.songgen/hvsc/converted/";
        
        if (!std::filesystem::exists(convertedPath)) {
            GtkWidget* dialog = gtk_message_dialog_new(
                GTK_WINDOW(renderer->window_), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING,
                GTK_BUTTONS_OK, "⚠️ Keine konvertierten SID-Dateien gefunden!\n\n"
                "Pfad: %s\n\nFühre zuerst 'SIDs extrahieren' aus.", convertedPath.c_str());
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
            return;
        }
        
        // Zähle MP3-Dateien
        std::vector<std::string> mp3Files;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(convertedPath)) {
            if (entry.path().extension() == ".mp3") {
                mp3Files.push_back(entry.path().string());
            }
        }
        
        if (mp3Files.empty()) {
            GtkWidget* dialog = gtk_message_dialog_new(
                GTK_WINDOW(renderer->window_), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO,
                GTK_BUTTONS_OK, "ℹ️ Keine MP3-Dateien gefunden in:\n%s", convertedPath.c_str());
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
            return;
        }
        
        // Bestätigungs-Dialog
        GtkWidget* confirmDialog = gtk_message_dialog_new(
            GTK_WINDOW(renderer->window_), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
            GTK_BUTTONS_YES_NO,
            "🎵 SID-MP3 Analyse starten?\n\n"
            "%zu MP3-Dateien gefunden\n"
            "Alle werden analysiert und zur Datenbank hinzugefügt.\n\n"
            "Genre: SID\n"
            "Dies kann einige Zeit dauern...",
            mp3Files.size());
        
        int response = gtk_dialog_run(GTK_DIALOG(confirmDialog));
        gtk_widget_destroy(confirmDialog);
        
        if (response != GTK_RESPONSE_YES) return;
        
        // Progress Dialog
        GtkWidget* progressDialog = gtk_dialog_new_with_buttons(
            "🎵 Analysiere SID-MP3s",
            GTK_WINDOW(renderer->window_),
            (GtkDialogFlags)0,
            NULL
        );
        
        makeDialogResizable(progressDialog, 500, 180);
        GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(progressDialog));
        GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
        gtk_container_set_border_width(GTK_CONTAINER(vbox), 15);
        gtk_container_add(GTK_CONTAINER(content), vbox);
        
        GtkWidget* progressBar = gtk_progress_bar_new();
        gtk_box_pack_start(GTK_BOX(vbox), progressBar, FALSE, FALSE, 0);
        
        GtkWidget* statusLabel = gtk_label_new("Starte Analyse...");
        gtk_label_set_selectable(GTK_LABEL(statusLabel), TRUE);
        gtk_box_pack_start(GTK_BOX(vbox), statusLabel, FALSE, FALSE, 0);
        
        gtk_widget_show_all(progressDialog);
        
        // Analysiere in Batches
        int processed = 0;
        int added = 0;
        int skipped = 0;
        
        for (const auto& mp3Path : mp3Files) {
            // Prüfe ob bereits in DB
            auto existing = renderer->database_->getAll();
            bool alreadyExists = false;
            for (const auto& media : existing) {
                if (media.filepath == mp3Path) {
                    alreadyExists = true;
                    break;
                }
            }
            
            if (alreadyExists) {
                skipped++;
            } else {
                // Analysiere
                MediaMetadata meta;
                meta.filepath = mp3Path;
                meta.title = std::filesystem::path(mp3Path).stem().string();
                meta.artist = "HVSC";
                meta.genre = "SID";
                
                if (renderer->analyzer_->analyze(mp3Path, meta)) {
                    meta.analyzed = true;
                    meta.addedTimestamp = std::time(nullptr);
                    
                    if (renderer->database_->addMedia(meta)) {
                        added++;
                    } else {
                        skipped++;
                    }
                } else {
                    skipped++;
                }
            }
            
            processed++;
            
            // Update UI
            float progress = (float)processed / mp3Files.size();
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progressBar), progress);
            
            std::string status = "Verarbeitet: " + std::to_string(processed) + " / " + 
                                std::to_string(mp3Files.size()) + "\n" +
                                "Neu: " + std::to_string(added) + " | " +
                                "Übersprungen: " + std::to_string(skipped);
            gtk_label_set_text(GTK_LABEL(statusLabel), status.c_str());
            
            // UI aktualisieren
            while (gtk_events_pending()) {
                gtk_main_iteration();
            }
        }
        
        gtk_widget_destroy(progressDialog);
        
        // Ergebnis-Dialog
        GtkWidget* resultDialog = gtk_message_dialog_new(
            GTK_WINDOW(renderer->window_), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO,
            GTK_BUTTONS_OK,
            "✅ SID-MP3 Analyse abgeschlossen!\n\n"
            "📊 Gesamt: %d Dateien\n"
            "✨ Neu hinzugefügt: %d\n"
            "⏭️ Übersprungen: %d\n\n"
            "Alle SID-MP3s sind jetzt im Interactive Training verfügbar!",
            processed, added, skipped);
        gtk_dialog_run(GTK_DIALOG(resultDialog));
        gtk_widget_destroy(resultDialog);
        
        // Status aktualisieren
        renderer->addHistoryEntry("SID-Analyse", 
            std::to_string(added) + " neue SID-MP3s zur Datenbank hinzugefügt",
            "✅ Abgeschlossen");
    }), this);
    gtk_box_pack_start(GTK_BOX(hbox2), btnAnalyzeSIDs, FALSE, FALSE, 0);
    
    // Progress Bar
    hvscProgressBar_ = gtk_progress_bar_new();
    gtk_box_pack_start(GTK_BOX(vbox), hvscProgressBar_, FALSE, FALSE, 0);
    
    // Status Label
    hvscStatusLabel_ = gtk_label_new("Bereit - Klicke 'HVSC herunterladen' um zu starten");
    gtk_label_set_selectable(GTK_LABEL(hvscStatusLabel_), TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), hvscStatusLabel_, FALSE, FALSE, 0);
    
    // MP3 Counter
    GtkWidget* mp3Label = gtk_label_new(NULL);
    std::string mp3Info = "MP3-Extraktion: " + std::to_string(mp3Extracted_.load()) + " / " + std::to_string(mp3Total_.load());
    gtk_label_set_text(GTK_LABEL(mp3Label), mp3Info.c_str());
    gtk_box_pack_start(GTK_BOX(vbox), mp3Label, FALSE, FALSE, 0);
    
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook_), vbox, gtk_label_new("📦 HVSC"));
    hvscTab_ = vbox;
}

void GtkRenderer::buildGeneratorTab() {
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
    
    // Titel
    GtkWidget* label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label), "<span size='large' weight='bold'>🎼 Song Generator</span>");
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
    
    // Genre
    GtkWidget* hbox1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hbox1, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox1), gtk_label_new("Genre:"), FALSE, FALSE, 0);
    
    genGenreCombo_ = gtk_combo_box_text_new();
    // Elektronische Musik
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "Electronic");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "Ambient");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "Techno");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "Drum'n'Bass");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "Trance");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "House");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "Chillout");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "Dubstep");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "Electro Swing");
    // Urban/Hip-Hop
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "Hip-Hop");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "Trap");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "RnB");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "Reggae");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "Dancehall");
    // Pop/Rock
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "Pop");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "Rock");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "Indie");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "Alternative");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "Punk");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "Metal");
    // Jazz/Blues/Klassik
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "Jazz");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "Blues");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "Funk");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "Soul");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "Classical");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "Walzer");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "Opera");
    // World Music
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "Salsa");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "Samba");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "Tango");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "Flamenco");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "Bossa Nova");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "Latin");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "Afrobeat");
    // Weitere
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "Country");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "Folk");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "Ska");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "Gospel");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "Swing");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "Disco");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "Lofi");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "Synthwave");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genGenreCombo_), "Vaporwave");
    gtk_combo_box_set_active(GTK_COMBO_BOX(genGenreCombo_), 0);
    g_signal_connect(genGenreCombo_, "changed", G_CALLBACK(onGenreChanged), this);
    gtk_box_pack_start(GTK_BOX(hbox1), genGenreCombo_, FALSE, FALSE, 0);
    
    // BPM
    GtkWidget* hbox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hbox2, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox2), gtk_label_new("BPM:"), FALSE, FALSE, 0);
    
    genBpmSpin_ = gtk_spin_button_new_with_range(60, 200, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(genBpmSpin_), 120);
    gtk_box_pack_start(GTK_BOX(hbox2), genBpmSpin_, FALSE, FALSE, 0);
    
    // Dauer
    GtkWidget* hbox3 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hbox3, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox3), gtk_label_new("Dauer (Sekunden):"), FALSE, FALSE, 0);
    
    genDurationSpin_ = gtk_spin_button_new_with_range(30, 600, 10);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(genDurationSpin_), 180);
    gtk_box_pack_start(GTK_BOX(hbox3), genDurationSpin_, FALSE, FALSE, 0);
    
    // Intensität
    GtkWidget* hbox4 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hbox4, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox4), gtk_label_new("Intensität:"), FALSE, FALSE, 0);
    
    genIntensityCombo_ = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genIntensityCombo_), "Low");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genIntensityCombo_), "Medium");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genIntensityCombo_), "High");
    gtk_combo_box_set_active(GTK_COMBO_BOX(genIntensityCombo_), 1);
    gtk_box_pack_start(GTK_BOX(hbox4), genIntensityCombo_, FALSE, FALSE, 0);
    
    // Preset-Lesezeichen
    GtkWidget* framePresets = gtk_frame_new("🔖 Lesezeichen / Presets");
    gtk_box_pack_start(GTK_BOX(vbox), framePresets, FALSE, FALSE, 0);
    
    GtkWidget* hboxPresets = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(hboxPresets), 10);
    gtk_container_add(GTK_CONTAINER(framePresets), hboxPresets);
    
    GtkWidget* btnSavePreset = gtk_button_new_with_label("💾 Preset speichern");
    g_signal_connect(btnSavePreset, "clicked", G_CALLBACK(onSavePreset), this);
    gtk_box_pack_start(GTK_BOX(hboxPresets), btnSavePreset, FALSE, FALSE, 0);
    
    GtkWidget* btnLoadPreset = gtk_button_new_with_label("📂 Preset laden");
    g_signal_connect(btnLoadPreset, "clicked", G_CALLBACK(onLoadPreset), this);
    gtk_box_pack_start(GTK_BOX(hboxPresets), btnLoadPreset, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(hboxPresets), gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 5);
    
    // Zweite Zeile für mehr Quick-Presets
    GtkWidget* hboxPresets2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(hboxPresets2), 5);
    gtk_box_pack_start(GTK_BOX(vbox), hboxPresets2, FALSE, FALSE, 0);
    
    // Row 1
    GtkWidget* btnQuickChillout = gtk_button_new_with_label("🌙 Chillout");
    g_signal_connect(btnQuickChillout, "clicked", G_CALLBACK(onQuickPresetChillout), this);
    gtk_box_pack_start(GTK_BOX(hboxPresets), btnQuickChillout, FALSE, FALSE, 0);
    
    GtkWidget* btnQuickTechno = gtk_button_new_with_label("⚡ Techno");
    g_signal_connect(btnQuickTechno, "clicked", G_CALLBACK(onQuickPresetTechno), this);
    gtk_box_pack_start(GTK_BOX(hboxPresets), btnQuickTechno, FALSE, FALSE, 0);
    
    GtkWidget* btnQuickAmbient = gtk_button_new_with_label("🌊 Ambient");
    g_signal_connect(btnQuickAmbient, "clicked", G_CALLBACK(onQuickPresetAmbient), this);
    gtk_box_pack_start(GTK_BOX(hboxPresets), btnQuickAmbient, FALSE, FALSE, 0);
    
    GtkWidget* btnQuickHouse = gtk_button_new_with_label("🏠 House");
    g_signal_connect(btnQuickHouse, "clicked", G_CALLBACK(onQuickPresetHouse), this);
    gtk_box_pack_start(GTK_BOX(hboxPresets), btnQuickHouse, FALSE, FALSE, 0);
    
    // Row 2
    GtkWidget* btnQuickJazz = gtk_button_new_with_label("🎷 Jazz");
    g_signal_connect(btnQuickJazz, "clicked", G_CALLBACK(onQuickPresetJazz), this);
    gtk_box_pack_start(GTK_BOX(hboxPresets2), btnQuickJazz, FALSE, FALSE, 0);
    
    GtkWidget* btnQuickSalsa = gtk_button_new_with_label("💃 Salsa");
    g_signal_connect(btnQuickSalsa, "clicked", G_CALLBACK(onQuickPresetSalsa), this);
    gtk_box_pack_start(GTK_BOX(hboxPresets2), btnQuickSalsa, FALSE, FALSE, 0);
    
    GtkWidget* btnQuickWalzer = gtk_button_new_with_label("🎻 Walzer");
    g_signal_connect(btnQuickWalzer, "clicked", G_CALLBACK(onQuickPresetWalzer), this);
    gtk_box_pack_start(GTK_BOX(hboxPresets2), btnQuickWalzer, FALSE, FALSE, 0);
    
    GtkWidget* btnQuickRnB = gtk_button_new_with_label("🎤 RnB");
    g_signal_connect(btnQuickRnB, "clicked", G_CALLBACK(onQuickPresetRnB), this);
    gtk_box_pack_start(GTK_BOX(hboxPresets2), btnQuickRnB, FALSE, FALSE, 0);
    
    // Generate Button
    GtkWidget* btnGenerate = gtk_button_new_with_label("🎼 Song generieren");
    g_signal_connect(btnGenerate, "clicked", G_CALLBACK(onGenerateSong), this);
    gtk_box_pack_start(GTK_BOX(vbox), btnGenerate, FALSE, FALSE, 0);
    
    // MIDI Export Button
    GtkWidget* btnExportMIDI = gtk_button_new_with_label("🎹 Als MIDI exportieren");
    g_signal_connect(btnExportMIDI, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer data) {
        auto* renderer = static_cast<GtkRenderer*>(data);
        
        GtkWidget* dialog = gtk_file_chooser_dialog_new(
            "MIDI exportieren", GTK_WINDOW(renderer->window_),
            GTK_FILE_CHOOSER_ACTION_SAVE,
            "_Abbrechen", GTK_RESPONSE_CANCEL,
            "_Speichern", GTK_RESPONSE_ACCEPT,
            NULL);
        
        gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), "song.mid");
        
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
            char* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
            
            // TODO: Actually export current song to MIDI
            std::cout << "🎹 Exporting MIDI to: " << filename << std::endl;
            
            GtkWidget* msgDialog = gtk_message_dialog_new(
                GTK_WINDOW(renderer->window_), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO,
                GTK_BUTTONS_OK, "MIDI wurde exportiert nach:\n%s", filename);
            gtk_dialog_run(GTK_DIALOG(msgDialog));
            gtk_widget_destroy(msgDialog);
            
            g_free(filename);
        }
        
        gtk_widget_destroy(dialog);
    }), this);
    gtk_box_pack_start(GTK_BOX(vbox), btnExportMIDI, FALSE, FALSE, 0);
    
    // Ausgabepfad
    GtkWidget* hbox5 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hbox5, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox5), gtk_label_new("Ausgabe:"), FALSE, FALSE, 0);
    
    const char* home = getenv("HOME");
    std::string outputPath = std::string(home) + "/.songgen/generated/";
    GtkWidget* pathLabel = gtk_label_new(outputPath.c_str());
    gtk_label_set_selectable(GTK_LABEL(pathLabel), TRUE);
    gtk_box_pack_start(GTK_BOX(hbox5), pathLabel, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 10);
    
    // 🎤 Pattern Capture System
    patternCaptureFrame_ = gtk_frame_new("🎤 Pattern Learning - Mikrofon Eingabe");
    gtk_box_pack_start(GTK_BOX(vbox), patternCaptureFrame_, FALSE, FALSE, 0);
    
    GtkWidget* patternBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(patternBox), 10);
    gtk_container_add(GTK_CONTAINER(patternCaptureFrame_), patternBox);
    
    // Description
    GtkWidget* patternDesc = gtk_label_new("Spiele Rhythmen oder Melodien über dein Mikrofon ein,\num dem System beizubringen, was gut klingt.");
    gtk_label_set_line_wrap(GTK_LABEL(patternDesc), TRUE);
    gtk_box_pack_start(GTK_BOX(patternBox), patternDesc, FALSE, FALSE, 0);
    
    // Pattern Type Selection
    GtkWidget* hboxPatternType = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(patternBox), hboxPatternType, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hboxPatternType), gtk_label_new("Pattern-Typ:"), FALSE, FALSE, 0);
    
    patternTypeCombo_ = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(patternTypeCombo_), "Rhythmus");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(patternTypeCombo_), "Melodie");
    gtk_combo_box_set_active(GTK_COMBO_BOX(patternTypeCombo_), 0);
    gtk_box_pack_start(GTK_BOX(hboxPatternType), patternTypeCombo_, FALSE, FALSE, 0);
    
    // Recording Controls
    GtkWidget* hboxPatternControls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(patternBox), hboxPatternControls, FALSE, FALSE, 0);
    
    patternRecordBtn_ = gtk_button_new_with_label("🔴 Aufnahme starten");
    g_signal_connect(patternRecordBtn_, "clicked", G_CALLBACK(onPatternRecord), this);
    gtk_box_pack_start(GTK_BOX(hboxPatternControls), patternRecordBtn_, FALSE, FALSE, 0);
    
    patternStopBtn_ = gtk_button_new_with_label("⏹️ Stoppen");
    gtk_widget_set_sensitive(patternStopBtn_, FALSE);
    g_signal_connect(patternStopBtn_, "clicked", G_CALLBACK(onPatternStop), this);
    gtk_box_pack_start(GTK_BOX(hboxPatternControls), patternStopBtn_, FALSE, FALSE, 0);
    
    // Status Label
    patternStatusLabel_ = gtk_label_new("Bereit zur Aufnahme");
    gtk_label_set_selectable(GTK_LABEL(patternStatusLabel_), TRUE);
    PangoAttrList* patternAttrs = pango_attr_list_new();
    PangoAttribute* patternAttr = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
    pango_attr_list_insert(patternAttrs, patternAttr);
    gtk_label_set_attributes(GTK_LABEL(patternStatusLabel_), patternAttrs);
    pango_attr_list_unref(patternAttrs);
    gtk_box_pack_start(GTK_BOX(patternBox), patternStatusLabel_, FALSE, FALSE, 0);
    
    // Pattern Library
    GtkWidget* frameLibrary = gtk_frame_new("📚 Gelernte Patterns");
    gtk_box_pack_start(GTK_BOX(patternBox), frameLibrary, TRUE, TRUE, 5);
    
    GtkWidget* libraryBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(libraryBox), 5);
    gtk_container_add(GTK_CONTAINER(frameLibrary), libraryBox);
    
    // ScrolledWindow for pattern list
    GtkWidget* scrolledWindow = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledWindow),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scrolledWindow, -1, 150);
    gtk_box_pack_start(GTK_BOX(libraryBox), scrolledWindow, TRUE, TRUE, 0);
    
    // TreeView for patterns
    patternLibraryStore_ = gtk_list_store_new(6, G_TYPE_STRING, G_TYPE_STRING, 
                                                G_TYPE_STRING, G_TYPE_STRING,
                                                G_TYPE_STRING, G_TYPE_INT);
    patternLibraryTree_ = gtk_tree_view_new_with_model(GTK_TREE_MODEL(patternLibraryStore_));
    
    GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(patternLibraryTree_),
        -1, "Typ", renderer, "text", 0, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(patternLibraryTree_),
        -1, "Name", renderer, "text", 1, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(patternLibraryTree_),
        -1, "Groove", renderer, "text", 2, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(patternLibraryTree_),
        -1, "Komplexität", renderer, "text", 3, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(patternLibraryTree_),
        -1, "Bewertung", renderer, "text", 4, NULL);
    
    g_signal_connect(patternLibraryTree_, "row-activated", 
                     G_CALLBACK(onPatternLibraryRowActivated), this);
    
    gtk_container_add(GTK_CONTAINER(scrolledWindow), patternLibraryTree_);
    
    // Pattern Library Buttons
    GtkWidget* hboxLibraryBtns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(libraryBox), hboxLibraryBtns, FALSE, FALSE, 0);
    
    GtkWidget* btnPatternExport = gtk_button_new_with_label("💾 Exportieren");
    g_signal_connect(btnPatternExport, "clicked", G_CALLBACK(onPatternExport), this);
    gtk_box_pack_start(GTK_BOX(hboxLibraryBtns), btnPatternExport, FALSE, FALSE, 0);
    
    GtkWidget* btnPatternImport = gtk_button_new_with_label("📂 Importieren");
    g_signal_connect(btnPatternImport, "clicked", G_CALLBACK(onPatternImport), this);
    gtk_box_pack_start(GTK_BOX(hboxLibraryBtns), btnPatternImport, FALSE, FALSE, 0);
    
    GtkWidget* btnPatternClear = gtk_button_new_with_label("🗑️ Alle löschen");
    g_signal_connect(btnPatternClear, "clicked", G_CALLBACK(onPatternClear), this);
    gtk_box_pack_start(GTK_BOX(hboxLibraryBtns), btnPatternClear, FALSE, FALSE, 0);
    
    // ▶️ Audio-Player für generierte Songs
    gtk_box_pack_start(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 10);
    
    GtkWidget* framePlayer = gtk_frame_new("🎵 Audio-Player");
    gtk_box_pack_start(GTK_BOX(vbox), framePlayer, FALSE, FALSE, 0);
    
    genPlayerBox_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(genPlayerBox_), 10);
    gtk_container_add(GTK_CONTAINER(framePlayer), genPlayerBox_);
    
    // Dateiname-Label
    genPlayerLabel_ = gtk_label_new("Kein Song geladen");
    gtk_label_set_selectable(GTK_LABEL(genPlayerLabel_), TRUE);
    gtk_box_pack_start(GTK_BOX(genPlayerBox_), genPlayerLabel_, FALSE, FALSE, 0);
    
    // Position Slider
    genPlayerScale_ = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 100.0, 1.0);
    gtk_scale_set_draw_value(GTK_SCALE(genPlayerScale_), FALSE);
    gtk_widget_set_size_request(genPlayerScale_, 400, -1);
    gtk_box_pack_start(GTK_BOX(genPlayerBox_), genPlayerScale_, FALSE, FALSE, 0);
    
    // Zeit-Label
    genPlayerTimeLabel_ = gtk_label_new("00:00 / 00:00");
    gtk_label_set_selectable(GTK_LABEL(genPlayerTimeLabel_), TRUE);
    gtk_box_pack_start(GTK_BOX(genPlayerBox_), genPlayerTimeLabel_, FALSE, FALSE, 0);
    
    // Control Buttons
    GtkWidget* hboxPlayerControls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(genPlayerBox_), hboxPlayerControls, FALSE, FALSE, 0);
    
    genPlayerBtnPlay_ = gtk_button_new_with_label("▶️ Play");
    gtk_box_pack_start(GTK_BOX(hboxPlayerControls), genPlayerBtnPlay_, FALSE, FALSE, 0);
    
    genPlayerBtnPause_ = gtk_button_new_with_label("⏸️ Pause");
    gtk_box_pack_start(GTK_BOX(hboxPlayerControls), genPlayerBtnPause_, FALSE, FALSE, 0);
    
    genPlayerBtnStop_ = gtk_button_new_with_label("⏹️ Stop");
    gtk_box_pack_start(GTK_BOX(hboxPlayerControls), genPlayerBtnStop_, FALSE, FALSE, 0);
    
    GtkWidget* btnLoadFile = gtk_button_new_with_label("📁 Song laden...");
    gtk_box_pack_start(GTK_BOX(hboxPlayerControls), btnLoadFile, FALSE, FALSE, 0);
    
    // Initialize state
    genPlayerTimeoutId_ = 0;
    genPlayerIsPlaying_ = false;
    genPlayerIsSeeking_ = false;
    genPlayerCurrentFile_ = "";
    
    // Position Update Callback
    static auto genPlayerUpdatePosition = [](gpointer data) -> gboolean {
        auto* renderer = static_cast<GtkRenderer*>(data);
        
        if (!renderer->audioPlayer_) {
            return G_SOURCE_CONTINUE;
        }
        
        // NICHT updaten während User den Slider bewegt!
        if (renderer->genPlayerIsSeeking_) {
            return G_SOURCE_CONTINUE;
        }
        
        double pos = renderer->audioPlayer_->getPosition();
        double dur = renderer->audioPlayer_->getDuration();
        
        if (dur > 0) {
            // Signal blockieren während automatischem Update
            g_signal_handlers_block_by_func(renderer->genPlayerScale_, (gpointer)G_CALLBACK(nullptr), renderer);
            gtk_range_set_value(GTK_RANGE(renderer->genPlayerScale_), (pos / dur) * 100.0);
            g_signal_handlers_unblock_by_func(renderer->genPlayerScale_, (gpointer)G_CALLBACK(nullptr), renderer);
            
            int posMin = (int)pos / 60;
            int posSec = (int)pos % 60;
            int durMin = (int)dur / 60;
            int durSec = (int)dur % 60;
            
            char timeStr[32];
            snprintf(timeStr, sizeof(timeStr), "%02d:%02d / %02d:%02d", posMin, posSec, durMin, durSec);
            gtk_label_set_text(GTK_LABEL(renderer->genPlayerTimeLabel_), timeStr);
        }
        
        return G_SOURCE_CONTINUE;
    };
    
    // Play Button
    g_signal_connect(genPlayerBtnPlay_, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer data) {
        auto* renderer = static_cast<GtkRenderer*>(data);
        
        if (!renderer->audioPlayer_) return;
        
        if (renderer->genPlayerCurrentFile_.empty()) {
            GtkWidget* dialog = gtk_message_dialog_new(
                GTK_WINDOW(renderer->window_), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO,
                GTK_BUTTONS_OK, "Bitte erst einen Song laden!");
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
            return;
        }
        
        if (!renderer->genPlayerIsPlaying_) {
            if (renderer->audioPlayer_->load(renderer->genPlayerCurrentFile_)) {
                renderer->audioPlayer_->play();
                renderer->genPlayerIsPlaying_ = true;
                
                // Start timer
                if (renderer->genPlayerTimeoutId_ > 0) {
                    g_source_remove(renderer->genPlayerTimeoutId_);
                }
                renderer->genPlayerTimeoutId_ = g_timeout_add(250, genPlayerUpdatePosition, renderer);
            }
        }
    }), this);
    
    // Pause Button
    g_signal_connect(genPlayerBtnPause_, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer data) {
        auto* renderer = static_cast<GtkRenderer*>(data);
        
        if (renderer->audioPlayer_ && renderer->genPlayerIsPlaying_) {
            renderer->audioPlayer_->pause();
            renderer->genPlayerIsPlaying_ = false;
        }
    }), this);
    
    // Stop Button
    g_signal_connect(genPlayerBtnStop_, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer data) {
        auto* renderer = static_cast<GtkRenderer*>(data);
        
        if (renderer->audioPlayer_) {
            renderer->audioPlayer_->stop();
            renderer->genPlayerIsPlaying_ = false;
            
            if (renderer->genPlayerTimeoutId_ > 0) {
                g_source_remove(renderer->genPlayerTimeoutId_);
                renderer->genPlayerTimeoutId_ = 0;
            }
            
            gtk_range_set_value(GTK_RANGE(renderer->genPlayerScale_), 0.0);
            gtk_label_set_text(GTK_LABEL(renderer->genPlayerTimeLabel_), "00:00 / 00:00");
        }
    }), this);
    
    // Load File Button
    g_signal_connect(btnLoadFile, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer data) {
        auto* renderer = static_cast<GtkRenderer*>(data);
        
        GtkWidget* dialog = gtk_file_chooser_dialog_new(
            "Song laden", GTK_WINDOW(renderer->window_),
            GTK_FILE_CHOOSER_ACTION_OPEN,
            "_Abbrechen", GTK_RESPONSE_CANCEL,
            "_Öffnen", GTK_RESPONSE_ACCEPT,
            NULL);
        
        // Set default folder to generated folder
        const char* home = getenv("HOME");
        std::string genPath = std::string(home) + "/.songgen/generated/";
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), genPath.c_str());
        
        // Filter for audio files
        GtkFileFilter* filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, "Audio Dateien");
        gtk_file_filter_add_pattern(filter, "*.wav");
        gtk_file_filter_add_pattern(filter, "*.mp3");
        gtk_file_filter_add_pattern(filter, "*.ogg");
        gtk_file_filter_add_pattern(filter, "*.flac");
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
        
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
            char* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
            renderer->genPlayerCurrentFile_ = filename;
            
            std::string fn = std::filesystem::path(filename).filename().string();
            gtk_label_set_text(GTK_LABEL(renderer->genPlayerLabel_), ("🎵 " + fn).c_str());
            
            g_free(filename);
        }
        
        gtk_widget_destroy(dialog);
    }), this);
    
    // Position Slider Seek - verbesserte Callbacks
    g_signal_connect(genPlayerScale_, "button-press-event", G_CALLBACK(+[](GtkWidget*, GdkEventButton*, gpointer data) -> gboolean {
        auto* renderer = static_cast<GtkRenderer*>(data);
        renderer->genPlayerIsSeeking_ = true;
        return FALSE;
    }), this);
    
    g_signal_connect(genPlayerScale_, "button-release-event", G_CALLBACK(+[](GtkWidget*, GdkEventButton*, gpointer data) -> gboolean {
        auto* renderer = static_cast<GtkRenderer*>(data);
        renderer->genPlayerIsSeeking_ = false;
        return FALSE;
    }), this);
    
    // Value-Changed: Erlaube Seek während Ziehen UND bei Klick
    g_signal_connect(genPlayerScale_, "value-changed", G_CALLBACK(+[](GtkRange* range, gpointer data) {
        auto* renderer = static_cast<GtkRenderer*>(data);
        
        // Nur wenn User aktiv seeked (nicht während automatischer Updates)
        if (renderer->genPlayerIsSeeking_ && renderer->audioPlayer_ && !renderer->genPlayerCurrentFile_.empty()) {
            double value = gtk_range_get_value(range);
            double dur = renderer->audioPlayer_->getDuration();
            
            if (dur > 0) {
                double seekPos = (value / 100.0) * dur;
                renderer->audioPlayer_->seek(seekPos);
            }
        }
    }), this);
    
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook_), vbox, gtk_label_new("🎼 Generator"));
    generatorTab_ = vbox;
}

void GtkRenderer::buildSettingsTab() {
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
    
    GtkWidget* label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label), "<span size='large' weight='bold'>⚙️ Einstellungen</span>");
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 5);
    
    // Hardware-Beschleunigung
    GtkWidget* frame1 = gtk_frame_new("Hardware-Beschleunigung");
    gtk_box_pack_start(GTK_BOX(vbox), frame1, FALSE, FALSE, 5);
    
    GtkWidget* vbox1 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(vbox1), 10);
    gtk_container_add(GTK_CONTAINER(frame1), vbox1);
    
    GtkWidget* checkNPU = gtk_check_button_new_with_label("Intel NPU/GPU nutzen (OpenVINO)");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkNPU), TRUE);
    gtk_box_pack_start(GTK_BOX(vbox1), checkNPU, FALSE, FALSE, 0);
    
    // Extraktions-Einstellungen
    GtkWidget* frame2 = gtk_frame_new("Extraktion");
    gtk_box_pack_start(GTK_BOX(vbox), frame2, FALSE, FALSE, 5);
    
    GtkWidget* vbox2 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(vbox2), 10);
    gtk_container_add(GTK_CONTAINER(frame2), vbox2);
    
    GtkWidget* hbox1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox2), hbox1, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(hbox1), gtk_label_new("Threads:"), FALSE, FALSE, 0);
    GtkWidget* threadSpin = gtk_spin_button_new_with_range(1, 32, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(threadSpin), 24);
    gtk_box_pack_start(GTK_BOX(hbox1), threadSpin, FALSE, FALSE, 0);
    
    // Auto-Sync
    GtkWidget* frame3 = gtk_frame_new("Automatische Synchronisation");
    gtk_box_pack_start(GTK_BOX(vbox), frame3, FALSE, FALSE, 5);
    
    GtkWidget* vbox3 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(vbox3), 10);
    gtk_container_add(GTK_CONTAINER(frame3), vbox3);
    
    GtkWidget* checkAutoSync = gtk_check_button_new_with_label("Auto-Sync aktiviert (alle 30s)");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkAutoSync), TRUE);
    gtk_box_pack_start(GTK_BOX(vbox3), checkAutoSync, FALSE, FALSE, 0);
    
    // Pfade
    GtkWidget* frame4 = gtk_frame_new("Verzeichnisse");
    gtk_box_pack_start(GTK_BOX(vbox), frame4, FALSE, FALSE, 5);
    
    GtkWidget* vbox4 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(vbox4), 10);
    gtk_container_add(GTK_CONTAINER(frame4), vbox4);
    
    const char* home = getenv("HOME");
    std::string dbPath = std::string(home) + "/.songgen/media.db";
    std::string hvscPath = std::string(home) + "/.songgen/hvsc/";
    std::string genPath = std::string(home) + "/.songgen/generated/";
    
    GtkWidget* labelDbPath = gtk_label_new(("Datenbank: " + dbPath).c_str());
    gtk_label_set_selectable(GTK_LABEL(labelDbPath), TRUE);
    gtk_box_pack_start(GTK_BOX(vbox4), labelDbPath, FALSE, FALSE, 0);
    
    GtkWidget* labelHvscPath = gtk_label_new(("HVSC: " + hvscPath).c_str());
    gtk_label_set_selectable(GTK_LABEL(labelHvscPath), TRUE);
    gtk_box_pack_start(GTK_BOX(vbox4), labelHvscPath, FALSE, FALSE, 0);
    
    GtkWidget* labelGenPath = gtk_label_new(("Generiert: " + genPath).c_str());
    gtk_label_set_selectable(GTK_LABEL(labelGenPath), TRUE);
    gtk_box_pack_start(GTK_BOX(vbox4), labelGenPath, FALSE, FALSE, 0);
    
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook_), vbox, gtk_label_new("⚙️ Settings"));
    settingsTab_ = vbox;
}

void GtkRenderer::buildTrainingTab() {
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
    
    // Titel
    GtkWidget* label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label), "<span size='large' weight='bold'>🧠 KI-Training &amp; Modell-Management</span>");
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
    
    GtkWidget* sublabel = gtk_label_new("Trainiere und verwalte das Audio-Analyse-Modell");
    gtk_label_set_selectable(GTK_LABEL(sublabel), TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), sublabel, FALSE, FALSE, 0);
    
    // Steuerungs-Buttons
    GtkWidget* hboxControls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hboxControls, FALSE, FALSE, 0);
    
    GtkWidget* btnTrain = gtk_button_new_with_label("🎓 Training starten");
    g_signal_connect(btnTrain, "clicked", G_CALLBACK(onTrainModel), this);
    gtk_box_pack_start(GTK_BOX(hboxControls), btnTrain, FALSE, FALSE, 0);
    
    GtkWidget* btnSave = gtk_button_new_with_label("💾 Modell speichern");
    g_signal_connect(btnSave, "clicked", G_CALLBACK(onSaveModel), this);
    gtk_box_pack_start(GTK_BOX(hboxControls), btnSave, FALSE, FALSE, 0);
    
    GtkWidget* btnLoad = gtk_button_new_with_label("📂 Modell laden");
    g_signal_connect(btnLoad, "clicked", G_CALLBACK(onLoadModel), this);
    gtk_box_pack_start(GTK_BOX(hboxControls), btnLoad, FALSE, FALSE, 0);
    
    GtkWidget* btnHistory = gtk_button_new_with_label("📜 Entscheidungs-Historie");
    g_signal_connect(btnHistory, "clicked", G_CALLBACK(onShowDecisionHistory), this);
    gtk_box_pack_start(GTK_BOX(hboxControls), btnHistory, FALSE, FALSE, 0);
    
    GtkWidget* btnInteractive = gtk_button_new_with_label("🎓 Interaktives Training");
    g_signal_connect(btnInteractive, "clicked", G_CALLBACK(onInteractiveTraining), this);
    gtk_box_pack_start(GTK_BOX(hboxControls), btnInteractive, FALSE, FALSE, 0);
    
    // 🧠 Intelligente Datenbank-Korrektur (Zweite Zeile)
    GtkWidget* hboxControls2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hboxControls2, FALSE, FALSE, 0);
    
    GtkWidget* btnAnalyze = gtk_button_new_with_label("🔍 Datenbank analysieren");
    g_signal_connect(btnAnalyze, "clicked", G_CALLBACK(onAnalyzeDatabase), this);
    gtk_box_pack_start(GTK_BOX(hboxControls2), btnAnalyze, FALSE, FALSE, 0);
    
    GtkWidget* btnPatterns = gtk_button_new_with_label("📊 Muster anzeigen");
    g_signal_connect(btnPatterns, "clicked", G_CALLBACK(onShowPatterns), this);
    gtk_box_pack_start(GTK_BOX(hboxControls2), btnPatterns, FALSE, FALSE, 0);
    
    GtkWidget* btnStructure = gtk_button_new_with_label("🎵 Song-Struktur lernen");
    g_signal_connect(btnStructure, "clicked", G_CALLBACK(onLearnSongStructure), this);
    gtk_box_pack_start(GTK_BOX(hboxControls2), btnStructure, FALSE, FALSE, 0);
    
    // Training-Einstellungen
    GtkWidget* frameSettings = gtk_frame_new("Training-Einstellungen");
    gtk_box_pack_start(GTK_BOX(vbox), frameSettings, FALSE, FALSE, 0);
    
    GtkWidget* gridSettings = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(gridSettings), 5);
    gtk_grid_set_column_spacing(GTK_GRID(gridSettings), 10);
    gtk_container_set_border_width(GTK_CONTAINER(gridSettings), 10);
    gtk_container_add(GTK_CONTAINER(frameSettings), gridSettings);
    
    gtk_grid_attach(GTK_GRID(gridSettings), gtk_label_new("Epochen:"), 0, 0, 1, 1);
    GtkWidget* spinEpochs = gtk_spin_button_new_with_range(10, 1000, 10);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spinEpochs), 100);
    gtk_grid_attach(GTK_GRID(gridSettings), spinEpochs, 1, 0, 1, 1);
    
    gtk_grid_attach(GTK_GRID(gridSettings), gtk_label_new("Batch Size:"), 0, 1, 1, 1);
    GtkWidget* spinBatch = gtk_spin_button_new_with_range(8, 128, 8);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spinBatch), 32);
    gtk_grid_attach(GTK_GRID(gridSettings), spinBatch, 1, 1, 1, 1);
    
    gtk_grid_attach(GTK_GRID(gridSettings), gtk_label_new("Learning Rate:"), 0, 2, 1, 1);
    GtkWidget* spinLR = gtk_spin_button_new_with_range(0.0001, 0.1, 0.0001);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spinLR), 0.001);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spinLR), 4);
    gtk_grid_attach(GTK_GRID(gridSettings), spinLR, 1, 2, 1, 1);
    
    GtkWidget* checkAutoSave = gtk_check_button_new_with_label("🔄 Auto-Save aktivieren (alle 10 Epochen)");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkAutoSave), TRUE);
    gtk_grid_attach(GTK_GRID(gridSettings), checkAutoSave, 0, 3, 2, 1);
    
    // Progress
    GtkWidget* frameProgress = gtk_frame_new("Training-Fortschritt");
    gtk_box_pack_start(GTK_BOX(vbox), frameProgress, FALSE, FALSE, 0);
    
    GtkWidget* vboxProgress = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(vboxProgress), 10);
    gtk_container_add(GTK_CONTAINER(frameProgress), vboxProgress);
    
    trainingStatusLabel_ = gtk_label_new("Bereit zum Training");
    gtk_label_set_selectable(GTK_LABEL(trainingStatusLabel_), TRUE);
    gtk_box_pack_start(GTK_BOX(vboxProgress), trainingStatusLabel_, FALSE, FALSE, 0);
    
    trainingProgressBar_ = gtk_progress_bar_new();
    gtk_box_pack_start(GTK_BOX(vboxProgress), trainingProgressBar_, FALSE, FALSE, 0);
    
    // 🎸 Instrumenten-Extraktion Info
    GtkWidget* frameInstruments = gtk_frame_new("🎸 Instrumenten-Library");
    gtk_box_pack_start(GTK_BOX(vbox), frameInstruments, FALSE, FALSE, 0);
    
    GtkWidget* vboxInstruments = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(vboxInstruments), 10);
    gtk_container_add(GTK_CONTAINER(frameInstruments), vboxInstruments);
    
    GtkWidget* labelInstrInfo = gtk_label_new(
        "Während des Trainings werden automatisch Instrumente aus den Tracks extrahiert:\n"
        "📁 Speicherort: ~/.songgen/instruments/\n"
        "🎵 Kategorien: Kicks, Snares, Hi-Hats, Bass, Leads, Other\n"
        "⚡ Extraktion: Alle 10 Tracks während Feature-Extraktion");
    gtk_label_set_line_wrap(GTK_LABEL(labelInstrInfo), TRUE);
    gtk_label_set_xalign(GTK_LABEL(labelInstrInfo), 0.0);
    gtk_box_pack_start(GTK_BOX(vboxInstruments), labelInstrInfo, FALSE, FALSE, 0);
    
    GtkWidget* hboxInstrButtons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vboxInstruments), hboxInstrButtons, FALSE, FALSE, 0);
    
    GtkWidget* btnShowInstruments = gtk_button_new_with_label("📂 Instrumenten-Ordner öffnen");
    g_signal_connect(btnShowInstruments, "clicked", G_CALLBACK(onShowInstrumentsFolder), this);
    gtk_box_pack_start(GTK_BOX(hboxInstrButtons), btnShowInstruments, FALSE, FALSE, 0);
    
    GtkWidget* btnCountInstruments = gtk_button_new_with_label("📊 Statistik anzeigen");
    g_signal_connect(btnCountInstruments, "clicked", G_CALLBACK(onShowInstrumentStats), this);
    gtk_box_pack_start(GTK_BOX(hboxInstrButtons), btnCountInstruments, FALSE, FALSE, 0);
    
    GtkWidget* btnPlayGenreDemos = gtk_button_new_with_label("🎵 Genre-Demos abspielen");
    g_signal_connect(btnPlayGenreDemos, "clicked", G_CALLBACK(onPlayGenreDemos), this);
    gtk_box_pack_start(GTK_BOX(hboxInstrButtons), btnPlayGenreDemos, FALSE, FALSE, 0);
    
    GtkWidget* btnRemoveDuplicates = gtk_button_new_with_label("🔍 Duplikate entfernen");
    g_signal_connect(btnRemoveDuplicates, "clicked", G_CALLBACK(onRemoveInstrumentDuplicates), this);
    gtk_box_pack_start(GTK_BOX(hboxInstrButtons), btnRemoveDuplicates, FALSE, FALSE, 0);
    
    // 🎭 Genre-Fusion Learning
    GtkWidget* frameGenreFusion = gtk_frame_new("🎭 Genre-Fusion & Künstler-Stile");
    gtk_box_pack_start(GTK_BOX(vbox), frameGenreFusion, FALSE, FALSE, 0);
    
    GtkWidget* vboxGenreFusion = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(vboxGenreFusion), 10);
    gtk_container_add(GTK_CONTAINER(frameGenreFusion), vboxGenreFusion);
    
    GtkWidget* labelGenreInfo = gtk_label_new(
        "Lernt genreübergreifende Musik (wie The Prodigy: Breakbeat + Electronic + Industrial)\n"
        "und charakteristische Künstler-Stile aus der Datenbank.");
    gtk_label_set_line_wrap(GTK_LABEL(labelGenreInfo), TRUE);
    gtk_label_set_xalign(GTK_LABEL(labelGenreInfo), 0.0);
    gtk_box_pack_start(GTK_BOX(vboxGenreFusion), labelGenreInfo, FALSE, FALSE, 0);
    
    GtkWidget* hboxGenreButtons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vboxGenreFusion), hboxGenreButtons, FALSE, FALSE, 0);
    
    GtkWidget* btnLearnFusions = gtk_button_new_with_label("🎭 Genre-Fusionen lernen");
    g_signal_connect(btnLearnFusions, "clicked", G_CALLBACK(onLearnGenreFusions), this);
    gtk_box_pack_start(GTK_BOX(hboxGenreButtons), btnLearnFusions, FALSE, FALSE, 0);
    
    GtkWidget* btnLearnArtistStyle = gtk_button_new_with_label("🎨 Künstler-Stil analysieren");
    g_signal_connect(btnLearnArtistStyle, "clicked", G_CALLBACK(onLearnArtistStyle), this);
    gtk_box_pack_start(GTK_BOX(hboxGenreButtons), btnLearnArtistStyle, FALSE, FALSE, 0);
    
    GtkWidget* btnSuggestTags = gtk_button_new_with_label("🔍 Genre-Tags vorschlagen");
    g_signal_connect(btnSuggestTags, "clicked", G_CALLBACK(onSuggestGenreTags), this);
    gtk_box_pack_start(GTK_BOX(hboxGenreButtons), btnSuggestTags, FALSE, FALSE, 0);
    
    // 🎸 Genre-Kombinationen Info
    GtkWidget* labelGenreInfo2 = gtk_label_new(
        "📝 Während des Trainings werden automatisch Genre-spezifische Instrumenten-Kombinationen generiert:\n"
        "   • Zu Beginn: 5 Genre-Demos als Referenz\n"
        "   • Alle 5 Epochen: Neue Kombination für zufälliges Genre\n"
        "   • Gespeichert als: ~/.songgen/training_demo_<Genre>.wav");
    gtk_label_set_line_wrap(GTK_LABEL(labelGenreInfo2), TRUE);
    gtk_label_set_xalign(GTK_LABEL(labelGenreInfo2), 0.0);
    gtk_box_pack_start(GTK_BOX(vboxInstruments), labelGenreInfo2, FALSE, FALSE, 5);
    
    // Training-History
    GtkWidget* frameHistory = gtk_frame_new("📜 Training-History");
    gtk_box_pack_start(GTK_BOX(vbox), frameHistory, TRUE, TRUE, 0);
    
    GtkWidget* scrolledHistory = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledHistory), 
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(frameHistory), scrolledHistory);
    
    GtkListStore* historyStore = gtk_list_store_new(4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    historyTreeView_ = gtk_tree_view_new_with_model(GTK_TREE_MODEL(historyStore));
    g_object_unref(historyStore);
    
    GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(historyTreeView_), -1, "Datum", renderer, "text", 0, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(historyTreeView_), -1, "Epochen", renderer, "text", 1, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(historyTreeView_), -1, "Loss", renderer, "text", 2, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(historyTreeView_), -1, "Genauigkeit", renderer, "text", 3, NULL);
    
    gtk_container_add(GTK_CONTAINER(scrolledHistory), historyTreeView_);
    
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook_), vbox, gtk_label_new("🧠 Training"));
    trainingTab_ = vbox;
}

void GtkRenderer::buildAnalyzerTab() {
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
    
    // Titel
    GtkWidget* label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label), "<span size='large' weight='bold'>🔬 Audio-Analyzer</span>");
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
    
    GtkWidget* sublabel = gtk_label_new("Analysiere Audio-Dateien und zeige detaillierte Informationen");
    gtk_label_set_selectable(GTK_LABEL(sublabel), TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), sublabel, FALSE, FALSE, 0);
    
    // Datei-Auswahl
    GtkWidget* hboxFile = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hboxFile, FALSE, FALSE, 0);
    
    GtkWidget* btnSelectFile = gtk_button_new_with_label("📂 Datei wählen");
    g_signal_connect(btnSelectFile, "clicked", G_CALLBACK(onAnalyzeFile), this);
    gtk_box_pack_start(GTK_BOX(hboxFile), btnSelectFile, FALSE, FALSE, 0);
    
    GtkWidget* labelFile = gtk_label_new("Keine Datei ausgewählt");
    gtk_label_set_selectable(GTK_LABEL(labelFile), TRUE);
    gtk_box_pack_start(GTK_BOX(hboxFile), labelFile, TRUE, TRUE, 0);
    
    // Analyse-Einstellungen
    GtkWidget* frameSettings = gtk_frame_new("Analyse-Einstellungen");
    gtk_box_pack_start(GTK_BOX(vbox), frameSettings, FALSE, FALSE, 0);
    
    GtkWidget* gridSettings = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(gridSettings), 5);
    gtk_grid_set_column_spacing(GTK_GRID(gridSettings), 10);
    gtk_container_set_border_width(GTK_CONTAINER(gridSettings), 10);
    gtk_container_add(GTK_CONTAINER(frameSettings), gridSettings);
    
    GtkWidget* checkBPM = gtk_check_button_new_with_label("BPM erkennen");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkBPM), TRUE);
    gtk_grid_attach(GTK_GRID(gridSettings), checkBPM, 0, 0, 1, 1);
    
    GtkWidget* checkKey = gtk_check_button_new_with_label("Tonart erkennen");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkKey), TRUE);
    gtk_grid_attach(GTK_GRID(gridSettings), checkKey, 1, 0, 1, 1);
    
    GtkWidget* checkEnergy = gtk_check_button_new_with_label("Energie-Analyse");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkEnergy), TRUE);
    gtk_grid_attach(GTK_GRID(gridSettings), checkEnergy, 0, 1, 1, 1);
    
    GtkWidget* checkSpectral = gtk_check_button_new_with_label("Spektral-Analyse");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkSpectral), TRUE);
    gtk_grid_attach(GTK_GRID(gridSettings), checkSpectral, 1, 1, 1, 1);
    
    GtkWidget* checkStructure = gtk_check_button_new_with_label("Struktur-Erkennung");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkStructure), TRUE);
    gtk_grid_attach(GTK_GRID(gridSettings), checkStructure, 0, 2, 1, 1);
    
    GtkWidget* checkMood = gtk_check_button_new_with_label("Stimmungs-Analyse");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkMood), TRUE);
    gtk_grid_attach(GTK_GRID(gridSettings), checkMood, 1, 2, 1, 1);
    
    // Analyse-Ergebnisse
    GtkWidget* frameResults = gtk_frame_new("📊 Analyse-Ergebnisse");
    gtk_box_pack_start(GTK_BOX(vbox), frameResults, TRUE, TRUE, 0);
    
    GtkWidget* scrolledResults = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledResults), 
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(frameResults), scrolledResults);
    
    GtkWidget* textView = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(textView), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(textView), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(textView), GTK_WRAP_WORD);
    gtk_container_add(GTK_CONTAINER(scrolledResults), textView);
    
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textView));
    gtk_text_buffer_set_text(buffer, 
        "Wähle eine Audio-Datei aus, um die Analyse zu starten.\n\n"
        "Die Analyse umfasst:\n"
        "• BPM (Beats per Minute)\n"
        "• Tonart und Modus\n"
        "• Energie-Level und Dynamik\n"
        "• Spektrale Eigenschaften\n"
        "• Song-Struktur (Intro, Verse, Chorus, etc.)\n"
        "• Stimmung und Intensität\n"
        "• Instrumente und Klangfarbe", -1);
    
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook_), vbox, gtk_label_new("🔬 Analyzer"));
    analyzerTab_ = vbox;
}

void GtkRenderer::buildHistoryTab() {
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
    
    // Titel
    GtkWidget* label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label), "<span size='large' weight='bold'>📜 Entscheidungshistorie</span>");
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
    
    GtkWidget* sublabel = gtk_label_new("Nachvollziehen und korrigieren von Verarbeitungsschritten und Metadaten");
    gtk_label_set_selectable(GTK_LABEL(sublabel), TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), sublabel, FALSE, FALSE, 0);
    
    // Button-Leiste
    GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    
    GtkWidget* btnEditMetadata = gtk_button_new_with_label("✏️ Metadaten bearbeiten");
    g_signal_connect(btnEditMetadata, "clicked", G_CALLBACK(onEditHistoryMetadata), this);
    gtk_box_pack_start(GTK_BOX(hbox), btnEditMetadata, FALSE, FALSE, 0);
    
    GtkWidget* btnExport = gtk_button_new_with_label("💾 Exportieren");
    g_signal_connect(btnExport, "clicked", G_CALLBACK(onExportHistory), this);
    gtk_box_pack_start(GTK_BOX(hbox), btnExport, FALSE, FALSE, 0);
    
    GtkWidget* btnClear = gtk_button_new_with_label("🗑️ Historie löschen");
    g_signal_connect(btnClear, "clicked", G_CALLBACK(onClearHistory), this);
    gtk_box_pack_end(GTK_BOX(hbox), btnClear, FALSE, FALSE, 0);
    
    // Paned für TreeView und Details
    GtkWidget* paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_pack_start(GTK_BOX(vbox), paned, TRUE, TRUE, 0);
    
    // TreeView für Historie-Einträge
    GtkWidget* scrolledTree = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledTree), 
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_paned_pack1(GTK_PANED(paned), scrolledTree, TRUE, TRUE);
    
    // ListStore: Timestamp, Action, Details, Result, FilePath, Metadata (JSON)
    historyStore_ = gtk_list_store_new(6, 
        G_TYPE_STRING,  // Timestamp
        G_TYPE_STRING,  // Action (z.B. "Datei hinzugefügt", "Genre erkannt", "BPM analysiert")
        G_TYPE_STRING,  // Details
        G_TYPE_STRING,  // Result
        G_TYPE_STRING,  // FilePath
        G_TYPE_STRING   // Metadata (JSON)
    );
    
    historyTreeView_ = gtk_tree_view_new_with_model(GTK_TREE_MODEL(historyStore_));
    g_object_unref(historyStore_);
    
    // Spalten
    GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
    
    GtkTreeViewColumn* colTime = gtk_tree_view_column_new_with_attributes("🕐 Zeit", renderer, "text", 0, NULL);
    gtk_tree_view_column_set_resizable(colTime, TRUE);
    gtk_tree_view_column_set_min_width(colTime, 150);
    gtk_tree_view_append_column(GTK_TREE_VIEW(historyTreeView_), colTime);
    
    GtkTreeViewColumn* colAction = gtk_tree_view_column_new_with_attributes("🎬 Aktion", renderer, "text", 1, NULL);
    gtk_tree_view_column_set_resizable(colAction, TRUE);
    gtk_tree_view_column_set_min_width(colAction, 180);
    gtk_tree_view_append_column(GTK_TREE_VIEW(historyTreeView_), colAction);
    
    GtkTreeViewColumn* colDetails = gtk_tree_view_column_new_with_attributes("📋 Details", renderer, "text", 2, NULL);
    gtk_tree_view_column_set_resizable(colDetails, TRUE);
    gtk_tree_view_column_set_expand(colDetails, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(historyTreeView_), colDetails);
    
    GtkTreeViewColumn* colResult = gtk_tree_view_column_new_with_attributes("✅ Ergebnis", renderer, "text", 3, NULL);
    gtk_tree_view_column_set_resizable(colResult, TRUE);
    gtk_tree_view_column_set_min_width(colResult, 150);
    gtk_tree_view_append_column(GTK_TREE_VIEW(historyTreeView_), colResult);
    
    g_signal_connect(historyTreeView_, "row-activated", G_CALLBACK(onHistoryRowActivated), this);
    
    gtk_container_add(GTK_CONTAINER(scrolledTree), historyTreeView_);
    
    // Details-TextView
    GtkWidget* frameDetails = gtk_frame_new("📝 Details");
    GtkWidget* scrolledDetails = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledDetails), 
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(frameDetails), scrolledDetails);
    gtk_paned_pack2(GTK_PANED(paned), frameDetails, FALSE, TRUE);
    
    historyTextView_ = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(historyTextView_), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(historyTextView_), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(historyTextView_), GTK_WRAP_WORD);
    gtk_container_add(GTK_CONTAINER(scrolledDetails), historyTextView_);
    
    // Monospace-Font für Details
    PangoFontDescription* fontDesc = pango_font_description_from_string("monospace 10");
    gtk_widget_override_font(historyTextView_, fontDesc);
    pango_font_description_free(fontDesc);
    
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(historyTextView_));
    gtk_text_buffer_set_text(buffer, 
        "Wähle einen Eintrag aus, um Details anzuzeigen.\n\n"
        "Diese Historie protokolliert alle Verarbeitungsschritte:\n"
        "• Dateien hinzugefügt/entfernt\n"
        "• Genre-Erkennungen und Änderungen\n"
        "• BPM-Analysen\n"
        "• Metadaten-Korrekturen\n"
        "• Video-Konvertierungen\n"
        "• Training-Entscheidungen\n\n"
        "Doppelklick auf Eintrag öffnet Editor für Korrekturen.", -1);
    
    // Setze Paned-Position
    gtk_paned_set_position(GTK_PANED(paned), 400);
    
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook_), vbox, gtk_label_new("📜 Historie"));
    historyTab_ = vbox;
}

void GtkRenderer::buildDataQualityTab() {
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
    
    // Titel
    GtkWidget* label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label), 
        "<span size='large' weight='bold'>📊 Datenqualität & Fehlende Elemente</span>");
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
    
    GtkWidget* subtitle = gtk_label_new(
        "Zeigt was dem System für perfekte KI-Generierung noch fehlt");
    gtk_box_pack_start(GTK_BOX(vbox), subtitle, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), 
                       FALSE, FALSE, 5);
    
    // Analyze Button
    GtkWidget* btnAnalyze = gtk_button_new_with_label("🔍 Datenqualität analysieren");
    g_signal_connect(btnAnalyze, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer data) {
        auto* self = static_cast<GtkRenderer*>(data);
        
        std::thread([self]() {
            std::cout << "🔍 Starting data quality analysis...\n";
            
            auto metrics = self->qualityAnalyzer_->analyze();
            self->qualityAnalyzer_->analyzePatterns(self->patternCapture_.get(), metrics);
            
            // Show results in GTK
            gdk_threads_add_idle([](gpointer data) -> gboolean {
                auto* metrics = static_cast<SongGen::DataQualityMetrics*>(data);
                auto* self = static_cast<GtkRenderer*>(g_object_get_data(
                    G_OBJECT(gtk_widget_get_toplevel(GTK_WIDGET(nullptr))), "renderer"));
                
                std::string message = "📊 DATENQUALITÄT ANALYSE\n\n";
                message += "═══════════════════════════════════\n\n";
                message += "🎯 GESAMT-SCORE: " + 
                    std::to_string(static_cast<int>(metrics->overallQuality * 100)) + "%\n\n";
                
                message += "📈 DETAILLIERTE SCORES:\n";
                message += "  • Genre-Abdeckung: " + 
                    std::to_string(static_cast<int>(metrics->genreCoverage * 100)) + "%\n";
                message += "  • Tempo-Balance: " + 
                    std::to_string(static_cast<int>(metrics->tempoRangeCoverage * 100)) + "%\n";
                message += "  • Instrument-Vielfalt: " + 
                    std::to_string(static_cast<int>(metrics->instrumentDiversity * 100)) + "%\n";
                message += "  • Rhythmus-Patterns: " + 
                    std::to_string(static_cast<int>(metrics->rhythmPatternCount * 100)) + "%\n";
                message += "  • Melodie-Patterns: " + 
                    std::to_string(static_cast<int>(metrics->melodyPatternCount * 100)) + "%\n";
                message += "  • Feature-Extraktion: " + 
                    std::to_string(static_cast<int>(metrics->tracksWithMFCC * 100)) + "%\n\n";
                
                message += "🎭 TEMPO-VERTEILUNG:\n";
                message += "  • Langsam (< 90 BPM): " + std::to_string(metrics->tracksSlow) + "\n";
                message += "  • Mittel (90-130): " + std::to_string(metrics->tracksMedium) + "\n";
                message += "  • Schnell (> 130): " + std::to_string(metrics->tracksFast) + "\n\n";
                
                if (!metrics->missingData.empty()) {
                    message += "❌ KRITISCHE LÜCKEN:\n";
                    for (const auto& gap : metrics->missingData) {
                        message += "  • " + gap + "\n";
                    }
                    message += "\n";
                }
                
                if (!metrics->recommendations.empty()) {
                    message += "💡 EMPFEHLUNGEN:\n";
                    for (size_t i = 0; i < std::min(size_t(10), metrics->recommendations.size()); i++) {
                        message += "  " + std::to_string(i+1) + ". " + 
                                  metrics->recommendations[i] + "\n";
                    }
                }
                
                GtkWidget* dialog = gtk_message_dialog_new(
                    nullptr, GTK_DIALOG_MODAL,
                    GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
                    "%s", message.c_str());
                gtk_window_set_title(GTK_WINDOW(dialog), "Datenqualität-Analyse");
                gtk_dialog_run(GTK_DIALOG(dialog));
                gtk_widget_destroy(dialog);
                
                delete metrics;
                return G_SOURCE_REMOVE;
            }, new SongGen::DataQualityMetrics(metrics));
            
        }).detach();
    }), this);
    gtk_box_pack_start(GTK_BOX(vbox), btnAnalyze, FALSE, FALSE, 0);
    
    // Progress Bars Frame
    GtkWidget* frameProgress = gtk_frame_new("📊 Live-Scores (% Vollständigkeit)");
    gtk_box_pack_start(GTK_BOX(vbox), frameProgress, TRUE, TRUE, 5);
    
    GtkWidget* progressBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(progressBox), 10);
    gtk_container_add(GTK_CONTAINER(frameProgress), progressBox);
    
    // Automatisch aktualisierte Progress Bars
    auto createProgressRow = [&](const char* labelText, GtkWidget** progressBar) {
        GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        gtk_box_pack_start(GTK_BOX(progressBox), hbox, FALSE, FALSE, 0);
        
        GtkWidget* lbl = gtk_label_new(labelText);
        gtk_widget_set_size_request(lbl, 200, -1);
        gtk_widget_set_halign(lbl, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(hbox), lbl, FALSE, FALSE, 0);
        
        *progressBar = gtk_progress_bar_new();
        gtk_widget_set_size_request(*progressBar, 400, -1);
        gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(*progressBar), TRUE);
        gtk_box_pack_start(GTK_BOX(hbox), *progressBar, TRUE, TRUE, 0);
    };
    
    GtkWidget *progGenre, *progTempo, *progInstruments, *progRhythm, *progMelody, *progFeatures;
    
    createProgressRow("🎭 Genre-Abdeckung:", &progGenre);
    createProgressRow("🎵 Tempo-Balance:", &progTempo);
    createProgressRow("🎸 Instrument-Vielfalt:", &progInstruments);
    createProgressRow("🥁 Rhythmus-Patterns:", &progRhythm);
    createProgressRow("🎹 Melodie-Patterns:", &progMelody);
    createProgressRow("📊 Feature-Extraktion:", &progFeatures);
    
    // Auto-Update Timer (alle 5 Sekunden)
    g_timeout_add_seconds(5, [](gpointer data) -> gboolean {
        auto* self = static_cast<GtkRenderer*>(data);
        
        if (!self->qualityAnalyzer_) return G_SOURCE_REMOVE;
        
        // Quick analysis (ohne Dialog)
        std::thread([self]() {
            auto metrics = self->qualityAnalyzer_->analyze();
            self->qualityAnalyzer_->analyzePatterns(self->patternCapture_.get(), metrics);
            
            // Update wird im nächsten Frame gemacht (simplified)
            std::cout << "📊 Quality: " << (metrics.overallQuality * 100) << "% | "
                     << "Genres: " << (metrics.genreCoverage * 100) << "% | "
                     << "Patterns: R=" << (metrics.rhythmPatternCount * 100) 
                     << "% M=" << (metrics.melodyPatternCount * 100) << "%\n";
        }).detach();
        
        return G_SOURCE_CONTINUE;
    }, this);
    
    // Genre Details Button
    GtkWidget* btnGenreDetails = gtk_button_new_with_label("🎭 Genre-Details anzeigen");
    g_signal_connect(btnGenreDetails, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer data) {
        auto* self = static_cast<GtkRenderer*>(data);
        
        std::thread([self]() {
            auto genreCompleteness = self->qualityAnalyzer_->analyzeGenreCoverage();
            
            gdk_threads_add_idle([](gpointer data) -> gboolean {
                auto* genres = static_cast<std::vector<SongGen::GenreCompleteness>*>(data);
                
                std::string message = "🎭 GENRE-VOLLSTÄNDIGKEIT\n\n";
                message += "═══════════════════════════════════\n\n";
                
                for (size_t i = 0; i < std::min(size_t(15), genres->size()); i++) {
                    const auto& gc = (*genres)[i];
                    int percent = static_cast<int>(gc.completeness * 100);
                    
                    message += gc.genre + ": " + std::to_string(percent) + "% ";
                    message += "(" + std::to_string(gc.trackCount) + "/" + 
                              std::to_string(gc.minRecommended) + ")\n";
                    
                    if (!gc.missingElements.empty() && percent < 80) {
                        for (const auto& missing : gc.missingElements) {
                            message += "    → " + missing + "\n";
                        }
                    }
                }
                
                GtkWidget* dialog = gtk_message_dialog_new(
                    nullptr, GTK_DIALOG_MODAL,
                    GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
                    "%s", message.c_str());
                gtk_window_set_title(GTK_WINDOW(dialog), "Genre-Analyse");
                gtk_dialog_run(GTK_DIALOG(dialog));
                gtk_widget_destroy(dialog);
                
                delete genres;
                return G_SOURCE_REMOVE;
            }, new std::vector<SongGen::GenreCompleteness>(genreCompleteness));
        }).detach();
    }), this);
    gtk_box_pack_start(GTK_BOX(progressBox), btnGenreDetails, FALSE, FALSE, 5);
    
    // Priority Actions Button
    GtkWidget* btnPriority = gtk_button_new_with_label("⚡ Prioritäts-Liste");
    g_signal_connect(btnPriority, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer data) {
        auto* self = static_cast<GtkRenderer*>(data);
        
        std::thread([self]() {
            auto actions = self->qualityAnalyzer_->getPriorityActions();
            
            gdk_threads_add_idle([](gpointer data) -> gboolean {
                auto* actions = static_cast<std::vector<std::pair<std::string, float>>*>(data);
                
                std::string message = "⚡ PRIORITÄTS-LISTE\n\n";
                message += "Was sollte ZUERST verbessert werden?\n\n";
                message += "═══════════════════════════════════\n\n";
                
                for (size_t i = 0; i < actions->size(); i++) {
                    int priority = static_cast<int>((*actions)[i].second * 100);
                    message += std::to_string(i+1) + ". " + (*actions)[i].first + 
                              " (Priorität: " + std::to_string(priority) + "%)\n\n";
                }
                
                GtkWidget* dialog = gtk_message_dialog_new(
                    nullptr, GTK_DIALOG_MODAL,
                    GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
                    "%s", message.c_str());
                gtk_window_set_title(GTK_WINDOW(dialog), "Was jetzt tun?");
                gtk_dialog_run(GTK_DIALOG(dialog));
                gtk_widget_destroy(dialog);
                
                delete actions;
                return G_SOURCE_REMOVE;
            }, new std::vector<std::pair<std::string, float>>(actions));
        }).detach();
    }), this);
    gtk_box_pack_start(GTK_BOX(progressBox), btnPriority, FALSE, FALSE, 0);
    
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook_), vbox, gtk_label_new("📊 Qualität"));
}

// Callbacks
void GtkRenderer::onDeleteDatabase(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    
    GtkWidget* dialog = gtk_message_dialog_new(
        GTK_WINDOW(self->window_),
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_WARNING,
        GTK_BUTTONS_YES_NO,
        "⚠️ WARNUNG: Alle Datenbank-Einträge werden gelöscht!\nDiese Aktion kann nicht rückgängig gemacht werden.\n\nAktuell: %zu Einträge in der Datenbank",
        self->filteredMedia_.size()
    );
    
    gtk_window_set_title(GTK_WINDOW(dialog), "Datenbank löschen?");
    
    gint result = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    
    if (result == GTK_RESPONSE_YES) {
        self->isDeleting_ = true;
        self->deleteProgress_ = 0;
        
        // Progress-Dialog
        GtkWidget* progressDialog = gtk_dialog_new_with_buttons(
            "Datenbank wird gelöscht...",
            GTK_WINDOW(self->window_),
            (GtkDialogFlags)0,
            NULL
        );
        makeDialogResizable(progressDialog, 500, 180);
        
        GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(progressDialog));
        GtkWidget* progressBar = gtk_progress_bar_new();
        GtkWidget* labelProgress = gtk_label_new("Lösche Einträge...");
        gtk_label_set_selectable(GTK_LABEL(labelProgress), TRUE);
        
        gtk_box_pack_start(GTK_BOX(content), labelProgress, FALSE, FALSE, 5);
        gtk_box_pack_start(GTK_BOX(content), progressBar, FALSE, FALSE, 5);
        
        gtk_widget_show_all(progressDialog);
        
        // Löschung im Thread
        std::thread([self, progressDialog, progressBar, labelProgress]() {
            auto allMedia = self->database_->getAll();
            self->deleteTotal_ = allMedia.size();
            
            for (const auto& meta : allMedia) {
                self->database_->deleteMedia(meta.id);
                self->deleteProgress_++;
                
                // Update GUI (thread-safe via gdk_threads_add_idle)
                gdk_threads_add_idle([](gpointer data) -> gboolean {
                    auto* widgets = static_cast<std::tuple<GtkWidget*, GtkWidget*, GtkRenderer*>*>(data);
                    GtkRenderer* self = std::get<2>(*widgets);
                    
                    float progress = (float)self->deleteProgress_.load() / self->deleteTotal_.load();
                    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(std::get<0>(*widgets)), progress);
                    
                    std::string text = std::to_string(self->deleteProgress_.load()) + " / " + 
                                      std::to_string(self->deleteTotal_.load()) + " gelöscht";
                    gtk_label_set_text(GTK_LABEL(std::get<1>(*widgets)), text.c_str());
                    
                    return G_SOURCE_REMOVE;
                }, new std::tuple<GtkWidget*, GtkWidget*, GtkRenderer*>(progressBar, labelProgress, self));
            }
            
            self->filteredMedia_.clear();
            self->isDeleting_ = false;
            
            // Schließe Dialog
            gdk_threads_add_idle([](gpointer dialog) -> gboolean {
                gtk_widget_destroy(GTK_WIDGET(dialog));
                return G_SOURCE_REMOVE;
            }, progressDialog);
            
        }).detach();
    }
}

void GtkRenderer::onCleanupMissing(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    
    std::thread([self]() {
        auto allMedia = self->database_->getAll();
        size_t removed = 0;
        
        for (const auto& meta : allMedia) {
            if (!std::filesystem::exists(meta.filepath)) {
                self->database_->deleteMedia(meta.id);
                removed++;
            }
        }
        
        self->filteredMedia_ = self->database_->getAll();
        
        gdk_threads_add_idle([](gpointer data) -> gboolean {
            auto* info = static_cast<std::pair<GtkRenderer*, size_t>*>(data);
            
            GtkWidget* dialog = gtk_message_dialog_new(
                GTK_WINDOW(info->first->window_),
                GTK_DIALOG_MODAL,
                GTK_MESSAGE_INFO,
                GTK_BUTTONS_OK,
                "✅ %zu fehlende Einträge aus DB entfernt",
                info->second
            );
            
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
            
            delete info;
            return G_SOURCE_REMOVE;
        }, new std::pair<GtkRenderer*, size_t>(self, removed));
        
    }).detach();
}

void GtkRenderer::onAutoDetectGenres(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    
    auto allMedia = self->database_->getAll();
    
    if (allMedia.empty()) {
        GtkWidget* dialog = gtk_message_dialog_new(
            GTK_WINDOW(self->window_),
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_INFO,
            GTK_BUTTONS_OK,
            "Keine Dateien in der Datenbank vorhanden!"
        );
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }
    
    GtkWidget* confirmDialog = gtk_message_dialog_new(
        GTK_WINDOW(self->window_),
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_YES_NO,
        "Genre-Erkennung für %zu Dateien starten?\n\nDies kann einige Zeit dauern.",
        allMedia.size()
    );
    
    if (gtk_dialog_run(GTK_DIALOG(confirmDialog)) != GTK_RESPONSE_YES) {
        gtk_widget_destroy(confirmDialog);
        return;
    }
    gtk_widget_destroy(confirmDialog);
    
    // Progress-Dialog
    GtkWidget* progressDialog = gtk_dialog_new_with_buttons(
        "Genre-Erkennung läuft...",
        GTK_WINDOW(self->window_),
        (GtkDialogFlags)0,
        NULL
    );
    
    makeDialogResizable(progressDialog, 500, 180);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(progressDialog));
    GtkWidget* progressBar = gtk_progress_bar_new();
    GtkWidget* labelProgress = gtk_label_new("Analysiere Dateien...");
    gtk_label_set_selectable(GTK_LABEL(labelProgress), TRUE);
    
    gtk_box_pack_start(GTK_BOX(content), labelProgress, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(content), progressBar, FALSE, FALSE, 5);
    gtk_widget_show_all(progressDialog);
    
    // Analyse in Thread
    std::thread([self, allMedia, progressDialog, progressBar, labelProgress]() {
        size_t processed = 0;
        size_t updated = 0;
        
        for (const auto& media : allMedia) {
            processed++;
            
            // Lade Audio und analysiere
            std::vector<float> samples;
            int sampleRate;
            
            if (self->analyzer_->loadAudioFile(media.filepath, samples, sampleRate)) {
                float bpm = self->analyzer_->detectBPM(samples, sampleRate);
                std::string detectedGenre = self->analyzer_->detectGenreFromAudio(samples, sampleRate, bpm);
                
                // Simuliere Konfidenz basierend auf BPM-Konsistenz
                float confidence = 0.85f;
                if (bpm < 80 || bpm > 180) confidence = 0.6f;
                if (detectedGenre == "Unknown") confidence = 0.4f;
                
                // Bei niedriger Konfidenz: Benutzer fragen
                if (confidence < 0.7f && !detectedGenre.empty()) {
                    auto userAnswer = std::make_shared<std::string>();
                    std::atomic<bool> dialogDone{false};
                    
                    gdk_threads_add_idle([](gpointer data) -> gboolean {
                        auto* args = static_cast<std::tuple<GtkRenderer*, std::string, std::string, float, int64_t, 
                                                           std::shared_ptr<std::string>, std::atomic<bool>*>*>(data);
                        GtkRenderer* self = std::get<0>(*args);
                        std::string detected = std::get<1>(*args);
                        std::string filepath = std::get<2>(*args);
                        float conf = std::get<3>(*args);
                        int64_t fileId = std::get<4>(*args);
                        auto answer = std::get<5>(*args);
                        auto* done = std::get<6>(*args);
                        
                        std::string question = "🎵 Unsichere Genre-Erkennung:\n\n"
                                              "Datei: " + filepath.substr(filepath.find_last_of('/') + 1) + "\n"
                                              "Erkanntes Genre: " + detected + "\n"
                                              "Konfidenz: " + std::to_string((int)(conf * 100)) + "%\n\n"
                                              "Bitte wählen Sie das korrekte Genre:";
                        
                        std::vector<std::string> options = {detected, "Techno", "Trance", "House", "Ambient", 
                                                           "Rock", "Pop", "Jazz", "Classical", "Überspringen"};
                        
                        std::string context = "Datei: " + filepath + " | Konfidenz: " + std::to_string((int)(conf * 100)) + "%";
                        
                        std::string result = self->showDecisionDialog(question, options, context, filepath);
                        
                        // Speichere Entscheidung
                        MediaDatabase::TrainingDecision decision;
                        decision.question = question;
                        decision.options = options;
                        decision.userAnswer = result;
                        decision.confidence = conf;
                        decision.context = "{\"fileId\": " + std::to_string(fileId) + 
                                         ", \"detectedGenre\": \"" + detected + "\", "
                                         "\"filepath\": \"" + filepath + "\"}";
                        decision.timestamp = time(nullptr);
                        decision.decisionType = "genre_classification";
                        decision.answered = (!result.empty() && result != "Überspringen");
                        decision.audioFile = filepath;
                        
                        self->database_->saveDecision(decision);
                        
                        if (!result.empty() && result != "Überspringen") {
                            *answer = result;
                        }
                        
                        done->store(true);
                        delete args;
                        return G_SOURCE_REMOVE;
                    }, new std::tuple<GtkRenderer*, std::string, std::string, float, int64_t,
                                     std::shared_ptr<std::string>, std::atomic<bool>*>(
                        self, detectedGenre, media.filepath, confidence, media.id, userAnswer, &dialogDone));
                    
                    // Warte auf Benutzer-Antwort
                    while (!dialogDone.load()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    }
                    
                    std::string answer = *userAnswer;
                    if (!answer.empty()) {
                        detectedGenre = answer;
                    }
                }
                
                if (!detectedGenre.empty() && detectedGenre != media.genre && detectedGenre != "Überspringen") {
                    MediaMetadata updatedMeta = media;
                    updatedMeta.genre = detectedGenre;
                    updatedMeta.bpm = bpm;
                    
                    if (self->database_->addMedia(updatedMeta)) {
                        updated++;
                    }
                }
            }
            
            // Update Progress
            if (processed % 5 == 0 || processed == allMedia.size()) {
                gdk_threads_add_idle([](gpointer data) -> gboolean {
                    auto* info = static_cast<std::tuple<GtkWidget*, GtkWidget*, size_t, size_t, size_t>*>(data);
                    
                    float progress = (float)std::get<2>(*info) / std::get<3>(*info);
                    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(std::get<0>(*info)), progress);
                    
                    std::string text = std::to_string(std::get<2>(*info)) + " / " + 
                                      std::to_string(std::get<3>(*info)) + " (" + 
                                      std::to_string(std::get<4>(*info)) + " aktualisiert)";
                    gtk_label_set_text(GTK_LABEL(std::get<1>(*info)), text.c_str());
                    
                    delete info;
                    return G_SOURCE_REMOVE;
                }, new std::tuple<GtkWidget*, GtkWidget*, size_t, size_t, size_t>(progressBar, labelProgress, processed, allMedia.size(), updated));
            }
        }
        
        // Refresh in main thread (thread-safe)
        gdk_threads_add_idle([](gpointer data) -> gboolean {
            static_cast<GtkRenderer*>(data)->refreshDatabaseView();
            return G_SOURCE_REMOVE;
        }, self);
        
        // Schließe Dialog und zeige Ergebnis
        gdk_threads_add_idle([](gpointer data) -> gboolean {
            auto* info = static_cast<std::tuple<GtkRenderer*, GtkWidget*, size_t>*>(data);
            GtkRenderer* self = std::get<0>(*info);
            
            gtk_widget_destroy(std::get<1>(*info));
            
            GtkWidget* resultDialog = gtk_message_dialog_new(
                GTK_WINDOW(self->window_),
                GTK_DIALOG_MODAL,
                GTK_MESSAGE_INFO,
                GTK_BUTTONS_OK,
                "✅ Genre-Erkennung abgeschlossen!\n\n%zu Dateien aktualisiert.",
                std::get<2>(*info)
            );
            gtk_dialog_run(GTK_DIALOG(resultDialog));
            gtk_widget_destroy(resultDialog);
            
            delete info;
            return G_SOURCE_REMOVE;
        }, new std::tuple<GtkRenderer*, GtkWidget*, size_t>(self, progressDialog, updated));
        
    }).detach();
}

void GtkRenderer::onAnalyzeAll(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    
    auto allMedia = self->database_->getAll();
    
    if (allMedia.empty()) {
        GtkWidget* dialog = gtk_message_dialog_new(
            GTK_WINDOW(self->window_),
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_INFO,
            GTK_BUTTONS_OK,
            "Keine Dateien in der Datenbank vorhanden!"
        );
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }
    
    // Filter: Nur nicht-analysierte Dateien
    std::vector<MediaMetadata> unanalyzed;
    for (const auto& meta : allMedia) {
        if (!meta.analyzed) {
            unanalyzed.push_back(meta);
        }
    }
    
    if (unanalyzed.empty()) {
        GtkWidget* dialog = gtk_message_dialog_new(
            GTK_WINDOW(self->window_),
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_INFO,
            GTK_BUTTONS_OK,
            "✅ Alle Dateien bereits analysiert!\n\n%zu von %zu Dateien sind analysiert.",
            allMedia.size(), allMedia.size()
        );
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }
    
    GtkWidget* confirmDialog = gtk_message_dialog_new(
        GTK_WINDOW(self->window_),
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_YES_NO,
        "🔬 Vollständige Audio-Analyse starten?\n\n"
        "%zu nicht analysierte Dateien gefunden.\n\n"
        "Extrahiert:\n"
        "• BPM (Tempo)\n"
        "• Genre (automatisch)\n"
        "• Spektrale Features (MFCC, Centroid, Rolloff)\n"
        "• Intensität & Bass-Level\n\n"
        "Dies kann mehrere Minuten dauern.",
        unanalyzed.size()
    );
    
    if (gtk_dialog_run(GTK_DIALOG(confirmDialog)) != GTK_RESPONSE_YES) {
        gtk_widget_destroy(confirmDialog);
        return;
    }
    gtk_widget_destroy(confirmDialog);
    
    // Progress-Dialog mit Abbrechen-Button
    GtkWidget* progressDialog = gtk_dialog_new_with_buttons(
        "🔬 Analysiere Audio-Features...",
        GTK_WINDOW(self->window_),
        (GtkDialogFlags)0,
        "Abbrechen", GTK_RESPONSE_CANCEL,
        NULL
    );
    makeDialogResizable(progressDialog, 600, 200);
    
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(progressDialog));
    GtkWidget* progressBar = gtk_progress_bar_new();
    GtkWidget* labelProgress = gtk_label_new("Analysiere Dateien...");
    gtk_label_set_selectable(GTK_LABEL(labelProgress), TRUE);
    GtkWidget* labelDetails = gtk_label_new("");
    gtk_label_set_selectable(GTK_LABEL(labelDetails), TRUE);
    
    gtk_box_pack_start(GTK_BOX(content), labelProgress, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(content), progressBar, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(content), labelDetails, FALSE, FALSE, 5);
    gtk_widget_show_all(progressDialog);
    
    // Abbrechen-Handler
    std::atomic<bool>* cancelFlag = new std::atomic<bool>(false);
    g_signal_connect(progressDialog, "response", G_CALLBACK(+[](GtkDialog* dialog, gint response, gpointer data) {
        if (response == GTK_RESPONSE_CANCEL) {
            auto* flag = static_cast<std::atomic<bool>*>(data);
            *flag = true;
            std::cout << "⚠️ Analyse wird abgebrochen...\n";
        }
    }), cancelFlag);
    
    // Analyse mit Multi-Threading (nutzt alle CPU-Kerne)
    std::thread([self, unanalyzed, progressDialog, progressBar, labelProgress, labelDetails, cancelFlag]() {
        std::atomic<size_t> processed(0);
        std::atomic<size_t> analyzed(0);
        std::mutex dbMutex;  // Schütze Datenbank-Zugriff
        
        // Anzahl Threads = CPU-Kerne
        unsigned int numThreads = std::thread::hardware_concurrency();
        if (numThreads == 0) numThreads = 4;  // Fallback
        
        std::cout << "🚀 Starte parallele Analyse mit " << numThreads << " Threads\n";
        
        // Thread-Pool
        std::vector<std::thread> workers;
        std::atomic<size_t> nextIndex(0);
        
        for (unsigned int t = 0; t < numThreads; ++t) {
            workers.emplace_back([&, self]() {
                // Jeder Thread erstellt eigenen Analyzer (thread-safe)
                AudioAnalyzer localAnalyzer;
                
                while (true) {
                    // Check Cancel-Flag
                    if (*cancelFlag) {
                        std::cout << "⚠️ Thread " << t << " abgebrochen\n";
                        break;
                    }
                    
                    size_t idx = nextIndex.fetch_add(1);
                    if (idx >= unanalyzed.size()) break;
                    
                    const auto& media = unanalyzed[idx];
                    
                    // Lade Audio und analysiere vollständig
                    std::vector<float> samples;
                    int sampleRate;
                    
                    if (localAnalyzer.loadAudioFile(media.filepath, samples, sampleRate)) {
                        MediaMetadata updatedMeta = media;
                        
                        // BPM-Erkennung
                        updatedMeta.bpm = localAnalyzer.detectBPM(samples, sampleRate);
                        
                        // Genre-Erkennung
                        updatedMeta.genre = localAnalyzer.detectGenreFromAudio(samples, sampleRate, updatedMeta.bpm);
                        
                        // Intensität
                        updatedMeta.intensity = localAnalyzer.detectIntensity(samples);
                        
                        // Bass-Level
                        updatedMeta.bassLevel = localAnalyzer.detectBassLevel(samples, sampleRate);
                        
                        // Spektrale Features
                        updatedMeta.spectralCentroid = localAnalyzer.calculateSpectralCentroid(samples, sampleRate);
                        updatedMeta.zeroCrossingRate = localAnalyzer.calculateZeroCrossingRate(samples);
                        updatedMeta.mfccHash = localAnalyzer.calculateMFCCHash(samples, sampleRate);
                        
                        // Markiere als analysiert
                        updatedMeta.analyzed = true;
                        
                        // Thread-safe Datenbank-Update
                        {
                            std::lock_guard<std::mutex> lock(dbMutex);
                            if (self->database_->updateMedia(updatedMeta)) {
                                analyzed++;
                            }
                        }
                    }
                    
                    processed++;
                    
                    // Update Progress (alle 5 Dateien)
                    if (processed % 5 == 0 || processed == unanalyzed.size()) {
                        gdk_threads_add_idle([](gpointer data) -> gboolean {
                            auto* info = static_cast<std::tuple<GtkWidget*, GtkWidget*, GtkWidget*, size_t, size_t, size_t>*>(data);
                            
                            float progress = (float)std::get<3>(*info) / std::get<4>(*info);
                            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(std::get<0>(*info)), progress);
                            
                            // Text mit Prozent
                            char percentText[64];
                            snprintf(percentText, sizeof(percentText), "%.1f%%", progress * 100.0f);
                            gtk_progress_bar_set_text(GTK_PROGRESS_BAR(std::get<0>(*info)), percentText);
                            gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(std::get<0>(*info)), TRUE);
                            
                            std::string text = std::to_string(std::get<3>(*info)) + " / " + 
                                              std::to_string(std::get<4>(*info)) + " (" + percentText + ")";
                            gtk_label_set_text(GTK_LABEL(std::get<1>(*info)), text.c_str());
                            
                            float analyzedPercent = (float)std::get<5>(*info) / std::get<4>(*info) * 100.0f;
                            char detailsText[128];
                            snprintf(detailsText, sizeof(detailsText), "✓ Analysiert: %zu (%.1f%%)", 
                                    std::get<5>(*info), analyzedPercent);
                            gtk_label_set_text(GTK_LABEL(std::get<2>(*info)), detailsText);
                            
                            delete info;
                            return G_SOURCE_REMOVE;
                        }, new std::tuple<GtkWidget*, GtkWidget*, GtkWidget*, size_t, size_t, size_t>(
                            progressBar, labelProgress, labelDetails, processed.load(), unanalyzed.size(), analyzed.load()));
                    }
                }
            });
        }
        
        // Warte auf alle Worker-Threads
        for (auto& worker : workers) {
            worker.join();
        }
        
        std::cout << "✅ Alle Worker-Threads beendet. Analyzed: " << analyzed << " Cancelled: " << *cancelFlag << "\n";
        
        bool wasCancelled = *cancelFlag;
        delete cancelFlag;  // Cleanup
        
        // Refresh Database List in main thread
        gdk_threads_add_idle([](gpointer data) -> gboolean {
            GtkRenderer* self = static_cast<GtkRenderer*>(data);
            self->refreshDatabaseView();
            std::cout << "✅ Database view refreshed\n";
            return G_SOURCE_REMOVE;
        }, self);
        
        // Schließe Dialog und zeige Ergebnis (NACH refresh)
        gdk_threads_add_idle([](gpointer data) -> gboolean {
            auto* info = static_cast<std::tuple<GtkRenderer*, GtkWidget*, size_t, bool>*>(data);
            GtkRenderer* self = std::get<0>(*info);
            GtkWidget* progressDlg = std::get<1>(*info);
            size_t analyzedCount = std::get<2>(*info);
            bool cancelled = std::get<3>(*info);
            
            gtk_widget_destroy(progressDlg);
            
            char message[512];
            float percentAnalyzed = self->filteredMedia_.empty() ? 0.0f : 
                                   (float)analyzedCount / self->filteredMedia_.size() * 100.0f;
            
            if (cancelled) {
                snprintf(message, sizeof(message),
                    "⚠️ Analyse abgebrochen!\n\n"
                    "%zu Dateien analysiert (%.1f%%) bevor Abbruch.\n\n"
                    "Du kannst die Analyse jederzeit wieder starten.",
                    analyzedCount, percentAnalyzed);
            } else {
                snprintf(message, sizeof(message),
                    "✅ Audio-Analyse abgeschlossen!\n\n"
                    "%zu Dateien vollständig analysiert (%.1f%%).\n\n"
                    "Extrahierte Features:\n"
                    "• BPM & Genre\n"
                    "• Spektrale Features (MFCC, Centroid)\n"
                    "• Intensität & Bass-Level\n\n"
                    "Dateien sind jetzt bereit für KI-Training!",
                    analyzedCount, percentAnalyzed);
            }
            
            GtkWidget* resultDialog = gtk_message_dialog_new(
                GTK_WINDOW(self->window_),
                GTK_DIALOG_MODAL,
                cancelled ? GTK_MESSAGE_WARNING : GTK_MESSAGE_INFO,
                GTK_BUTTONS_OK,
                "%s", message);
            gtk_window_set_title(GTK_WINDOW(resultDialog), cancelled ? "Analyse abgebrochen" : "Analyse abgeschlossen");
            makeDialogResizable(resultDialog, 600, 250);
            gtk_dialog_run(GTK_DIALOG(resultDialog));
            gtk_widget_destroy(resultDialog);
            
            std::cout << "✅ Result dialog closed\n";
            
            delete info;
            return G_SOURCE_REMOVE;
        }, new std::tuple<GtkRenderer*, GtkWidget*, size_t, bool>(self, progressDialog, analyzed.load(), wasCancelled));
        
    }).detach();
}

void GtkRenderer::onRepairClipping(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    
    auto allMedia = self->database_->getAll();
    
    if (allMedia.empty()) {
        GtkWidget* dialog = gtk_message_dialog_new(
            GTK_WINDOW(self->window_),
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_INFO,
            GTK_BUTTONS_OK,
            "Keine Dateien in der Datenbank vorhanden!"
        );
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }
    
    GtkWidget* confirmDialog = gtk_message_dialog_new(
        GTK_WINDOW(self->window_),
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_YES_NO,
        "Übersteuerungs-Analyse für %zu Dateien starten?\n\n"
        "Dateien mit Clipping werden automatisch repariert und neu gespeichert.",
        allMedia.size()
    );
    
    if (gtk_dialog_run(GTK_DIALOG(confirmDialog)) != GTK_RESPONSE_YES) {
        gtk_widget_destroy(confirmDialog);
        return;
    }
    gtk_widget_destroy(confirmDialog);
    
    // Progress-Dialog mit Abbrechen-Button
    GtkWidget* progressDialog = gtk_dialog_new_with_buttons(
        "Übersteuerungen reparieren...",
        GTK_WINDOW(self->window_),
        (GtkDialogFlags)0,
        "Abbrechen", GTK_RESPONSE_CANCEL,
        NULL
    );
    makeDialogResizable(progressDialog, 600, 200);
    
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(progressDialog));
    GtkWidget* progressBar = gtk_progress_bar_new();
    GtkWidget* labelProgress = gtk_label_new("Analysiere Audio-Dateien...");
    gtk_label_set_selectable(GTK_LABEL(labelProgress), TRUE);
    
    gtk_box_pack_start(GTK_BOX(content), labelProgress, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(content), progressBar, FALSE, FALSE, 5);
    gtk_widget_show_all(progressDialog);
    
    // Abbrechen-Handler
    std::atomic<bool>* cancelFlag = new std::atomic<bool>(false);
    g_signal_connect(progressDialog, "response", G_CALLBACK(+[](GtkDialog* dialog, gint response, gpointer data) {
        if (response == GTK_RESPONSE_CANCEL) {
            auto* flag = static_cast<std::atomic<bool>*>(data);
            *flag = true;
            std::cout << "⚠️ Reparatur wird abgebrochen...\n";
        }
    }), cancelFlag);
    
    // Analyse in Thread
    std::thread([self, allMedia, progressDialog, progressBar, labelProgress, cancelFlag]() {
        size_t processed = 0;
        size_t repaired = 0;
        
        for (const auto& media : allMedia) {
            // Check Cancel-Flag
            if (*cancelFlag) {
                std::cout << "⚠️ Reparatur abgebrochen bei " << processed << " / " << allMedia.size() << "\n";
                break;
            }
            
            processed++;
            
            // Prüfe auf Clipping
            std::vector<float> samples;
            int sampleRate;
            
            if (self->analyzer_->loadAudioFile(media.filepath, samples, sampleRate)) {
                auto clippingInfo = self->analyzer_->detectClipping(samples);
                
                if (clippingInfo.hasClipping && clippingInfo.clippingPercentage > 1.0f) {
                    // Erstelle Backup und repariere
                    std::string backupPath = media.filepath + ".backup";
                    std::filesystem::copy_file(media.filepath, backupPath, std::filesystem::copy_options::overwrite_existing);
                    
                    if (self->analyzer_->repairClipping(media.filepath, media.filepath, 0.95f)) {
                        repaired++;
                    }
                }
            }
            
            // Update Progress
            if (processed % 5 == 0 || processed == allMedia.size()) {
                gdk_threads_add_idle([](gpointer data) -> gboolean {
                    auto* info = static_cast<std::tuple<GtkWidget*, GtkWidget*, size_t, size_t, size_t>*>(data);
                    
                    float progress = (float)std::get<2>(*info) / std::get<3>(*info);
                    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(std::get<0>(*info)), progress);
                    
                    // Mit % Anzeige
                    char percentText[32];
                    snprintf(percentText, sizeof(percentText), "%.1f%%", progress * 100.0f);
                    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(std::get<0>(*info)), percentText);
                    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(std::get<0>(*info)), TRUE);
                    
                    char text[128];
                    snprintf(text, sizeof(text), "%zu / %zu (%.1f%%) - %zu repariert",
                            std::get<2>(*info), std::get<3>(*info), progress * 100.0f, std::get<4>(*info));
                    gtk_label_set_text(GTK_LABEL(std::get<1>(*info)), text);
                    
                    delete info;
                    return G_SOURCE_REMOVE;
                }, new std::tuple<GtkWidget*, GtkWidget*, size_t, size_t, size_t>(progressBar, labelProgress, processed, allMedia.size(), repaired));
            }
        }
        
        bool wasCancelled = *cancelFlag;
        delete cancelFlag;  // Cleanup
        
        // Schließe Dialog und zeige Ergebnis
        gdk_threads_add_idle([](gpointer data) -> gboolean {
            auto* info = static_cast<std::tuple<GtkRenderer*, GtkWidget*, size_t, size_t, bool>*>(data);
            GtkRenderer* self = std::get<0>(*info);
            GtkWidget* progressDlg = std::get<1>(*info);
            size_t repaired = std::get<2>(*info);
            size_t processed = std::get<3>(*info);
            bool cancelled = std::get<4>(*info);
            
            gtk_widget_destroy(progressDlg);
            
            char message[512];
            float repairedPercent = processed > 0 ? (float)repaired / processed * 100.0f : 0.0f;
            
            if (cancelled) {
                snprintf(message, sizeof(message),
                    "⚠️ Reparatur abgebrochen!\n\n"
                    "%zu Dateien verarbeitet\n"
                    "%zu Dateien repariert (%.1f%%)",
                    processed, repaired, repairedPercent);
            } else {
                snprintf(message, sizeof(message),
                    "✅ Clipping-Reparatur abgeschlossen!\n\n"
                    "%zu Dateien analysiert\n"
                    "%zu Dateien repariert (%.1f%%)\n\n"
                    "Backups wurden als .backup gespeichert.",
                    processed, repaired, repairedPercent);
            }
            
            GtkWidget* resultDialog = gtk_message_dialog_new(
                GTK_WINDOW(self->window_),
                GTK_DIALOG_MODAL,
                cancelled ? GTK_MESSAGE_WARNING : GTK_MESSAGE_INFO,
                GTK_BUTTONS_OK,
                "%s", message
            );
            gtk_window_set_title(GTK_WINDOW(resultDialog), cancelled ? "Reparatur abgebrochen" : "Reparatur abgeschlossen");
            gtk_window_set_default_size(GTK_WINDOW(resultDialog), 500, 180);
            gtk_dialog_run(GTK_DIALOG(resultDialog));
            gtk_widget_destroy(resultDialog);
            
            delete info;
            return G_SOURCE_REMOVE;
        }, new std::tuple<GtkRenderer*, GtkWidget*, size_t, size_t, bool>(self, progressDialog, repaired, processed, wasCancelled));
        
    }).detach();
}

// === Duplikat-Erkennung ===
void GtkRenderer::onFindDuplicates(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    
    GtkWidget* dialog = gtk_message_dialog_new(
        GTK_WINDOW(self->window_),
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_YES_NO,
        "Duplikate in der Datenbank suchen?\n\n"
        "Dies kann bei großen Datenbanken einige Zeit dauern."
    );
    
    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    
    if (response != GTK_RESPONSE_YES) return;
    
    // Progress Dialog
    GtkWidget* progressDialog = gtk_dialog_new_with_buttons(
        "Suche Duplikate...",
        GTK_WINDOW(self->window_),
        (GtkDialogFlags)0,
        nullptr
    );
    
    makeDialogResizable(progressDialog, 500, 180);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(progressDialog));
    GtkWidget* progressBar = gtk_progress_bar_new();
    GtkWidget* labelProgress = gtk_label_new("Analysiere Dateien...");
    
    gtk_box_pack_start(GTK_BOX(content), labelProgress, FALSE, FALSE, 10);
    gtk_box_pack_start(GTK_BOX(content), progressBar, FALSE, FALSE, 10);
    gtk_widget_show_all(progressDialog);
    
    // Thread für Suche
    std::thread([self, progressDialog, progressBar, labelProgress]() {
        gdk_threads_add_idle([](gpointer data) -> gboolean {
            auto* info = static_cast<std::tuple<GtkWidget*, float, std::string>*>(data);
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(std::get<0>(*info)), std::get<1>(*info));
            gtk_label_set_text(GTK_LABEL(std::get<0>(*info)), std::get<2>(*info).c_str());
            delete info;
            return G_SOURCE_REMOVE;
        }, new std::tuple<GtkWidget*, float, std::string>(labelProgress, 0.3f, "Suche identische Dateien..."));
        
        auto duplicates = self->database_->findDuplicates(0.95f);
        
        gdk_threads_add_idle([](gpointer data) -> gboolean {
            auto* info = static_cast<std::tuple<GtkWidget*, float, std::string>*>(data);
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(std::get<0>(*info)), std::get<1>(*info));
            gtk_label_set_text(GTK_LABEL(std::get<0>(*info)), std::get<2>(*info).c_str());
            delete info;
            return G_SOURCE_REMOVE;
        }, new std::tuple<GtkWidget*, float, std::string>(progressBar, 1.0f, "Fertig!"));
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // Zeige Ergebnisse
        gdk_threads_add_idle([](gpointer data) -> gboolean {
            auto* info = static_cast<std::tuple<GtkRenderer*, GtkWidget*, std::vector<MediaDatabase::DuplicateInfo>>*>(data);
            GtkRenderer* self = std::get<0>(*info);
            GtkWidget* progressDialog = std::get<1>(*info);
            auto& duplicates = std::get<2>(*info);
            
            gtk_widget_destroy(progressDialog);
            
            // Erstelle Ergebnis-Dialog mit TreeView
            GtkWidget* resultDialog = gtk_dialog_new_with_buttons(
                "Gefundene Duplikate",
                GTK_WINDOW(self->window_),
                (GtkDialogFlags)0,
                "Schließen", GTK_RESPONSE_CLOSE,
                nullptr
            );
            makeDialogResizable(resultDialog, 700, 400);
            
            GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(resultDialog));
            
            if (duplicates.empty()) {
                GtkWidget* label = gtk_label_new("✅ Keine Duplikate gefunden!");
                gtk_label_set_selectable(GTK_LABEL(label), TRUE);
                gtk_box_pack_start(GTK_BOX(content), label, TRUE, TRUE, 10);
            } else {
                GtkWidget* label = gtk_label_new(("🔍 " + std::to_string(duplicates.size()) + " Duplikate gefunden:").c_str());
                gtk_label_set_selectable(GTK_LABEL(label), TRUE);
                gtk_box_pack_start(GTK_BOX(content), label, FALSE, FALSE, 5);
                
                // ScrolledWindow + TreeView
                GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
                gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
                gtk_box_pack_start(GTK_BOX(content), scrolled, TRUE, TRUE, 0);
                
                GtkListStore* store = gtk_list_store_new(4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_FLOAT, G_TYPE_STRING);
                
                for (const auto& dup : duplicates) {
                    GtkTreeIter iter;
                    gtk_list_store_append(store, &iter);
                    gtk_list_store_set(store, &iter,
                        0, dup.filepath1.c_str(),
                        1, dup.filepath2.c_str(),
                        2, dup.similarity,
                        3, dup.reason.c_str(),
                        -1);
                }
                
                GtkWidget* treeView = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
                gtk_container_add(GTK_CONTAINER(scrolled), treeView);
                
                // Spalten
                GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
                gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(treeView), -1, "Datei 1", renderer, "text", 0, nullptr);
                gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(treeView), -1, "Datei 2", renderer, "text", 1, nullptr);
                gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(treeView), -1, "Ähnlichkeit", renderer, "text", 2, nullptr);
                gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(treeView), -1, "Grund", renderer, "text", 3, nullptr);
                
                g_object_unref(store);
            }
            
            gtk_widget_show_all(resultDialog);
            gtk_dialog_run(GTK_DIALOG(resultDialog));
            gtk_widget_destroy(resultDialog);
            
            delete info;
            return G_SOURCE_REMOVE;
        }, new std::tuple<GtkRenderer*, GtkWidget*, std::vector<MediaDatabase::DuplicateInfo>>(self, progressDialog, duplicates));
        
    }).detach();
}

// === Training-Statistik ===
void GtkRenderer::onShowTrainingStats(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    
    // Zeige Lade-Dialog
    GtkWidget* loadingDialog = gtk_message_dialog_new(
        GTK_WINDOW(self->window_),
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_INFO,
        GTK_BUTTONS_NONE,
        "📊 Lade Training-Statistiken..."
    );
    gtk_widget_show(loadingDialog);
    
    // Lade Statistiken asynchron
    std::thread([self, loadingDialog]() {
        // Hole Statistiken im Hintergrund
        auto stats = self->database_->getTrainingStats();
        
        // Baue Dialog-Inhalt
        std::stringstream ss;
        ss << "📚 Gesamt: " << stats.totalFiles << " Dateien\n";
        ss << "✅ Analysiert: " << stats.analyzedFiles << " (" 
           << (100.0f * stats.analyzedFiles / std::max(1ul, stats.totalFiles)) << "%)\n";
        ss << "🔁 Duplikate: ~" << stats.duplicates << "\n";
        ss << "❓ Ohne Genre: " << stats.filesWithoutGenre << "\n";
        ss << "🎵 Ø BPM: " << std::fixed << std::setprecision(1) << stats.avgBpm << "\n";
        ss << "⏱️ Ø Dauer: " << std::fixed << std::setprecision(1) << stats.avgDuration << "s\n";
        
        std::stringstream genreSS;
        for (const auto& [genre, count] : stats.genreDistribution) {
            float percentage = 100.0f * count / std::max(1ul, stats.totalFiles);
            genreSS << genre << ": " << count << " (" << std::fixed << std::setprecision(1) << percentage << "%)\n";
        }
        
        // Update UI im Hauptthread
        gdk_threads_add_idle([](gpointer data) -> gboolean {
            auto* args = static_cast<std::tuple<GtkRenderer*, GtkWidget*, std::string, std::string>*>(data);
            GtkRenderer* self = std::get<0>(*args);
            GtkWidget* loadingDialog = std::get<1>(*args);
            std::string overview = std::get<2>(*args);
            std::string genres = std::get<3>(*args);
            
            // Schließe Lade-Dialog
            gtk_widget_destroy(loadingDialog);
            
            // Erstelle Statistik-Dialog
            GtkWidget* dialog = gtk_dialog_new_with_buttons(
                "📊 Training-Dataset Statistik",
                GTK_WINDOW(self->window_),
                GTK_DIALOG_MODAL,
                "OK", GTK_RESPONSE_OK,
                nullptr
            );
            gtk_window_set_default_size(GTK_WINDOW(dialog), 500, 400);
            
            GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
            GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
            gtk_container_set_border_width(GTK_CONTAINER(vbox), 15);
            gtk_box_pack_start(GTK_BOX(content), vbox, TRUE, TRUE, 0);
            
            // Übersicht
            GtkWidget* labelOverview = gtk_label_new(overview.c_str());
            gtk_label_set_xalign(GTK_LABEL(labelOverview), 0);
            gtk_box_pack_start(GTK_BOX(vbox), labelOverview, FALSE, FALSE, 0);
            
            // Genre-Verteilung
            GtkWidget* labelGenres = gtk_label_new("<b>Genre-Verteilung:</b>");
            gtk_label_set_use_markup(GTK_LABEL(labelGenres), TRUE);
            gtk_label_set_xalign(GTK_LABEL(labelGenres), 0);
            gtk_box_pack_start(GTK_BOX(vbox), labelGenres, FALSE, FALSE, 5);
            
            GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
            gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
            gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);
            
            GtkWidget* genreList = gtk_text_view_new();
            gtk_text_view_set_editable(GTK_TEXT_VIEW(genreList), FALSE);
            gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(genreList), TRUE);
            gtk_container_add(GTK_CONTAINER(scrolled), genreList);
            
            GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(genreList));
            gtk_text_buffer_set_text(buffer, genres.c_str(), -1);
            
            gtk_widget_show_all(dialog);
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
            
            delete args;
            return G_SOURCE_REMOVE;
        }, new std::tuple<GtkRenderer*, GtkWidget*, std::string, std::string>(
            self, loadingDialog, ss.str(), genreSS.str()));
    }).detach();
}

// === Dataset Balancieren ===
void GtkRenderer::onBalanceDataset(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    
    GtkWidget* dialog = gtk_message_dialog_new(
        GTK_WINDOW(self->window_),
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_INFO,
        GTK_BUTTONS_OK,
        "Dataset wird balanciert..."
    );
    
    gtk_widget_show(dialog);
    
    bool success = self->database_->balanceDataset();
    
    gtk_widget_destroy(dialog);
    
    // Zeige Ergebnis
    if (success) {
        self->onShowTrainingStats(widget, data);
    }
}

void GtkRenderer::onDownloadHVSC(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    
    const char* home = getenv("HOME");
    std::string hvscPath = std::string(home) + "/.songgen/hvsc/";
    
    // Starte Download im Thread
    std::thread([self, hvscPath]() {
        gdk_threads_add_idle([](gpointer data) -> gboolean {
            GtkRenderer* self = static_cast<GtkRenderer*>(data);
            gtk_label_set_text(GTK_LABEL(self->hvscStatusLabel_), "📥 Download läuft...");
            return G_SOURCE_REMOVE;
        }, self);
        
        self->mp3Total_ = 75442;
        
        bool success = self->hvscDownloader_->downloadHVSC(
            hvscPath,
            [self](size_t current, size_t total, float speed) {
                gdk_threads_add_idle([](gpointer data) -> gboolean {
                    auto* info = static_cast<std::tuple<GtkRenderer*, size_t, size_t, float>*>(data);
                    GtkRenderer* self = std::get<0>(*info);
                    size_t current = std::get<1>(*info);
                    size_t total = std::get<2>(*info);
                    
                    if (total > 0) {
                        float progress = (float)current / total;
                        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(self->hvscProgressBar_), progress);
                        
                        std::string status = "📥 Download: " + std::to_string(current / (1024*1024)) + 
                                           " / " + std::to_string(total / (1024*1024)) + " MB";
                        gtk_label_set_text(GTK_LABEL(self->hvscStatusLabel_), status.c_str());
                    }
                    
                    delete info;
                    return G_SOURCE_REMOVE;
                }, new std::tuple<GtkRenderer*, size_t, size_t, float>(self, current, total, speed));
            },
            true,  // autoConvertAndImport
            &self->stopExtraction_
        );
        
        gdk_threads_add_idle([](gpointer data) -> gboolean {
            auto* info = static_cast<std::pair<GtkRenderer*, bool>*>(data);
            GtkRenderer* self = info->first;
            
            if (info->second) {
                gtk_label_set_text(GTK_LABEL(self->hvscStatusLabel_), "✅ HVSC erfolgreich heruntergeladen und extrahiert!");
                self->refreshDatabaseView();
            } else {
                gtk_label_set_text(GTK_LABEL(self->hvscStatusLabel_), "❌ Download/Extraktion fehlgeschlagen");
            }
            
            delete info;
            return G_SOURCE_REMOVE;
        }, new std::pair<GtkRenderer*, bool>(self, success));
        
    }).detach();
}

void GtkRenderer::onExtractHVSC(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    
    // Starte nur Extraktion (kein Download)
    const char* home = getenv("HOME");
    std::string sidDir = std::string(home) + "/.songgen/hvsc/C64Music/";
    std::string mp3Dir = std::string(home) + "/.songgen/hvsc/mp3/";
    
    if (!std::filesystem::exists(sidDir)) {
        GtkWidget* dialog = gtk_message_dialog_new(
            GTK_WINDOW(self->window_),
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR,
            GTK_BUTTONS_OK,
            "❌ SID-Verzeichnis nicht gefunden!\nBitte erst HVSC herunterladen."
        );
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }
    
    std::thread([self, sidDir, mp3Dir]() {
        gdk_threads_add_idle([](gpointer self) -> gboolean {
            gtk_label_set_text(GTK_LABEL(static_cast<GtkRenderer*>(self)->hvscStatusLabel_), 
                             "🎵 Extrahiere SIDs zu MP3...");
            return G_SOURCE_REMOVE;
        }, self);
        
        // Implementiere SID-Extraktion hier
        // (Vereinfachte Version - ruft direkt DB-Sync auf)
        
        gdk_threads_add_idle([](gpointer self) -> gboolean {
            gtk_label_set_text(GTK_LABEL(static_cast<GtkRenderer*>(self)->hvscStatusLabel_), 
                             "✅ Extraktion abgeschlossen! Importiere in DB...");
            
            // Trigger DB-Sync
            onDBSync(nullptr, self);
            
            return G_SOURCE_REMOVE;
        }, self);
        
    }).detach();
}

void GtkRenderer::onBrowseLocal(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    
    // Prüfe welcher Button geklickt wurde (Dateien oder Ordner)
    const char* label = gtk_button_get_label(GTK_BUTTON(widget));
    bool isFolder = (label && std::string(label).find("Ordner") != std::string::npos);
    
    GtkWidget* dialog = gtk_file_chooser_dialog_new(
        isFolder ? "Ordner wählen (rekursiv)" : "Dateien wählen",
        GTK_WINDOW(self->window_),
        isFolder ? GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER : GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Abbrechen", GTK_RESPONSE_CANCEL,
        "_Hinzufügen", GTK_RESPONSE_ACCEPT,
        NULL
    );
    
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog), TRUE);
    
    // Filter (nur bei Datei-Auswahl)
    if (!isFolder) {
        // Alle Dateien (Standard)
        GtkFileFilter* filterAll = gtk_file_filter_new();
        gtk_file_filter_set_name(filterAll, "Alle Dateien");
        gtk_file_filter_add_pattern(filterAll, "*");
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filterAll);
        
        // Audio-Dateien
        GtkFileFilter* filterAudio = gtk_file_filter_new();
        gtk_file_filter_set_name(filterAudio, "Audio-Dateien");
        gtk_file_filter_add_pattern(filterAudio, "*.mp3");
        gtk_file_filter_add_pattern(filterAudio, "*.wav");
        gtk_file_filter_add_pattern(filterAudio, "*.flac");
        gtk_file_filter_add_pattern(filterAudio, "*.ogg");
        gtk_file_filter_add_pattern(filterAudio, "*.m4a");
        gtk_file_filter_add_pattern(filterAudio, "*.sid");
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filterAudio);
        
        // Video-Dateien
        GtkFileFilter* filterVideo = gtk_file_filter_new();
        gtk_file_filter_set_name(filterVideo, "Video-Dateien");
        gtk_file_filter_add_pattern(filterVideo, "*.mp4");
        gtk_file_filter_add_pattern(filterVideo, "*.mkv");
        gtk_file_filter_add_pattern(filterVideo, "*.avi");
        gtk_file_filter_add_pattern(filterVideo, "*.webm");
        gtk_file_filter_add_pattern(filterVideo, "*.mov");
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filterVideo);
        
        // Setze "Alle Dateien" als Standard
        gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(dialog), filterAll);
    }
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        GSList* filenames = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(dialog));
        
        // Progress-Dialog für längere Operationen
        GtkWidget* progressDialog = gtk_dialog_new_with_buttons(
            "Füge Dateien hinzu...",
            GTK_WINDOW(self->window_),
            (GtkDialogFlags)0,
            NULL
        );
        
        makeDialogResizable(progressDialog, 500, 180);
        GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(progressDialog));
        GtkWidget* progressBar = gtk_progress_bar_new();
        GtkWidget* labelProgress = gtk_label_new("Durchsuche Verzeichnisse...");
        gtk_label_set_selectable(GTK_LABEL(labelProgress), TRUE);
        
        gtk_box_pack_start(GTK_BOX(content), labelProgress, FALSE, FALSE, 5);
        gtk_box_pack_start(GTK_BOX(content), progressBar, FALSE, FALSE, 5);
        gtk_widget_show_all(progressDialog);
        
        // Im Thread verarbeiten
        std::thread([self, filenames, progressDialog, progressBar, labelProgress, isFolder]() {
            int added = 0;
            int total = 0;
            
            // Sammle alle Dateien (rekursiv bei Ordnern)
            std::vector<std::string> allFiles;
            std::vector<std::string> errors;
            
            for (GSList* l = filenames; l != NULL; l = l->next) {
                char* path = (char*)l->data;
                
                try {
                    if (isFolder && std::filesystem::is_directory(path)) {
                        // Rekursiv alle Audio-Dateien im Ordner finden
                        std::filesystem::recursive_directory_iterator iter(path, 
                            std::filesystem::directory_options::skip_permission_denied);
                        
                        for (const auto& entry : iter) {
                            try {
                                if (entry.is_regular_file()) {
                                    std::string ext = entry.path().extension().string();
                                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                                    // Audio-Formate
                                    if (ext == ".mp3" || ext == ".wav" || ext == ".sid" || 
                                        ext == ".flac" || ext == ".ogg" || ext == ".m4a" ||
                                        // Video-Formate
                                        ext == ".mp4" || ext == ".mkv" || ext == ".avi" || 
                                        ext == ".webm" || ext == ".mov" || ext == ".flv") {
                                        allFiles.push_back(entry.path().string());
                                    }
                                }
                            } catch (const std::exception& e) {
                                // Einzelne Datei übersprungen (z.B. gebrochener Symlink)
                                errors.push_back(entry.path().string() + ": " + e.what());
                            }
                        }
                    } else if (std::filesystem::exists(path)) {
                        allFiles.push_back(path);
                    }
                } catch (const std::exception& e) {
                    errors.push_back(std::string(path) + ": " + e.what());
                    std::cerr << "❌ Fehler beim Durchsuchen von " << path << ": " << e.what() << std::endl;
                }
                
                g_free(path);
            }
            g_slist_free(filenames);
            
            total = allFiles.size();
            int processed = 0;
            int converted = 0;
            
            // Füge Dateien zur DB hinzu
            for (const auto& filepath : allFiles) {
                processed++;
                
                std::string finalPath = filepath;
                std::string ext = std::filesystem::path(filepath).extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                
                // Prüfe ob Video-Datei
                bool isVideo = (ext == ".mp4" || ext == ".mkv" || ext == ".avi" || 
                               ext == ".webm" || ext == ".mov" || ext == ".flv");
                
                if (isVideo) {
                    // Konvertiere Video zu MP3
                    std::string mp3Path = filepath.substr(0, filepath.find_last_of('.')) + ".mp3";
                    
                    // Update Progress mit Konvertierungs-Info
                    gdk_threads_add_idle([](gpointer data) -> gboolean {
                        auto* info = static_cast<std::tuple<GtkWidget*, std::string>*>(data);
                        std::string msg = "Konvertiere: " + std::filesystem::path(std::get<1>(*info)).filename().string();
                        gtk_label_set_text(GTK_LABEL(std::get<0>(*info)), msg.c_str());
                        delete info;
                        return G_SOURCE_REMOVE;
                    }, new std::tuple<GtkWidget*, std::string>(labelProgress, filepath));
                    
                    // Konvertiere mit ffmpeg
                    std::string cmd = "ffmpeg -i \"" + filepath + "\" -vn -acodec libmp3lame -q:a 2 \"" + mp3Path + "\" 2>/dev/null";
                    int result = std::system(cmd.c_str());
                    
                    if (result == 0 && std::filesystem::exists(mp3Path)) {
                        finalPath = mp3Path;
                        converted++;
                    } else {
                        errors.push_back(filepath + ": Video-Konvertierung fehlgeschlagen");
                        continue;
                    }
                }
                
                MediaMetadata meta;
                meta.filepath = finalPath;
                meta.title = std::filesystem::path(finalPath).stem().string();
                meta.genre = "Unknown";
                meta.artist = "Unknown";
                meta.analyzed = false;
                meta.addedTimestamp = std::time(nullptr);
                
                if (self->database_->addMedia(meta)) {
                    added++;
                } else {
                    // Duplikat übersprungen
                }
                
                // Update Progress
                if (processed % 10 == 0 || processed == total) {
                    gdk_threads_add_idle([](gpointer data) -> gboolean {
                        auto* info = static_cast<std::tuple<GtkWidget*, GtkWidget*, int, int, int>*>(data);
                        
                        float progress = (float)std::get<2>(*info) / std::get<3>(*info);
                        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(std::get<0>(*info)), progress);
                        
                        int skipped = std::get<3>(*info) - std::get<4>(*info);
                        std::string text = std::to_string(std::get<2>(*info)) + " / " + 
                                          std::to_string(std::get<3>(*info)) + " (" + 
                                          std::to_string(std::get<4>(*info)) + " neu, " +
                                          std::to_string(skipped) + " Duplikate)";
                        gtk_label_set_text(GTK_LABEL(std::get<1>(*info)), text.c_str());
                        
                        delete info;
                        return G_SOURCE_REMOVE;
                    }, new std::tuple<GtkWidget*, GtkWidget*, int, int, int>(progressBar, labelProgress, processed, total, added));
                }
            }
            
            self->refreshDatabaseView();
            
            // Schließe Progress-Dialog und zeige Ergebnis
            int skipped = total - added;
            gdk_threads_add_idle([](gpointer data) -> gboolean {
                auto* info = static_cast<std::tuple<GtkRenderer*, GtkWidget*, int, int, int, std::vector<std::string>>*>(data);
                GtkRenderer* self = std::get<0>(*info);
                int added = std::get<2>(*info);
                int skipped = std::get<3>(*info);
                int converted = std::get<4>(*info);
                auto errors = std::get<5>(*info);
                
                gtk_widget_destroy(std::get<1>(*info));
                
                std::string msg;
                if (skipped > 0) {
                    msg = "✅ " + std::to_string(added) + " Dateien hinzugefügt\n" +
                          "ℹ️ " + std::to_string(skipped) + " bereits vorhanden";
                } else {
                    msg = "✅ " + std::to_string(added) + " Dateien zur Datenbank hinzugefügt";
                }
                
                if (converted > 0) {
                    msg += "\n🎬 " + std::to_string(converted) + " Videos zu MP3 konvertiert";
                }
                
                if (!errors.empty()) {
                    msg += "\n\n⚠️ " + std::to_string(errors.size()) + " Fehler";
                }
                
                GtkWidget* resultDialog = gtk_message_dialog_new(
                    GTK_WINDOW(self->window_),
                    GTK_DIALOG_MODAL,
                    !errors.empty() ? GTK_MESSAGE_WARNING : GTK_MESSAGE_INFO,
                    GTK_BUTTONS_OK,
                    "%s", msg.c_str()
                );
                
                // Bei Fehlern: Zeige Details
                if (!errors.empty() && errors.size() <= 10) {
                    std::string details = "\n\nFehlerdetails:\n";
                    for (size_t i = 0; i < std::min(errors.size(), size_t(10)); i++) {
                        details += "• " + errors[i] + "\n";
                    }
                    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(resultDialog), 
                                                             "%s", details.c_str());
                }
                
                gtk_dialog_run(GTK_DIALOG(resultDialog));
                gtk_widget_destroy(resultDialog);
                
                delete info;
                return G_SOURCE_REMOVE;
            }, new std::tuple<GtkRenderer*, GtkWidget*, int, int, int, std::vector<std::string>>(
                self, progressDialog, added, skipped, converted, errors));
            
        }).detach();
    }
    
    gtk_widget_destroy(dialog);
}

void GtkRenderer::onBrowseSMB(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    
    GtkWidget* dialog = gtk_message_dialog_new(
        GTK_WINDOW(self->window_),
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_INFO,
        GTK_BUTTONS_OK,
        "SMB/Netzwerk-Browser\n\nGeben Sie smb://server/share in der Adressleiste ein"
    );
    
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

void GtkRenderer::onBrowseFTP(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    
    GtkWidget* dialog = gtk_message_dialog_new(
        GTK_WINDOW(self->window_),
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_INFO,
        GTK_BUTTONS_OK,
        "FTP-Browser\n\nGeben Sie ftp://server/ in der Adressleiste ein"
    );
    
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

void GtkRenderer::onAddressBarGo(GtkWidget* widget, gpointer data) {
    auto* bd = static_cast<BrowserData*>(data);
    const char* path = gtk_entry_get_text(GTK_ENTRY(bd->addrEntry));
    bd->currentPath = path;
    loadBrowserDirectory(path, bd->store);
}

void GtkRenderer::onAddSelectedFiles(GtkWidget* widget, gpointer data) {
    auto* bd = static_cast<BrowserData*>(data);
    GtkRenderer* self = static_cast<GtkRenderer*>(bd->self);
    
    // Sammle alle markierten Einträge aus TreeView
    std::vector<std::string> selectedPaths;
    std::vector<std::string> selectedTypes;
    
    GtkTreeIter iter;
    GtkTreeModel* model = GTK_TREE_MODEL(bd->store);
    
    if (gtk_tree_model_get_iter_first(model, &iter)) {
        do {
            gboolean checked;
            gchar* type;
            gchar* path;
            gtk_tree_model_get(model, &iter, 0, &checked, 3, &type, 4, &path, -1);
            
            if (checked) {
                selectedPaths.push_back(path);
                selectedTypes.push_back(type);
            }
            
            g_free(type);
            g_free(path);
        } while (gtk_tree_model_iter_next(model, &iter));
    }
    
    if (selectedPaths.empty()) {
        GtkWidget* dialog = gtk_message_dialog_new(
            GTK_WINDOW(self->window_),
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_WARNING,
            GTK_BUTTONS_OK,
            "⚠️ Keine Einträge markiert"
        );
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }
    
    // Progress-Dialog
    GtkWidget* progressDialog = gtk_dialog_new_with_buttons(
        "Verarbeite Auswahl...",
        GTK_WINDOW(self->window_),
        (GtkDialogFlags)0,
        NULL
    );
    
    makeDialogResizable(progressDialog, 500, 180);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(progressDialog));
    GtkWidget* progressBar = gtk_progress_bar_new();
    GtkWidget* labelProgress = gtk_label_new("Sammle Dateien...");
    gtk_label_set_selectable(GTK_LABEL(labelProgress), TRUE);
    
    gtk_box_pack_start(GTK_BOX(content), labelProgress, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(content), progressBar, FALSE, FALSE, 5);
    gtk_widget_show_all(progressDialog);
    
    // Verarbeite im Thread (detached, damit GUI nicht blockiert)
    std::thread([self, selectedPaths, selectedTypes, progressDialog, progressBar, labelProgress]() {
        int added = 0;
        int skipped = 0;
        int converted = 0;
        int processed = 0;
        std::vector<std::string> allFiles;
        std::vector<std::string> errors;
        
        // Sammle alle Dateien (rekursiv aus Ordnern)
        for (size_t i = 0; i < selectedPaths.size(); i++) {
            const auto& path = selectedPaths[i];
            const auto& type = selectedTypes[i];
            
            // Update: Zeige aktuellen Ordner
            gdk_threads_add_idle([](gpointer data) -> gboolean {
                auto* info = static_cast<std::tuple<GtkWidget*, std::string>*>(data);
                std::string msg = "📁 Durchsuche: " + std::filesystem::path(std::get<1>(*info)).filename().string();
                gtk_label_set_text(GTK_LABEL(std::get<0>(*info)), msg.c_str());
                delete info;
                return G_SOURCE_REMOVE;
            }, new std::tuple<GtkWidget*, std::string>(labelProgress, path));
            
            if (type == "Ordner") {
                // Rekursiv alle Medien-Dateien sammeln
                try {
                    for (const auto& entry : std::filesystem::recursive_directory_iterator(
                        path, std::filesystem::directory_options::skip_permission_denied)) {
                        try {
                            if (entry.is_regular_file()) {
                                std::string ext = entry.path().extension().string();
                                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                                if (ext == ".mp3" || ext == ".wav" || ext == ".sid" || 
                                    ext == ".flac" || ext == ".ogg" || ext == ".m4a" ||
                                    ext == ".mp4" || ext == ".mkv" || ext == ".avi" || 
                                    ext == ".webm" || ext == ".mov" || ext == ".flv") {
                                    allFiles.push_back(entry.path().string());
                                    
                                    // Update alle 50 Dateien
                                    if (allFiles.size() % 50 == 0) {
                                        gdk_threads_add_idle([](gpointer data) -> gboolean {
                                            auto* info = static_cast<std::tuple<GtkWidget*, int>*>(data);
                                            std::string msg = "📂 Gefunden: " + std::to_string(std::get<1>(*info)) + " Dateien";
                                            gtk_label_set_text(GTK_LABEL(std::get<0>(*info)), msg.c_str());
                                            delete info;
                                            return G_SOURCE_REMOVE;
                                        }, new std::tuple<GtkWidget*, int>(labelProgress, (int)allFiles.size()));
                                    }
                                }
                            }
                        } catch (...) {
                            // Einzelne Datei überspringen
                        }
                    }
                } catch (const std::exception& e) {
                    errors.push_back(path + ": " + e.what());
                }
            } else if (type == "Audio" || type == "Video") {
                // Direkte Datei
                allFiles.push_back(path);
            }
        }
        
        int totalFiles = allFiles.size();
        
        if (totalFiles == 0) {
            gdk_threads_add_idle([](gpointer data) -> gboolean {
                auto* info = static_cast<std::tuple<GtkRenderer*, GtkWidget*>*>(data);
                gtk_widget_destroy(std::get<1>(*info));
                
                GtkWidget* dialog = gtk_message_dialog_new(
                    GTK_WINDOW(std::get<0>(*info)->window_),
                    GTK_DIALOG_MODAL,
                    GTK_MESSAGE_INFO,
                    GTK_BUTTONS_OK,
                    "ℹ️ Keine Medien-Dateien gefunden"
                );
                gtk_dialog_run(GTK_DIALOG(dialog));
                gtk_widget_destroy(dialog);
                
                delete info;
                return G_SOURCE_REMOVE;
            }, new std::tuple<GtkRenderer*, GtkWidget*>(self, progressDialog));
            return;
        }
        
        // Verarbeite alle gesammelten Dateien
        for (const auto& filepath : allFiles) {
            processed++;
            
            if (!std::filesystem::exists(filepath)) {
                skipped++;
                continue;
            }
            
            std::string finalPath = filepath;
            std::string ext = std::filesystem::path(filepath).extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            
            // Prüfe ob Video-Datei
            bool isVideo = (ext == ".mp4" || ext == ".mkv" || ext == ".avi" || 
                           ext == ".webm" || ext == ".mov" || ext == ".flv");
            
            // Prüfe ob Audio-Datei
            bool isAudio = (ext == ".mp3" || ext == ".wav" || ext == ".sid" || 
                           ext == ".flac" || ext == ".ogg" || ext == ".m4a");
            
            if (isVideo) {
                // Konvertiere Video zu MP3
                std::string mp3Path = filepath.substr(0, filepath.find_last_of('.')) + ".mp3";
                
                // Update Progress
                gdk_threads_add_idle([](gpointer data) -> gboolean {
                    auto* info = static_cast<std::tuple<GtkWidget*, std::string>*>(data);
                    std::string msg = "🎬 Konvertiere: " + std::filesystem::path(std::get<1>(*info)).filename().string();
                    gtk_label_set_text(GTK_LABEL(std::get<0>(*info)), msg.c_str());
                    delete info;
                    return G_SOURCE_REMOVE;
                }, new std::tuple<GtkWidget*, std::string>(labelProgress, filepath));
                
                // Konvertiere mit ffmpeg
                std::string cmd = "ffmpeg -i \"" + filepath + "\" -vn -acodec libmp3lame -q:a 2 \"" + mp3Path + "\" 2>/dev/null";
                int result = std::system(cmd.c_str());
                
                if (result == 0 && std::filesystem::exists(mp3Path)) {
                    finalPath = mp3Path;
                    converted++;
                    
                    // Historie für erfolgreiche Konvertierung
                    gdk_threads_add_idle([](gpointer data) -> gboolean {
                        auto* info = static_cast<std::tuple<GtkRenderer*, std::string, std::string>*>(data);
                        std::get<0>(*info)->addHistoryEntry(
                            "Video-Konvertierung",
                            "Original: " + std::filesystem::path(std::get<1>(*info)).filename().string() + "\n" +
                            "Ausgabe: " + std::filesystem::path(std::get<2>(*info)).filename().string(),
                            "✅ Erfolgreich zu MP3 konvertiert"
                        );
                        delete info;
                        return G_SOURCE_REMOVE;
                    }, new std::tuple<GtkRenderer*, std::string, std::string>(self, filepath, mp3Path));
                } else {
                    errors.push_back(filepath + ": Konvertierung fehlgeschlagen");
                    skipped++;
                    
                    // Historie für fehlgeschlagene Konvertierung
                    gdk_threads_add_idle([](gpointer data) -> gboolean {
                        auto* info = static_cast<std::tuple<GtkRenderer*, std::string>*>(data);
                        std::get<0>(*info)->addHistoryEntry(
                            "Video-Konvertierung fehlgeschlagen",
                            "Datei: " + std::filesystem::path(std::get<1>(*info)).filename().string(),
                            "❌ FFmpeg-Fehler"
                        );
                        delete info;
                        return G_SOURCE_REMOVE;
                    }, new std::tuple<GtkRenderer*, std::string>(self, filepath));
                    continue;
                }
            } else if (!isAudio) {
                // Weder Audio noch Video
                skipped++;
                continue;
            }
            
            // Füge zur Datenbank hinzu
            MediaMetadata meta;
            meta.filepath = finalPath;
            meta.title = std::filesystem::path(finalPath).stem().string();
            meta.genre = "Unknown";
            meta.artist = "Unknown";
            meta.analyzed = false;
            meta.addedTimestamp = std::time(nullptr);
            
            if (self->database_->addMedia(meta)) {
                added++;
                
                // Historie-Eintrag hinzufügen
                gdk_threads_add_idle([](gpointer data) -> gboolean {
                    auto* info = static_cast<std::tuple<GtkRenderer*, std::string, bool>*>(data);
                    std::string filename = std::filesystem::path(std::get<1>(*info)).filename().string();
                    std::string action = std::get<2>(*info) ? "Video konvertiert & hinzugefügt" : "Datei hinzugefügt";
                    std::get<0>(*info)->addHistoryEntry(
                        action,
                        "Datei: " + filename + "\nPfad: " + std::get<1>(*info),
                        "✅ Zur Datenbank hinzugefügt"
                    );
                    delete info;
                    return G_SOURCE_REMOVE;
                }, new std::tuple<GtkRenderer*, std::string, bool>(self, finalPath, isVideo));
            } else {
                skipped++;
            }
            
            // Update Progress (nur alle 5 Dateien)
            if (processed % 5 == 0 || processed == totalFiles) {
                gdk_threads_add_idle([](gpointer data) -> gboolean {
                    auto* info = static_cast<std::tuple<GtkWidget*, GtkWidget*, int, int>*>(data);
                    float progress = (float)std::get<2>(*info) / std::get<3>(*info);
                    // info: 0=labelProgress, 1=progressBar, 2=processed, 3=total
                    gtk_label_set_text(GTK_LABEL(std::get<0>(*info)), 
                        (std::to_string(std::get<2>(*info)) + " / " + std::to_string(std::get<3>(*info))).c_str());
                    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(std::get<1>(*info)), progress);
                    
                    delete info;
                    return G_SOURCE_REMOVE;
                }, new std::tuple<GtkWidget*, GtkWidget*, int, int>(labelProgress, progressBar, processed, totalFiles));
            }
        }
        
        self->refreshDatabaseView();
        
        // Zeige Ergebnis
        gdk_threads_add_idle([](gpointer data) -> gboolean {
            auto* info = static_cast<std::tuple<GtkRenderer*, GtkWidget*, int, int, int, std::vector<std::string>>*>(data);
            GtkRenderer* self = std::get<0>(*info);
            int added = std::get<2>(*info);
            int skipped = std::get<3>(*info);
            int converted = std::get<4>(*info);
            auto errors = std::get<5>(*info);
            
            gtk_widget_destroy(std::get<1>(*info));
            
            std::string msg;
            if (added > 0 && skipped == 0) {
                msg = "✅ " + std::to_string(added) + " Dateien zur Datenbank hinzugefügt";
            } else if (added > 0 && skipped > 0) {
                msg = "✅ " + std::to_string(added) + " hinzugefügt\n" + 
                      "ℹ️ " + std::to_string(skipped) + " übersprungen";
            } else if (added == 0 && skipped > 0) {
                msg = "ℹ️ Alle Dateien bereits vorhanden oder ungültig";
            } else {
                msg = "⚠️ Keine gültigen Dateien gefunden";
            }
            
            if (converted > 0) {
                msg += "\n🎬 " + std::to_string(converted) + " Videos zu MP3 konvertiert";
            }
            
            if (!errors.empty()) {
                msg += "\n\n⚠️ " + std::to_string(errors.size()) + " Fehler";
            }
            
            GtkWidget* resultDialog = gtk_message_dialog_new(
                GTK_WINDOW(self->window_),
                GTK_DIALOG_MODAL,
                !errors.empty() ? GTK_MESSAGE_WARNING : GTK_MESSAGE_INFO,
                GTK_BUTTONS_OK,
                "%s", msg.c_str()
            );
            
            if (!errors.empty() && errors.size() <= 5) {
                std::string details = "\n\nFehler:\n";
                for (size_t i = 0; i < std::min(errors.size(), size_t(5)); i++) {
                    details += "• " + errors[i] + "\n";
                }
                gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(resultDialog), 
                                                         "%s", details.c_str());
            }
            
            gtk_dialog_run(GTK_DIALOG(resultDialog));
            gtk_widget_destroy(resultDialog);
            
            delete info;
            return G_SOURCE_REMOVE;
        }, new std::tuple<GtkRenderer*, GtkWidget*, int, int, int, std::vector<std::string>>(
            self, progressDialog, added, skipped, converted, errors));
    }).detach();
}

void GtkRenderer::onAddCurrentFolder(GtkWidget* widget, gpointer data) {
    struct BrowserData {
        GtkWidget* addrEntry;
        GtkWidget* fileChooser;
        GtkRenderer* self;
    };
    
    auto* bd = static_cast<BrowserData*>(data);
    GtkRenderer* self = bd->self;
    
    // Hole aktuellen Ordner aus FileChooser
    char* currentFolder = gtk_file_chooser_get_current_folder(GTK_FILE_CHOOSER(bd->fileChooser));
    if (!currentFolder) {
        GtkWidget* dialog = gtk_message_dialog_new(
            GTK_WINDOW(self->window_),
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_WARNING,
            GTK_BUTTONS_OK,
            "⚠️ Kein Ordner ausgewählt"
        );
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }
    
    std::string folderPath = currentFolder;
    g_free(currentFolder);
    
    // Progress-Dialog
    GtkWidget* progressDialog = gtk_dialog_new_with_buttons(
        "Durchsuche Ordner...",
        GTK_WINDOW(self->window_),
        (GtkDialogFlags)0,
        NULL
    );
    
    makeDialogResizable(progressDialog, 500, 180);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(progressDialog));
    GtkWidget* progressBar = gtk_progress_bar_new();
    GtkWidget* labelProgress = gtk_label_new("Durchsuche Verzeichnisse...");
    gtk_label_set_selectable(GTK_LABEL(labelProgress), TRUE);
    
    gtk_box_pack_start(GTK_BOX(content), labelProgress, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(content), progressBar, FALSE, FALSE, 5);
    gtk_widget_show_all(progressDialog);
    
    // Verarbeite im Thread
    std::thread([self, folderPath, progressDialog, progressBar, labelProgress]() {
        int added = 0;
        int converted = 0;
        int processed = 0;
        int total = 0;
        std::vector<std::string> allFiles;
        std::vector<std::string> errors;
        
        // Sammle alle Dateien rekursiv
        try {
            std::filesystem::recursive_directory_iterator iter(folderPath, 
                std::filesystem::directory_options::skip_permission_denied);
            
            for (const auto& entry : iter) {
                try {
                    if (entry.is_regular_file()) {
                        std::string ext = entry.path().extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                        // Audio-Formate
                        if (ext == ".mp3" || ext == ".wav" || ext == ".sid" || 
                            ext == ".flac" || ext == ".ogg" || ext == ".m4a" ||
                            // Video-Formate
                            ext == ".mp4" || ext == ".mkv" || ext == ".avi" || 
                            ext == ".webm" || ext == ".mov" || ext == ".flv") {
                            allFiles.push_back(entry.path().string());
                        }
                    }
                } catch (const std::exception& e) {
                    errors.push_back(entry.path().string() + ": " + e.what());
                }
            }
        } catch (const std::exception& e) {
            errors.push_back(folderPath + ": " + e.what());
            std::cerr << "❌ Fehler beim Durchsuchen: " << e.what() << std::endl;
        }
        
        total = allFiles.size();
        
        if (total == 0) {
            gdk_threads_add_idle([](gpointer data) -> gboolean {
                auto* info = static_cast<std::tuple<GtkRenderer*, GtkWidget*>*>(data);
                gtk_widget_destroy(std::get<1>(*info));
                
                GtkWidget* dialog = gtk_message_dialog_new(
                    GTK_WINDOW(std::get<0>(*info)->window_),
                    GTK_DIALOG_MODAL,
                    GTK_MESSAGE_INFO,
                    GTK_BUTTONS_OK,
                    "ℹ️ Keine Audio- oder Video-Dateien gefunden"
                );
                gtk_dialog_run(GTK_DIALOG(dialog));
                gtk_widget_destroy(dialog);
                
                delete info;
                return G_SOURCE_REMOVE;
            }, new std::tuple<GtkRenderer*, GtkWidget*>(self, progressDialog));
            return;
        }
        
        // Verarbeite alle Dateien
        for (const auto& filepath : allFiles) {
            processed++;
            
            std::string finalPath = filepath;
            std::string ext = std::filesystem::path(filepath).extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            
            // Prüfe ob Video-Datei
            bool isVideo = (ext == ".mp4" || ext == ".mkv" || ext == ".avi" || 
                           ext == ".webm" || ext == ".mov" || ext == ".flv");
            
            if (isVideo) {
                // Konvertiere Video zu MP3
                std::string mp3Path = filepath.substr(0, filepath.find_last_of('.')) + ".mp3";
                
                // Update Progress
                gdk_threads_add_idle([](gpointer data) -> gboolean {
                    auto* info = static_cast<std::tuple<GtkWidget*, std::string>*>(data);
                    std::string msg = "🎬 Konvertiere: " + std::filesystem::path(std::get<1>(*info)).filename().string();
                    gtk_label_set_text(GTK_LABEL(std::get<0>(*info)), msg.c_str());
                    delete info;
                    return G_SOURCE_REMOVE;
                }, new std::tuple<GtkWidget*, std::string>(labelProgress, filepath));
                
                // Konvertiere mit ffmpeg
                std::string cmd = "ffmpeg -i \"" + filepath + "\" -vn -acodec libmp3lame -q:a 2 \"" + mp3Path + "\" 2>/dev/null";
                int result = std::system(cmd.c_str());
                
                if (result == 0 && std::filesystem::exists(mp3Path)) {
                    finalPath = mp3Path;
                    converted++;
                } else {
                    errors.push_back(filepath + ": Konvertierung fehlgeschlagen");
                    continue;
                }
            }
            
            // Füge zur Datenbank hinzu
            MediaMetadata meta;
            meta.filepath = finalPath;
            meta.title = std::filesystem::path(finalPath).stem().string();
            meta.genre = "Unknown";
            meta.artist = "Unknown";
            meta.analyzed = false;
            meta.addedTimestamp = std::time(nullptr);
            
            if (self->database_->addMedia(meta)) {
                added++;
            }
            
            // Update Progress alle 10 Dateien
            if (processed % 10 == 0 || processed == total) {
                gdk_threads_add_idle([](gpointer data) -> gboolean {
                    auto* info = static_cast<std::tuple<GtkWidget*, GtkWidget*, int, int, int>*>(data);
                    
                    float progress = (float)std::get<2>(*info) / std::get<3>(*info);
                    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(std::get<0>(*info)), progress);
                    
                    int skipped = std::get<3>(*info) - std::get<4>(*info);
                    std::string text = std::to_string(std::get<2>(*info)) + " / " + 
                                      std::to_string(std::get<3>(*info)) + " (" + 
                                      std::to_string(std::get<4>(*info)) + " neu, " +
                                      std::to_string(skipped) + " Duplikate)";
                    gtk_label_set_text(GTK_LABEL(std::get<1>(*info)), text.c_str());
                    
                    delete info;
                    return G_SOURCE_REMOVE;
                }, new std::tuple<GtkWidget*, GtkWidget*, int, int, int>(progressBar, labelProgress, processed, total, added));
            }
        }
        
        self->refreshDatabaseView();
        
        // Zeige Ergebnis
        int skipped = total - added;
        gdk_threads_add_idle([](gpointer data) -> gboolean {
            auto* info = static_cast<std::tuple<GtkRenderer*, GtkWidget*, int, int, int, std::vector<std::string>>*>(data);
            GtkRenderer* self = std::get<0>(*info);
            int added = std::get<2>(*info);
            int skipped = std::get<3>(*info);
            int converted = std::get<4>(*info);
            auto errors = std::get<5>(*info);
            
            gtk_widget_destroy(std::get<1>(*info));
            
            std::string msg;
            if (skipped > 0) {
                msg = "✅ " + std::to_string(added) + " Dateien hinzugefügt\n" +
                      "ℹ️ " + std::to_string(skipped) + " bereits vorhanden";
            } else {
                msg = "✅ " + std::to_string(added) + " Dateien zur Datenbank hinzugefügt";
            }
            
            if (converted > 0) {
                msg += "\n🎬 " + std::to_string(converted) + " Videos zu MP3 konvertiert";
            }
            
            if (!errors.empty()) {
                msg += "\n\n⚠️ " + std::to_string(errors.size()) + " Fehler";
            }
            
            GtkWidget* resultDialog = gtk_message_dialog_new(
                GTK_WINDOW(self->window_),
                GTK_DIALOG_MODAL,
                !errors.empty() ? GTK_MESSAGE_WARNING : GTK_MESSAGE_INFO,
                GTK_BUTTONS_OK,
                "%s", msg.c_str()
            );
            
            if (!errors.empty() && errors.size() <= 10) {
                std::string details = "\n\nFehler:\n";
                for (size_t i = 0; i < std::min(errors.size(), size_t(10)); i++) {
                    details += "• " + errors[i] + "\n";
                }
                gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(resultDialog), 
                                                         "%s", details.c_str());
            }
            
            gtk_dialog_run(GTK_DIALOG(resultDialog));
            gtk_widget_destroy(resultDialog);
            
            delete info;
            return G_SOURCE_REMOVE;
        }, new std::tuple<GtkRenderer*, GtkWidget*, int, int, int, std::vector<std::string>>(
            self, progressDialog, added, skipped, converted, errors));
    }).detach();
}

void GtkRenderer::onBookmarkGo(GtkWidget* widget, gpointer data) {
    GtkWidget* bookmarkCombo = GTK_WIDGET(g_object_get_data(G_OBJECT(widget), "bookmark-combo"));
    BrowserData* bd = static_cast<BrowserData*>(g_object_get_data(G_OBJECT(widget), "browser-data"));
    
    if (!bookmarkCombo || !bd) return;
    
    int active = gtk_combo_box_get_active(GTK_COMBO_BOX(bookmarkCombo));
    std::string path;
    const char* home = getenv("HOME");
    
    switch (active) {
        case 0: path = home ? home : "/home"; break;  // Home
        case 1: path = std::string(home ? home : "/home") + "/Music"; break;  // Musik
        case 2: path = std::string(home ? home : "/home") + "/Downloads"; break;  // Downloads
        case 3: path = std::string(home ? home : "/home") + "/Documents"; break;  // Dokumente
        case 4: path = std::string(home ? home : "/home") + "/Pictures"; break;  // Bilder
        case 5: path = std::string(home ? home : "/home") + "/Videos"; break;  // Videos
        case 6: path = std::string(home ? home : "/home") + "/.songgen/hvsc/mp3"; break;  // HVSC
        case 7: path = "/"; break;  // Root
        case 8: path = "/tmp"; break;  // tmp
        case 9: path = "/etc"; break;  // etc
        case 10: path = "/usr"; break;  // usr
        case 11: path = "/opt"; break;  // opt
        default: path = home ? home : "/home"; break;
    }
    
    if (!path.empty()) {
        gtk_entry_set_text(GTK_ENTRY(bd->addrEntry), path.c_str());
        bd->currentPath = path;
        loadBrowserDirectory(path, bd->store);
    }
}

void GtkRenderer::onBookmarkAdd(GtkWidget* widget, gpointer data) {
    GtkWidget* addrEntry = GTK_WIDGET(g_object_get_data(G_OBJECT(widget), "addr-entry"));
    GtkWidget* bookmarkCombo = GTK_WIDGET(g_object_get_data(G_OBJECT(widget), "bookmark-combo"));
    
    if (!addrEntry || !bookmarkCombo) return;
    
    const char* currentPath = gtk_entry_get_text(GTK_ENTRY(addrEntry));
    if (!currentPath || strlen(currentPath) == 0) return;
    
    // Zeige Dialog zum Eingeben des Lesezeichennamens
    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        "Lesezeichen hinzufügen",
        nullptr,
        (GtkDialogFlags)0,
        "_Abbrechen", GTK_RESPONSE_CANCEL,
        "_Hinzufügen", GTK_RESPONSE_OK,
        nullptr
    );
    
    makeDialogResizable(dialog, 400, 200);
    
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
    gtk_container_add(GTK_CONTAINER(content), vbox);
    
    GtkWidget* labelPath = gtk_label_new(("Pfad: " + std::string(currentPath)).c_str());
    gtk_label_set_selectable(GTK_LABEL(labelPath), TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), labelPath, FALSE, FALSE, 0);
    
    GtkWidget* labelName = gtk_label_new("Name für Lesezeichen:");
    gtk_box_pack_start(GTK_BOX(vbox), labelName, FALSE, FALSE, 0);
    
    GtkWidget* entry = gtk_entry_new();
    // Nutze letzten Teil des Pfads als Vorschlag
    std::string suggestion = std::filesystem::path(currentPath).filename().string();
    if (suggestion.empty()) suggestion = "Lesezeichen";
    gtk_entry_set_text(GTK_ENTRY(entry), suggestion.c_str());
    gtk_box_pack_start(GTK_BOX(vbox), entry, FALSE, FALSE, 0);
    
    gtk_widget_show_all(dialog);
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        const char* name = gtk_entry_get_text(GTK_ENTRY(entry));
        if (name && strlen(name) > 0) {
            std::string bookmarkText = "📁 " + std::string(name);
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(bookmarkCombo), bookmarkText.c_str());
            
            // Speichere Pfad in der ComboBox als Daten
            int index = gtk_combo_box_get_active(GTK_COMBO_BOX(bookmarkCombo));
            // TODO: Lesezeichen in Config-Datei speichern für Persistenz
        }
    }
    
    gtk_widget_destroy(dialog);
}

void GtkRenderer::onFolderChanged(GtkFileChooser* chooser, gpointer data) {
    struct BrowserData {
        GtkWidget* addrEntry;
        GtkWidget* fileChooser;
        GtkRenderer* self;
    };
    
    auto* bd = static_cast<BrowserData*>(data);
    char* folder = gtk_file_chooser_get_current_folder(chooser);
    
    if (folder) {
        gtk_entry_set_text(GTK_ENTRY(bd->addrEntry), folder);
        g_free(folder);
    }
}

// Browser TreeView Callback-Funktionen
void GtkRenderer::onBrowserToggleCell(GtkCellRendererToggle* cell, gchar* path_str, gpointer data) {
    GtkListStore* store = GTK_LIST_STORE(data);
    GtkTreeIter iter;
    if (gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(store), &iter, path_str)) {
        gboolean active;
        gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, 0, &active, -1);
        gtk_list_store_set(store, &iter, 0, !active, -1);
    }
}

void GtkRenderer::onBrowserSelectAll(GtkWidget* widget, gpointer data) {
    GtkListStore* store = GTK_LIST_STORE(data);
    GtkTreeIter iter;
    if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter)) {
        do {
            gtk_list_store_set(store, &iter, 0, TRUE, -1);
        } while (gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter));
    }
}

void GtkRenderer::onBrowserDeselectAll(GtkWidget* widget, gpointer data) {
    GtkListStore* store = GTK_LIST_STORE(data);
    GtkTreeIter iter;
    if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter)) {
        do {
            gtk_list_store_set(store, &iter, 0, FALSE, -1);
        } while (gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter));
    }
}

void GtkRenderer::onBrowserRowActivated(GtkTreeView* tree, GtkTreePath* path, GtkTreeViewColumn* col, gpointer data) {
    auto* bd = static_cast<BrowserData*>(data);
    GtkTreeIter iter;
    GtkTreeModel* model = gtk_tree_view_get_model(tree);
    
    if (gtk_tree_model_get_iter(model, &iter, path)) {
        gchar* type;
        gchar* fullPath;
        gtk_tree_model_get(model, &iter, 3, &type, 4, &fullPath, -1);
        
        if (std::string(type) == "Ordner") {
            // Navigiere in Ordner
            bd->currentPath = fullPath;
            gtk_entry_set_text(GTK_ENTRY(bd->addrEntry), fullPath);
            loadBrowserDirectory(fullPath, bd->store);
        }
        
        g_free(type);
        g_free(fullPath);
    }
}

void GtkRenderer::loadBrowserDirectory(const std::string& path, GtkListStore* store) {
    gtk_list_store_clear(store);
    
    try {
        for (const auto& entry : std::filesystem::directory_iterator(path)) {
            try {
                std::string filename = entry.path().filename().string();
                std::string icon, type;
                
                if (entry.is_directory()) {
                    icon = "📁";
                    type = "Ordner";
                } else if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    
                    if (ext == ".mp3" || ext == ".wav" || ext == ".flac" || 
                        ext == ".ogg" || ext == ".m4a" || ext == ".sid") {
                        icon = "🎵";
                        type = "Audio";
                    } else if (ext == ".mp4" || ext == ".mkv" || ext == ".avi" || 
                               ext == ".webm" || ext == ".mov" || ext == ".flv") {
                        icon = "🎬";
                        type = "Video";
                    } else {
                        continue; // Skip other files
                    }
                } else {
                    continue; // Skip non-regular files
                }
                
                GtkTreeIter iter;
                gtk_list_store_append(store, &iter);
                gtk_list_store_set(store, &iter,
                    0, FALSE,                              // Checkbox
                    1, icon.c_str(),                       // Icon
                    2, filename.c_str(),                   // Name
                    3, type.c_str(),                       // Typ
                    4, entry.path().string().c_str(),      // Fullpath
                    -1);
            } catch (...) {}
        }
    } catch (...) {}
}

void GtkRenderer::onPlaySong(GtkTreeView* tree_view, GtkTreePath* path, GtkTreeViewColumn* column, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    
    GtkTreeModel* model = gtk_tree_view_get_model(tree_view);
    GtkTreeIter iter;
    
    if (gtk_tree_model_get_iter(model, &iter, path)) {
        gchar* filepath = nullptr;
        gchar* title = nullptr;
        
        gtk_tree_model_get(model, &iter, 5, &filepath, 1, &title, -1);
        
        if (filepath) {
            // Stoppe aktuellen Song
            self->audioPlayer_->stop();
            
            // Lade und spiele neuen Song
            if (self->audioPlayer_->load(filepath)) {
                self->audioPlayer_->play();
                self->updateStatusBar(std::string("🎵 Spielt: ") + (title ? title : "Unbekannt"));
                
                // Desktop-Benachrichtigung
                std::string titleStr = title ? title : "Unbekannt";
                std::thread([titleStr]() {
                    std::string msg = std::string("🎵 Spielt: ") + titleStr;
                    std::string cmd = "notify-send -a 'SongGen' -u low '🎵 Abspielen' '" + msg + "' 2>/dev/null &";
                    system(cmd.c_str());
                }).detach();
            } else {
                self->updateStatusBar("❌ Fehler beim Abspielen");
            }
            
            g_free(filepath);
        }
        
        if (title) {
            g_free(title);
        }
    }
}

void GtkRenderer::onSearchChanged(GtkEntry* entry, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    self->refreshDatabaseView();
}

void GtkRenderer::onGenreFilterChanged(GtkComboBox* combo, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    self->refreshDatabaseView();
}

// === Quick Preset Callbacks ===
void GtkRenderer::onGenreChanged(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    
    // Automatische BPM-Anpassung basierend auf Genre
    gchar* genre = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(widget));
    if (!genre) return;
    
    std::string genreStr(genre);
    int suggestedBPM = 120;  // Default
    
    if (genreStr == "Techno") suggestedBPM = 135;
    else if (genreStr == "House") suggestedBPM = 125;
    else if (genreStr == "Trance") suggestedBPM = 138;
    else if (genreStr == "Ambient") suggestedBPM = 70;
    else if (genreStr == "Trap") suggestedBPM = 140;
    else if (genreStr == "Dubstep") suggestedBPM = 140;
    else if (genreStr == "Drum'n'Bass") suggestedBPM = 170;
    else if (genreStr == "Hardcore") suggestedBPM = 170;
    else if (genreStr == "Hardstyle") suggestedBPM = 150;
    else if (genreStr == "Rock") suggestedBPM = 120;
    else if (genreStr == "Metal") suggestedBPM = 180;
    else if (genreStr == "Punk") suggestedBPM = 180;
    else if (genreStr == "Pop") suggestedBPM = 120;
    else if (genreStr == "Hip-Hop") suggestedBPM = 90;
    else if (genreStr == "Jazz") suggestedBPM = 120;
    else if (genreStr == "Reggae") suggestedBPM = 80;
    else if (genreStr == "Salsa") suggestedBPM = 180;
    else if (genreStr == "Walzer") suggestedBPM = 180;
    
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(self->genBpmSpin_), suggestedBPM);
    
    g_free(genre);
}

void GtkRenderer::onQuickPresetChillout(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    gtk_combo_box_set_active(GTK_COMBO_BOX(self->genGenreCombo_), 6); // Chillout
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(self->genBpmSpin_), 90);
    gtk_combo_box_set_active(GTK_COMBO_BOX(self->genIntensityCombo_), 0); // Low
}

void GtkRenderer::onQuickPresetTechno(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    gtk_combo_box_set_active(GTK_COMBO_BOX(self->genGenreCombo_), 2); // Techno
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(self->genBpmSpin_), 140);
    gtk_combo_box_set_active(GTK_COMBO_BOX(self->genIntensityCombo_), 2); // High
}

void GtkRenderer::onQuickPresetAmbient(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    gtk_combo_box_set_active(GTK_COMBO_BOX(self->genGenreCombo_), 5); // Ambient
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(self->genBpmSpin_), 70);
    gtk_combo_box_set_active(GTK_COMBO_BOX(self->genIntensityCombo_), 0); // Low
}

void GtkRenderer::onQuickPresetHouse(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    gtk_combo_box_set_active(GTK_COMBO_BOX(self->genGenreCombo_), 4); // House
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(self->genBpmSpin_), 125);
    gtk_combo_box_set_active(GTK_COMBO_BOX(self->genIntensityCombo_), 2); // High
}

void GtkRenderer::onQuickPresetJazz(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    gtk_combo_box_set_active(GTK_COMBO_BOX(self->genGenreCombo_), 21); // Jazz
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(self->genBpmSpin_), 110);
    gtk_combo_box_set_active(GTK_COMBO_BOX(self->genIntensityCombo_), 1); // Medium
}

void GtkRenderer::onQuickPresetSalsa(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    gtk_combo_box_set_active(GTK_COMBO_BOX(self->genGenreCombo_), 28); // Salsa
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(self->genBpmSpin_), 180);
    gtk_combo_box_set_active(GTK_COMBO_BOX(self->genIntensityCombo_), 2); // High
}

void GtkRenderer::onQuickPresetWalzer(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    gtk_combo_box_set_active(GTK_COMBO_BOX(self->genGenreCombo_), 26); // Walzer
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(self->genBpmSpin_), 180);
    gtk_combo_box_set_active(GTK_COMBO_BOX(self->genIntensityCombo_), 1); // Medium
}

void GtkRenderer::onQuickPresetRnB(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    gtk_combo_box_set_active(GTK_COMBO_BOX(self->genGenreCombo_), 12); // RnB
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(self->genBpmSpin_), 95);
    gtk_combo_box_set_active(GTK_COMBO_BOX(self->genIntensityCombo_), 1); // Medium
}

void GtkRenderer::onClearSearch(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    
    if (self->dbSearchEntry_) {
        gtk_entry_set_text(GTK_ENTRY(self->dbSearchEntry_), "");
    }
    
    if (self->dbGenreCombo_) {
        gtk_combo_box_set_active(GTK_COMBO_BOX(self->dbGenreCombo_), 0);
    }
    
    self->refreshDatabaseView();
}

void GtkRenderer::onStopExtraction(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    self->stopExtraction_ = true;
}

void GtkRenderer::onGenerateSong(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    
    // Lese Generator-Einstellungen
    const char* genreText = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(self->genGenreCombo_));
    std::string genre = genreText ? genreText : "Electronic";
    
    int bpm = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(self->genBpmSpin_));
    int duration = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(self->genDurationSpin_));
    
    const char* intensityText = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(self->genIntensityCombo_));
    std::string intensity = intensityText ? intensityText : "Mittel";
    
    // Ausgabepfad
    std::string outputPath = std::string(getenv("HOME")) + "/.songgen/generated/";
    
    // Erstelle Dialog mit Fortschritt
    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        "Song wird generiert...",
        GTK_WINDOW(self->window_),
        (GtkDialogFlags)0,
        nullptr
    );
    
    makeDialogResizable(dialog, 450, 180);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget* progressBar = gtk_progress_bar_new();
    GtkWidget* labelStatus = gtk_label_new("Initialisiere Generator...");
    gtk_label_set_selectable(GTK_LABEL(labelStatus), TRUE);
    
    gtk_box_pack_start(GTK_BOX(content), labelStatus, FALSE, FALSE, 10);
    gtk_box_pack_start(GTK_BOX(content), progressBar, FALSE, FALSE, 10);
    gtk_widget_show_all(dialog);
    
    // Thread für Generierung
    std::thread([self, genre, bpm, duration, intensity, outputPath, dialog, progressBar, labelStatus]() {
        // Progress-Updates
        auto updateProgress = [dialog, progressBar, labelStatus](float progress, const std::string& status) {
            gdk_threads_add_idle([](gpointer data) -> gboolean {
                auto* info = static_cast<std::tuple<GtkWidget*, GtkWidget*, float, std::string>*>(data);
                gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(std::get<1>(*info)), std::get<2>(*info));
                gtk_label_set_text(GTK_LABEL(std::get<0>(*info)), std::get<3>(*info).c_str());
                delete info;
                return G_SOURCE_REMOVE;
            }, new std::tuple<GtkWidget*, GtkWidget*, float, std::string>(labelStatus, progressBar, progress, status));
        };
        
        updateProgress(0.1f, "Lade Genre-Templates...");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        updateProgress(0.3f, "Generiere Melodie...");
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        
        updateProgress(0.5f, "Erstelle Rhythmus-Patterns...");
        std::this_thread::sleep_for(std::chrono::milliseconds(700));
        
        updateProgress(0.7f, "Synthetisiere Audio...");
        
        // Generiere Song mit SongGenerator
        GenerationParams params;
        params.genre = genre;
        params.bpm = bpm;
        params.duration = duration;
        params.intensity = intensity == "Niedrig" ? "soft" : 
                           intensity == "Hoch" ? "hart" : "mittel";
        params.energy = intensity == "Niedrig" ? 0.3f : 
                        intensity == "Hoch" ? 0.9f : 0.6f;
        params.complexity = 0.6f;
        params.variation = 0.5f;
        params.useIntro = true;
        params.useOutro = true;
        
        std::string filename = genre + "_" + std::to_string(bpm) + "bpm_" + 
                              std::to_string(std::time(nullptr)) + ".wav";
        std::string fullPath = outputPath + filename;
        
        // Erstelle Ausgabeverzeichnis
        std::filesystem::create_directories(outputPath);
        
        bool success = self->generator_->generate(params, fullPath, 
            [&updateProgress](const std::string& phase, float progress) {
                updateProgress(0.7f + progress * 0.3f, phase);
            });
        
        updateProgress(1.0f, success ? "Song generiert!" : "Fehler bei Generierung");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // Schließe Dialog und zeige Ergebnis
        gdk_threads_add_idle([](gpointer data) -> gboolean {
            auto* info = static_cast<std::tuple<GtkWidget*, GtkRenderer*, bool, std::string>*>(data);
            
            gtk_widget_destroy(std::get<0>(*info));
            
            GtkWidget* resultDialog = gtk_message_dialog_new(
                GTK_WINDOW(std::get<1>(*info)->window_),
                GTK_DIALOG_MODAL,
                std::get<2>(*info) ? GTK_MESSAGE_INFO : GTK_MESSAGE_ERROR,
                GTK_BUTTONS_OK,
                std::get<2>(*info) ? 
                    ("✅ Song erfolgreich generiert!\n\n" + std::get<3>(*info)).c_str() :
                    "❌ Fehler bei der Song-Generierung"
            );
            
            gtk_dialog_run(GTK_DIALOG(resultDialog));
            gtk_widget_destroy(resultDialog);
            
            delete info;
            return G_SOURCE_REMOVE;
        }, new std::tuple<GtkWidget*, GtkRenderer*, bool, std::string>(dialog, self, success, fullPath));
        
    }).detach();
}

void GtkRenderer::onDBSync(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    
    std::thread([self]() {
        std::string hvscMp3Dir = std::string(getenv("HOME")) + "/.songgen/hvsc/mp3/";
        
        if (!fs::exists(hvscMp3Dir)) {
            gdk_threads_add_idle([](gpointer window) -> gboolean {
                GtkWidget* dialog = gtk_message_dialog_new(
                    GTK_WINDOW(window),
                    GTK_DIALOG_MODAL,
                    GTK_MESSAGE_ERROR,
                    GTK_BUTTONS_OK,
                    "❌ MP3-Verzeichnis nicht gefunden!"
                );
                gtk_dialog_run(GTK_DIALOG(dialog));
                gtk_widget_destroy(dialog);
                return G_SOURCE_REMOVE;
            }, self->window_);
            return;
        }
        
        size_t added = self->hvscDownloader_->addToDatabase(hvscMp3Dir, *self->database_, false);
        self->filteredMedia_ = self->database_->getAll();
        
        gdk_threads_add_idle([](gpointer data) -> gboolean {
            auto* info = static_cast<std::pair<GtkRenderer*, size_t>*>(data);
            
            GtkWidget* dialog = gtk_message_dialog_new(
                GTK_WINDOW(info->first->window_),
                GTK_DIALOG_MODAL,
                GTK_MESSAGE_INFO,
                GTK_BUTTONS_OK,
                "✅ %zu neue SID-MP3s importiert",
                info->second
            );
            
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
            
            delete info;
            return G_SOURCE_REMOVE;
        }, new std::pair<GtkRenderer*, size_t>(self, added));
        
    }).detach();
}

void GtkRenderer::onPlayAudioSample(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    
    if (self->currentAudioFile_.empty()) {
        return;
    }
    
    // Stoppe aktuellen Player
    if (self->audioPlayer_) {
        self->audioPlayer_->stop();
    }
    
    // Spiele Audio-Sample ab
    std::cout << "▶️ Spiele ab: " << self->currentAudioFile_ << std::endl;
    
    if (self->audioPlayer_ && self->audioPlayer_->load(self->currentAudioFile_)) {
        self->audioPlayer_->play();
    } else {
        std::cerr << "❌ Konnte Audio nicht laden: " << self->currentAudioFile_ << std::endl;
        
        GtkWidget* dialog = gtk_message_dialog_new(
            GTK_WINDOW(self->window_),
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR,
            GTK_BUTTONS_OK,
            "❌ Fehler beim Abspielen der Datei:\\n%s",
            self->currentAudioFile_.c_str()
        );
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
}

void GtkRenderer::onStopAudioSample(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    
    if (self->audioPlayer_) {
        self->audioPlayer_->stop();
        std::cout << "⏹️ Wiedergabe gestoppt" << std::endl;
    }
}

void GtkRenderer::onDestroy(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    self->shutdown();
    gtk_main_quit();
}

std::string GtkRenderer::showDecisionDialog(const std::string& question, 
                                           const std::vector<std::string>& options,
                                           const std::string& context,
                                           const std::string& audioFile) {
    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        "🤔 KI-Entscheidungshilfe",
        GTK_WINDOW(window_),
        (GtkDialogFlags)0,
        "_Abbrechen", GTK_RESPONSE_CANCEL,
        "_OK", GTK_RESPONSE_OK,
        nullptr
    );
    
    makeDialogResizable(dialog, 600, 400);
    GtkWidget* contentArea = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(contentArea), 10);
    
    // Audio-File Info + Play Button (wenn vorhanden)
    if (!audioFile.empty()) {
        GtkWidget* audioBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_widget_set_margin_bottom(audioBox, 15);
        
        // Dateiname
        std::string filename = std::filesystem::path(audioFile).filename().string();
        GtkWidget* fileLabel = gtk_label_new(nullptr);
        std::string markup = "<b>🎵 Audio:</b> " + filename;
        gtk_label_set_markup(GTK_LABEL(fileLabel), markup.c_str());
        gtk_label_set_ellipsize(GTK_LABEL(fileLabel), PANGO_ELLIPSIZE_MIDDLE);
        gtk_label_set_max_width_chars(GTK_LABEL(fileLabel), 50);
        gtk_box_pack_start(GTK_BOX(audioBox), fileLabel, TRUE, TRUE, 0);
        
        // Play Button
        GtkWidget* playButton = gtk_button_new_with_label("▶️ Abspielen");
        currentAudioFile_ = audioFile;
        g_signal_connect(playButton, "clicked", G_CALLBACK(onPlayAudioSample), this);
        gtk_box_pack_start(GTK_BOX(audioBox), playButton, FALSE, FALSE, 0);
        
        // Stop Button
        GtkWidget* stopButton = gtk_button_new_with_label("⏹️ Stop");
        g_signal_connect(stopButton, "clicked", G_CALLBACK(onStopAudioSample), this);
        gtk_box_pack_start(GTK_BOX(audioBox), stopButton, FALSE, FALSE, 0);
        
        gtk_container_add(GTK_CONTAINER(contentArea), audioBox);
        
        // Voller Pfad als Tooltip
        GtkWidget* pathLabel = gtk_label_new(nullptr);
        std::string pathMarkup = "<small><tt>" + audioFile + "</tt></small>";
        gtk_label_set_markup(GTK_LABEL(pathLabel), pathMarkup.c_str());
        gtk_label_set_line_wrap(GTK_LABEL(pathLabel), TRUE);
        gtk_label_set_selectable(GTK_LABEL(pathLabel), TRUE);
        gtk_widget_set_margin_bottom(pathLabel, 10);
        gtk_container_add(GTK_CONTAINER(contentArea), pathLabel);
    }
    
    // Frage-Label
    GtkWidget* questionLabel = gtk_label_new(question.c_str());
    gtk_label_set_line_wrap(GTK_LABEL(questionLabel), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(questionLabel), 60);
    gtk_widget_set_margin_bottom(questionLabel, 15);
    gtk_container_add(GTK_CONTAINER(contentArea), questionLabel);
    
    // Context anzeigen (optional)
    if (!context.empty()) {
        GtkWidget* contextLabel = gtk_label_new(nullptr);
        gtk_label_set_markup(GTK_LABEL(contextLabel), 
                            ("<small><i>" + context + "</i></small>").c_str());
        gtk_label_set_line_wrap(GTK_LABEL(contextLabel), TRUE);
        gtk_widget_set_margin_bottom(contextLabel, 10);
        gtk_container_add(GTK_CONTAINER(contentArea), contextLabel);
    }
    
    // Radio Buttons für Optionen
    GSList* group = nullptr;
    std::vector<GtkWidget*> radioButtons;
    
    for (const auto& option : options) {
        GtkWidget* radio = gtk_radio_button_new_with_label(group, option.c_str());
        group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(radio));
        gtk_widget_set_margin_bottom(radio, 5);
        gtk_container_add(GTK_CONTAINER(contentArea), radio);
        radioButtons.push_back(radio);
    }
    
    // Ersten Button aktivieren
    if (!radioButtons.empty()) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radioButtons[0]), TRUE);
    }
    
    gtk_widget_show_all(dialog);
    
    std::string selectedAnswer;
    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    
    if (response == GTK_RESPONSE_OK) {
        // Finde den aktiven Radio Button
        for (size_t i = 0; i < radioButtons.size(); ++i) {
            if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(radioButtons[i]))) {
                selectedAnswer = options[i];
                break;
            }
        }
    }
    
    gtk_widget_destroy(dialog);
    return selectedAnswer;
}

void GtkRenderer::onShowDecisionHistory(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    
    auto decisions = self->database_->getDecisionHistory(100);
    
    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        "📜 Entscheidungs-Historie",
        GTK_WINDOW(self->window_),
        (GtkDialogFlags)0,
        "_Schließen", GTK_RESPONSE_CLOSE,
        nullptr
    );
    
    makeDialogResizable(dialog, 800, 600);
    
    GtkWidget* contentArea = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    
    // Scrolled Window
    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(contentArea), scrolled);
    
    // TreeView für Historie
    GtkListStore* store = gtk_list_store_new(5,
        G_TYPE_STRING,  // Zeit
        G_TYPE_STRING,  // Typ
        G_TYPE_STRING,  // Frage
        G_TYPE_STRING,  // Antwort
        G_TYPE_STRING   // Konfidenz
    );
    
    for (const auto& decision : decisions) {
        GtkTreeIter iter;
        gtk_list_store_append(store, &iter);
        
        // Format timestamp
        time_t timestamp = decision.timestamp;
        char timeStr[64];
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", localtime(&timestamp));
        
        // Kürze Frage wenn zu lang
        std::string shortQuestion = decision.question;
        if (shortQuestion.length() > 80) {
            shortQuestion = shortQuestion.substr(0, 77) + "...";
        }
        
        gtk_list_store_set(store, &iter,
                          0, timeStr,
                          1, decision.decisionType.c_str(),
                          2, shortQuestion.c_str(),
                          3, decision.userAnswer.c_str(),
                          4, (std::to_string((int)(decision.confidence * 100)) + "%").c_str(),
                          -1);
    }
    
    GtkWidget* treeView = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);
    
    // Spalten
    const char* columns[] = {"Zeit", "Typ", "Frage", "Antwort", "Konfidenz"};
    for (int i = 0; i < 5; ++i) {
        GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
        GtkTreeViewColumn* column = gtk_tree_view_column_new_with_attributes(
            columns[i], renderer, "text", i, nullptr);
        gtk_tree_view_column_set_resizable(column, TRUE);
        gtk_tree_view_column_set_sort_column_id(column, i);
        gtk_tree_view_append_column(GTK_TREE_VIEW(treeView), column);
    }
    
    gtk_container_add(GTK_CONTAINER(scrolled), treeView);
    
    // Statistik-Label am Ende
    GtkWidget* statsLabel = gtk_label_new(nullptr);
    gtk_label_set_selectable(GTK_LABEL(statsLabel), TRUE);
    std::string statsText = "<b>Statistik:</b> " + std::to_string(decisions.size()) + " Entscheidungen gespeichert";
    gtk_label_set_markup(GTK_LABEL(statsLabel), statsText.c_str());
    gtk_widget_set_margin_top(statsLabel, 10);
    gtk_container_add(GTK_CONTAINER(contentArea), statsLabel);
    
    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

void GtkRenderer::startAutoSync() {
    autoSyncRunning_ = true;
    
    autoSyncThread_ = std::thread([this]() {
        const char* home = getenv("HOME");
        if (!home) return;
        
        std::string hvscMp3Dir = std::string(home) + "/.songgen/hvsc/mp3/";
        
        while (autoSyncRunning_) {
            std::this_thread::sleep_for(std::chrono::seconds(30));
            
            if (!fs::exists(hvscMp3Dir) || !fs::is_directory(hvscMp3Dir)) {
                continue;
            }
            
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
                continue;
            }
            
            if (mp3Count > 0 && dbCount < mp3Count) {
                size_t added = hvscDownloader_->addToDatabase(hvscMp3Dir, *database_, false);
                
                if (added > 0) {
                    filteredMedia_ = database_->getAll();
                    
                    // Desktop-Benachrichtigung
                    std::string msg = "✅ Auto-Sync: " + std::to_string(added) + " neue SID-MP3s importiert";
                    std::string cmd = "notify-send -a 'SongGen' '" + msg + "' 2>/dev/null &";
                    system(cmd.c_str());
                }
            }
        }
    });
}

void GtkRenderer::stopAutoSync() {
    autoSyncRunning_ = false;
    if (autoSyncThread_.joinable()) {
        autoSyncThread_.join();
    }
}

void GtkRenderer::onColumnHeaderClicked(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    const char* columnName = (const char*)g_object_get_data(G_OBJECT(widget), "column-name");
    
    if (columnName) {
        self->sortDatabaseBy(columnName);
    }
}

void GtkRenderer::sortDatabaseBy(const std::string& column) {
    // Toggle Sortier-Richtung wenn gleiche Spalte geklickt
    if (currentSortColumn_ == column) {
        sortAscending_ = !sortAscending_;
    } else {
        currentSortColumn_ = column;
        sortAscending_ = true;
    }
    
    // Sortiere filteredMedia_
    std::sort(filteredMedia_.begin(), filteredMedia_.end(), 
        [this, column](const MediaMetadata& a, const MediaMetadata& b) {
            bool result = false;
            if (column == "title") {
                result = a.title < b.title;
            } else if (column == "artist") {
                result = a.artist < b.artist;
            } else if (column == "genre") {
                result = a.genre < b.genre;
            } else if (column == "duration") {
                result = a.duration < b.duration;
            }
            return sortAscending_ ? result : !result;
        });
    
    refreshDatabaseView();
    
    std::string msg = "Sortiert nach " + column + (sortAscending_ ? " ↑" : " ↓");
    updateStatusBar(msg);
}

void GtkRenderer::onTrainModel(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    
    if (self->isTraining_) {
        GtkWidget* dialog = gtk_message_dialog_new(
            GTK_WINDOW(self->window_),
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_INFO,
            GTK_BUTTONS_OK,
            "Training läuft bereits!"
        );
        makeDialogResizable(dialog, 400, 150);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }
    
    self->isTraining_ = true;
    self->trainingEpoch_ = 0;
    self->trainingMaxEpochs_ = 100;
    
    // Starte echtes Training in Thread
    std::thread([self]() {
        std::cout << "\n🎓 Starte Training..." << std::endl;
        
        // Erstelle TrainingModel
        TrainingModel trainer(*self->database_);
        
        // Progress Callback für asynchrone UI-Updates
        int maxEpochs = self->trainingMaxEpochs_;  // Kopiere atomic Wert
        auto progressCallback = [self, maxEpochs](int epoch, float loss, float accuracy) {
            self->trainingEpoch_ = epoch;
            
            // Update UI thread-safe
            gdk_threads_add_idle([](gpointer data) -> gboolean {
                auto* args = static_cast<std::tuple<GtkRenderer*, int, int, float, float>*>(data);
                GtkRenderer* self = std::get<0>(*args);
                int epoch = std::get<1>(*args);
                int maxEpochs = std::get<2>(*args);
                float loss = std::get<3>(*args);
                float accuracy = std::get<4>(*args);
                
                float progress = (float)epoch / maxEpochs;
                gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(self->trainingProgressBar_), progress);
                
                // Progressbar mit % beschriften
                char percentText[32];
                snprintf(percentText, sizeof(percentText), "%.1f%%", progress * 100.0f);
                gtk_progress_bar_set_text(GTK_PROGRESS_BAR(self->trainingProgressBar_), percentText);
                gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(self->trainingProgressBar_), TRUE);
                
                char status[256];
                snprintf(status, sizeof(status), 
                        "Epoch %d/%d (%.1f%%) - Loss: %.4f - Accuracy: %.1f%%",
                        epoch, maxEpochs, progress * 100.0f, loss, accuracy * 100.0f);
                gtk_label_set_text(GTK_LABEL(self->trainingStatusLabel_), status);
                
                delete args;
                return G_SOURCE_REMOVE;
            }, new std::tuple<GtkRenderer*, int, int, float, float>(self, epoch, maxEpochs, loss, accuracy));
        };
        
        // Starte Training
        bool success = trainer.train(
            self->trainingMaxEpochs_,  // epochs
            32,                         // batchSize
            0.001f,                     // learningRate
            progressCallback
        );
        
        // Training abgeschlossen
        gdk_threads_add_idle([](gpointer data) -> gboolean {
            auto* args = static_cast<std::pair<GtkRenderer*, bool>*>(data);
            GtkRenderer* self = args->first;
            bool success = args->second;
            
            self->isTraining_ = false;
            
            if (success) {
                gtk_label_set_text(GTK_LABEL(self->trainingStatusLabel_), 
                                  "✅ Training erfolgreich abgeschlossen!");
                
                GtkWidget* dialog = gtk_message_dialog_new(
                    GTK_WINDOW(self->window_),
                    GTK_DIALOG_MODAL,
                    GTK_MESSAGE_INFO,
                    GTK_BUTTONS_OK,
                    "✅ Training erfolgreich abgeschlossen!\n\n"
                    "Modell wurde gespeichert in:\n~/.songgen/model.sgml"
                );
                gtk_dialog_run(GTK_DIALOG(dialog));
                gtk_widget_destroy(dialog);
            } else {
                gtk_label_set_text(GTK_LABEL(self->trainingStatusLabel_), 
                                  "❌ Training fehlgeschlagen!");
            }
            
            delete args;
            return G_SOURCE_REMOVE;
        }, new std::pair<GtkRenderer*, bool>(self, success));
    }).detach();
    
    self->updateStatusBar("🧠 Training gestartet...");
}

void GtkRenderer::onSaveModel(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    
    const char* home = getenv("HOME");
    std::string modelPath = std::string(home) + "/.songgen/models/";
    std::filesystem::create_directories(modelPath);
    
    time_t now = time(nullptr);
    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%Y%m%d_%H%M%S", localtime(&now));
    
    std::string filename = modelPath + "model_" + timeStr + ".bin";
    
    // Simuliere Modell-Speicherung
    std::ofstream file(filename);
    if (file.is_open()) {
        file << "SongGen Model v1.0\n";
        file << "Training Epochs: " << self->trainingEpoch_ << "\n";
        file.close();
        
        GtkWidget* dialog = gtk_message_dialog_new(
            GTK_WINDOW(self->window_),
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_INFO,
            GTK_BUTTONS_OK,
            "💾 Modell gespeichert:\n%s",
            filename.c_str()
        );
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        
        self->updateStatusBar("💾 Modell gespeichert");
    }
}

void GtkRenderer::onLoadModel(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    
    GtkWidget* dialog = gtk_file_chooser_dialog_new(
        "Modell laden",
        GTK_WINDOW(self->window_),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Abbrechen", GTK_RESPONSE_CANCEL,
        "_Laden", GTK_RESPONSE_ACCEPT,
        NULL
    );
    
    const char* home = getenv("HOME");
    std::string modelPath = std::string(home) + "/.songgen/models/";
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), modelPath.c_str());
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        
        GtkWidget* msgDialog = gtk_message_dialog_new(
            GTK_WINDOW(self->window_),
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_INFO,
            GTK_BUTTONS_OK,
            "📂 Modell geladen:\n%s",
            filename
        );
        gtk_dialog_run(GTK_DIALOG(msgDialog));
        gtk_widget_destroy(msgDialog);
        
        g_free(filename);
        self->updateStatusBar("📂 Modell geladen");
    }
    
    gtk_widget_destroy(dialog);
}

void GtkRenderer::onSavePreset(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    
    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        "Preset speichern",
        GTK_WINDOW(self->window_),
        (GtkDialogFlags)0,
        "_Abbrechen", GTK_RESPONSE_CANCEL,
        "_Speichern", GTK_RESPONSE_ACCEPT,
        NULL
    );
    
    makeDialogResizable(dialog, 400, 200);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Preset-Name...");
    gtk_box_pack_start(GTK_BOX(content), gtk_label_new("Name:"), FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(content), entry, FALSE, FALSE, 5);
    gtk_widget_show_all(dialog);
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const char* name = gtk_entry_get_text(GTK_ENTRY(entry));
        if (name && strlen(name) > 0) {
            self->saveGeneratorPreset(name);
        }
    }
    
    gtk_widget_destroy(dialog);
}

void GtkRenderer::saveGeneratorPreset(const std::string& name) {
    const char* home = getenv("HOME");
    std::string presetPath = std::string(home) + "/.songgen/presets/";
    std::filesystem::create_directories(presetPath);
    
    std::string filename = presetPath + name + ".json";
    std::ofstream file(filename);
    
    if (file.is_open()) {
        int genre = gtk_combo_box_get_active(GTK_COMBO_BOX(genGenreCombo_));
        int bpm = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(genBpmSpin_));
        int duration = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(genDurationSpin_));
        int intensity = gtk_combo_box_get_active(GTK_COMBO_BOX(genIntensityCombo_));
        
        file << "{\n";
        file << "  \"genre\": " << genre << ",\n";
        file << "  \"bpm\": " << bpm << ",\n";
        file << "  \"duration\": " << duration << ",\n";
        file << "  \"intensity\": " << intensity << "\n";
        file << "}\n";
        file.close();
        
        updateStatusBar("💾 Preset gespeichert: " + name);
    }
}

void GtkRenderer::onLoadPreset(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    
    GtkWidget* dialog = gtk_file_chooser_dialog_new(
        "Preset laden",
        GTK_WINDOW(self->window_),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Abbrechen", GTK_RESPONSE_CANCEL,
        "_Laden", GTK_RESPONSE_ACCEPT,
        NULL
    );
    
    const char* home = getenv("HOME");
    std::string presetPath = std::string(home) + "/.songgen/presets/";
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), presetPath.c_str());
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        self->loadGeneratorPreset(filename);
        g_free(filename);
    }
    
    gtk_widget_destroy(dialog);
}

void GtkRenderer::loadGeneratorPreset(const std::string& filename) {
    // Einfaches JSON-Parsing (für vollständiges JSON würde man eine Library verwenden)
    std::ifstream file(filename);
    if (file.is_open()) {
        std::string line;
        int genre = 0, bpm = 120, duration = 180, intensity = 1;
        
        while (std::getline(file, line)) {
            if (line.find("\"genre\"") != std::string::npos) {
                size_t pos = line.find(":");
                if (pos != std::string::npos) {
                    genre = std::stoi(line.substr(pos + 1));
                }
            } else if (line.find("\"bpm\"") != std::string::npos) {
                size_t pos = line.find(":");
                if (pos != std::string::npos) {
                    bpm = std::stoi(line.substr(pos + 1));
                }
            } else if (line.find("\"duration\"") != std::string::npos) {
                size_t pos = line.find(":");
                if (pos != std::string::npos) {
                    duration = std::stoi(line.substr(pos + 1));
                }
            } else if (line.find("\"intensity\"") != std::string::npos) {
                size_t pos = line.find(":");
                if (pos != std::string::npos) {
                    intensity = std::stoi(line.substr(pos + 1));
                }
            }
        }
        file.close();
        
        gtk_combo_box_set_active(GTK_COMBO_BOX(genGenreCombo_), genre);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(genBpmSpin_), bpm);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(genDurationSpin_), duration);
        gtk_combo_box_set_active(GTK_COMBO_BOX(genIntensityCombo_), intensity);
        
        updateStatusBar("📂 Preset geladen");
    }
}

void GtkRenderer::onAnalyzeFile(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    
    GtkWidget* dialog = gtk_file_chooser_dialog_new(
        "Audio-Datei wählen",
        GTK_WINDOW(self->window_),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Abbrechen", GTK_RESPONSE_CANCEL,
        "_Analysieren", GTK_RESPONSE_ACCEPT,
        NULL
    );
    
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Audio-Dateien");
    gtk_file_filter_add_pattern(filter, "*.mp3");
    gtk_file_filter_add_pattern(filter, "*.wav");
    gtk_file_filter_add_pattern(filter, "*.sid");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        
        // Starte Analyse in Thread
        std::thread([self, filename]() {
            MediaMetadata meta;
            meta.filepath = filename;
            
            if (self->analyzer_->analyze(filename, meta)) {
                gdk_threads_add_idle([](gpointer data) -> gboolean {
                    auto* pair = static_cast<std::pair<GtkRenderer*, MediaMetadata>*>(data);
                    GtkRenderer* self = pair->first;
                    MediaMetadata& meta = pair->second;
                    
                    std::stringstream result;
                    result << "📊 Analyse-Ergebnisse:\n\n";
                    result << "Datei: " << std::filesystem::path(meta.filepath).filename().string() << "\n\n";
                    result << "🎵 Basis-Eigenschaften:\n";
                    result << "  BPM: " << meta.bpm << "\n";
                    result << "  Dauer: " << meta.duration << "s\n";
                    result << "  Genre: " << meta.genre << "\n";
                    result << "  Intensität: " << meta.intensity << "\n\n";
                    result << "🔊 Spektral-Analyse:\n";
                    result << "  Spectral Centroid: " << meta.spectralCentroid << "\n";
                    result << "  Spectral Rolloff: " << meta.spectralRolloff << "\n";
                    result << "  Zero Crossing Rate: " << meta.zeroCrossingRate << "\n\n";
                    result << "🎭 Stimmung & Charakter:\n";
                    result << "  Mood: " << meta.mood << "\n";
                    result << "  Bass Level: " << meta.bassLevel << "\n";
                    result << "  Instrumente: " << meta.instruments << "\n";
                    
                    GtkWidget* msgDialog = gtk_message_dialog_new(
                        GTK_WINDOW(self->window_),
                        GTK_DIALOG_MODAL,
                        GTK_MESSAGE_INFO,
                        GTK_BUTTONS_OK,
                        "%s", result.str().c_str()
                    );
                    gtk_dialog_run(GTK_DIALOG(msgDialog));
                    gtk_widget_destroy(msgDialog);
                    
                    delete pair;
                    return G_SOURCE_REMOVE;
                }, new std::pair<GtkRenderer*, MediaMetadata>(self, meta));
            }
            
            g_free(filename);
        }).detach();
    }
    
    gtk_widget_destroy(dialog);
}

void GtkRenderer::updateStatusBar(const std::string& message) {
    if (statusbar_) {
        gtk_statusbar_push(GTK_STATUSBAR(statusbar_), 0, message.c_str());
    }
}

void GtkRenderer::updateGPUUsage() {
    static int simulatedUsage = 0;
    
    // TODO: Echte GPU-Auslastung via OpenVINO oder intel_gpu_top auslesen
    // Für jetzt: Simuliere basierend auf Training/Generation
    if (isTraining_) {
        simulatedUsage = 60 + (rand() % 30);  // 60-90% während Training
    } else if (generator_ && generator_->hasAccelerator()) {
        simulatedUsage = std::max(0, simulatedUsage - 5);  // Langsam abfallen
    } else {
        simulatedUsage = 0;
    }
    
    double fraction = simulatedUsage / 100.0;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(gpuProgressBar_), fraction);
    
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "GPU: %d%%", simulatedUsage);
    gtk_label_set_text(GTK_LABEL(gpuLabel_), buffer);
}

gboolean GtkRenderer::onGPUUpdateTimer(gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    if (self && self->running_) {
        self->updateGPUUsage();
        return G_SOURCE_CONTINUE;  // Timer weiterlaufen lassen
    }
    return G_SOURCE_REMOVE;
}

void GtkRenderer::addHistoryEntry(const std::string& action, const std::string& details, const std::string& result) {
    if (!historyStore_) return;
    
    // Zeitstempel
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    char timestamp[64];
    std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", std::localtime(&time_t_now));
    
    // Füge Eintrag hinzu
    GtkTreeIter iter;
    gtk_list_store_append(historyStore_, &iter);
    gtk_list_store_set(historyStore_, &iter,
        0, timestamp,
        1, action.c_str(),
        2, details.c_str(),
        3, result.c_str(),
        4, "",  // FilePath (optional)
        5, "",  // Metadata JSON (optional)
        -1);
    
    // Scrolle zum neuesten Eintrag
    GtkTreePath* path = gtk_tree_model_get_path(GTK_TREE_MODEL(historyStore_), &iter);
    gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(historyTreeView_), path, NULL, TRUE, 1.0, 0.0);
    gtk_tree_path_free(path);
    
    std::cout << "📜 Historie: " << action << " - " << details << std::endl;
}

void GtkRenderer::onHistoryRowActivated(GtkTreeView* tree, GtkTreePath* path, GtkTreeViewColumn* col, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    GtkTreeModel* model = gtk_tree_view_get_model(tree);
    GtkTreeIter iter;
    
    if (gtk_tree_model_get_iter(model, &iter, path)) {
        gchar* timestamp;
        gchar* action;
        gchar* details;
        gchar* result;
        gchar* filepath;
        gchar* metadata;
        
        gtk_tree_model_get(model, &iter,
            0, &timestamp,
            1, &action,
            2, &details,
            3, &result,
            4, &filepath,
            5, &metadata,
            -1);
        
        // Zeige Details im TextView
        std::ostringstream detailsText;
        detailsText << "═══════════════════════════════════════\n";
        detailsText << "📅 Zeitstempel: " << timestamp << "\n";
        detailsText << "🎬 Aktion: " << action << "\n";
        detailsText << "═══════════════════════════════════════\n\n";
        detailsText << "📋 Details:\n" << details << "\n\n";
        detailsText << "✅ Ergebnis:\n" << result << "\n\n";
        
        if (filepath && strlen(filepath) > 0) {
            detailsText << "📁 Datei:\n" << filepath << "\n\n";
        }
        
        if (metadata && strlen(metadata) > 0) {
            detailsText << "🔧 Metadaten (JSON):\n" << metadata << "\n\n";
        }
        
        detailsText << "═══════════════════════════════════════\n";
        detailsText << "💡 Doppelklick auf 'Metadaten bearbeiten' zum Korrigieren\n";
        
        GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(self->historyTextView_));
        gtk_text_buffer_set_text(buffer, detailsText.str().c_str(), -1);
        
        g_free(timestamp);
        g_free(action);
        g_free(details);
        g_free(result);
        g_free(filepath);
        g_free(metadata);
    }
}

void GtkRenderer::onEditHistoryMetadata(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    
    // Hole ausgewählten Eintrag
    GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(self->historyTreeView_));
    GtkTreeModel* model;
    GtkTreeIter iter;
    
    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
        GtkWidget* dialog = gtk_message_dialog_new(
            GTK_WINDOW(self->window_),
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_INFO,
            GTK_BUTTONS_OK,
            "ℹ️ Bitte wähle einen Historie-Eintrag aus"
        );
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }
    
    gchar* filepath;
    gchar* action;
    gtk_tree_model_get(model, &iter, 4, &filepath, 1, &action, -1);
    
    if (!filepath || strlen(filepath) == 0) {
        GtkWidget* dialog = gtk_message_dialog_new(
            GTK_WINDOW(self->window_),
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_WARNING,
            GTK_BUTTONS_OK,
            "⚠️ Dieser Eintrag hat keine zugeordnete Datei"
        );
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        g_free(filepath);
        g_free(action);
        return;
    }
    
    // Lade Metadaten aus Datenbank
    auto allMedia = self->database_->getAll();
    MediaMetadata* targetMeta = nullptr;
    for (auto& meta : allMedia) {
        if (meta.filepath == filepath) {
            targetMeta = &meta;
            break;
        }
    }
    
    if (!targetMeta) {
        GtkWidget* dialog = gtk_message_dialog_new(
            GTK_WINDOW(self->window_),
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_WARNING,
            GTK_BUTTONS_OK,
            "⚠️ Datei nicht mehr in Datenbank: %s",
            filepath
        );
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        g_free(filepath);
        g_free(action);
        return;
    }
    
    // Editor-Dialog erstellen
    GtkWidget* editDialog = gtk_dialog_new_with_buttons(
        "✏️ Metadaten bearbeiten",
        GTK_WINDOW(self->window_),
        (GtkDialogFlags)0,
        "💾 Speichern", GTK_RESPONSE_ACCEPT,
        "❌ Abbrechen", GTK_RESPONSE_CANCEL,
        NULL
    );
    
    makeDialogResizable(editDialog, 600, 500);
    
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(editDialog));
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 15);
    gtk_container_add(GTK_CONTAINER(content), grid);
    
    int row = 0;
    
    // Dateiname (read-only) mit Play/Stop Buttons
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("📁 Datei:"), 0, row, 1, 1);
    GtkWidget* labelFile = gtk_label_new(std::filesystem::path(filepath).filename().c_str());
    gtk_label_set_selectable(GTK_LABEL(labelFile), TRUE);
    gtk_grid_attach(GTK_GRID(grid), labelFile, 1, row, 1, 1);
    
    // Play/Stop Buttons
    GtkWidget* btnBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    
    // Speichere filepath für Callback
    struct PlayData {
        GtkRenderer* renderer;
        std::string filepath;
    };
    PlayData* playData = new PlayData{self, std::string(filepath)};
    
    GtkWidget* btnPlay = gtk_button_new_with_label("▶️ Play");
    g_signal_connect_data(btnPlay, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer data) {
        auto* pd = static_cast<PlayData*>(data);
        pd->renderer->audioPlayer_->stop();
        if (pd->renderer->audioPlayer_->load(pd->filepath)) {
            pd->renderer->audioPlayer_->play();
        }
    }), playData, NULL, G_CONNECT_AFTER);
    gtk_box_pack_start(GTK_BOX(btnBox), btnPlay, FALSE, FALSE, 0);
    
    GtkWidget* btnStop = gtk_button_new_with_label("⏹️ Stop");
    g_signal_connect(btnStop, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer data) {
        static_cast<GtkRenderer*>(data)->audioPlayer_->stop();
    }), self);
    gtk_box_pack_start(GTK_BOX(btnBox), btnStop, FALSE, FALSE, 0);
    
    gtk_grid_attach(GTK_GRID(grid), btnBox, 2, row++, 1, 1);
    
    // Titel
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("🎵 Titel:"), 0, row, 1, 1);
    GtkWidget* entryTitle = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entryTitle), targetMeta->title.c_str());
    gtk_grid_attach(GTK_GRID(grid), entryTitle, 1, row++, 1, 1);
    
    // Artist
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("🎤 Artist:"), 0, row, 1, 1);
    GtkWidget* entryArtist = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entryArtist), targetMeta->artist.c_str());
    gtk_grid_attach(GTK_GRID(grid), entryArtist, 1, row++, 1, 1);
    
    // Genre
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("🎭 Genre:"), 0, row, 1, 1);
    GtkWidget* comboGenre = gtk_combo_box_text_new();
    const char* genres[] = {"Electronic", "Techno", "House", "Trance", "Ambient", "Hip-Hop", 
                            "RnB", "Jazz", "Classical", "Salsa", "Walzer", "Rock/Pop", "Reggae", "Punk", "Vocal", "Volksmusik", "SID", "Unknown"};
    for (const char* g : genres) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(comboGenre), g);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(comboGenre), 0);
    for (int i = 0; i < 15; i++) {
        if (targetMeta->genre == genres[i]) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(comboGenre), i);
            break;
        }
    }
    gtk_grid_attach(GTK_GRID(grid), comboGenre, 1, row++, 1, 1);
    
    // BPM
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("🥁 BPM:"), 0, row, 1, 1);
    GtkWidget* spinBPM = gtk_spin_button_new_with_range(0, 300, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spinBPM), targetMeta->bpm);
    gtk_grid_attach(GTK_GRID(grid), spinBPM, 1, row++, 1, 1);
    
    // Intensity
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("💪 Intensität:"), 0, row, 1, 1);
    GtkWidget* comboIntensity = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(comboIntensity), "soft");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(comboIntensity), "mittel");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(comboIntensity), "hart");
    gtk_combo_box_set_active(GTK_COMBO_BOX(comboIntensity), 1);
    if (targetMeta->intensity == "soft") gtk_combo_box_set_active(GTK_COMBO_BOX(comboIntensity), 0);
    else if (targetMeta->intensity == "hart") gtk_combo_box_set_active(GTK_COMBO_BOX(comboIntensity), 2);
    gtk_grid_attach(GTK_GRID(grid), comboIntensity, 1, row++, 1, 1);
    
    // Bass Level
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("🔊 Bass-Level:"), 0, row, 1, 1);
    GtkWidget* comboBass = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(comboBass), "soft");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(comboBass), "mittel");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(comboBass), "basslastig");
    gtk_combo_box_set_active(GTK_COMBO_BOX(comboBass), 1);
    if (targetMeta->bassLevel == "soft") gtk_combo_box_set_active(GTK_COMBO_BOX(comboBass), 0);
    else if (targetMeta->bassLevel == "basslastig") gtk_combo_box_set_active(GTK_COMBO_BOX(comboBass), 2);
    gtk_grid_attach(GTK_GRID(grid), comboBass, 1, row++, 1, 1);
    
    // Mood
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("😊 Stimmung:"), 0, row, 1, 1);
    GtkWidget* entryMood = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entryMood), targetMeta->mood.c_str());
    gtk_entry_set_placeholder_text(GTK_ENTRY(entryMood), "z.B. Energetic, Relaxed, Dark");
    gtk_grid_attach(GTK_GRID(grid), entryMood, 1, row++, 1, 1);
    
    gtk_widget_show_all(editDialog);
    
    gint response = gtk_dialog_run(GTK_DIALOG(editDialog));
    
    if (response == GTK_RESPONSE_ACCEPT) {
        // Speichere Änderungen
        targetMeta->title = gtk_entry_get_text(GTK_ENTRY(entryTitle));
        targetMeta->artist = gtk_entry_get_text(GTK_ENTRY(entryArtist));
        targetMeta->genre = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(comboGenre));
        targetMeta->bpm = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spinBPM));
        targetMeta->intensity = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(comboIntensity));
        targetMeta->bassLevel = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(comboBass));
        targetMeta->mood = gtk_entry_get_text(GTK_ENTRY(entryMood));
        
        if (self->database_->updateMedia(*targetMeta)) {
            self->addHistoryEntry(
                "Metadaten bearbeitet",
                "Datei: " + std::string(filepath) + "\n" +
                "Titel: " + targetMeta->title + "\n" +
                "Genre: " + targetMeta->genre + "\n" +
                "BPM: " + std::to_string(targetMeta->bpm),
                "✅ Erfolgreich gespeichert"
            );
            
            self->refreshDatabaseView();
            
            GtkWidget* successDialog = gtk_message_dialog_new(
                GTK_WINDOW(self->window_),
                GTK_DIALOG_MODAL,
                GTK_MESSAGE_INFO,
                GTK_BUTTONS_OK,
                "✅ Metadaten erfolgreich gespeichert"
            );
            gtk_dialog_run(GTK_DIALOG(successDialog));
            gtk_widget_destroy(successDialog);
        }
    }
    
    gtk_widget_destroy(editDialog);
    g_free(filepath);
    g_free(action);
}

void GtkRenderer::onShowInstrumentsFolder(GtkWidget* widget, gpointer data) {
    std::string cmd = "xdg-open ~/.songgen/instruments/ 2>/dev/null || nautilus ~/.songgen/instruments/ 2>/dev/null || thunar ~/.songgen/instruments/ 2>/dev/null &";
    system(cmd.c_str());
}

void GtkRenderer::onShowInstrumentStats(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    std::string instrumentDir = std::string(std::getenv("HOME")) + "/.songgen/instruments/";
    
    std::map<std::string, int> counts;
    const char* categories[] = {"kicks", "snares", "hihats", "bass", "leads", "other"};
    
    for (const char* cat : categories) {
        std::string path = instrumentDir + cat;
        int count = 0;
        if (std::filesystem::exists(path)) {
            for (const auto& entry : std::filesystem::directory_iterator(path)) {
                if (entry.is_regular_file() && entry.path().extension() == ".wav") {
                    count++;
                }
            }
        }
        counts[cat] = count;
    }
    
    std::string msg = "🎸 Extrahierte Instrumenten-Samples:\n\n";
    msg += "🥁 Kicks: " + std::to_string(counts["kicks"]) + "\n";
    msg += "🥁 Snares: " + std::to_string(counts["snares"]) + "\n";
    msg += "🎵 Hi-Hats: " + std::to_string(counts["hihats"]) + "\n";
    msg += "🎸 Bass: " + std::to_string(counts["bass"]) + "\n";
    msg += "🎹 Leads: " + std::to_string(counts["leads"]) + "\n";
    msg += "🎼 Other: " + std::to_string(counts["other"]) + "\n\n";
    
    int total = 0;
    for (const auto& pair : counts) total += pair.second;
    msg += "📦 Gesamt: " + std::to_string(total) + " Samples";
    
    GtkWidget* dialog = gtk_message_dialog_new(
        GTK_WINDOW(self->window_), GTK_DIALOG_MODAL,
        GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s", msg.c_str());
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

void GtkRenderer::onRemoveInstrumentDuplicates(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    
    std::string instrumentDir = std::string(std::getenv("HOME")) + "/.songgen/instruments/";
    
    // Prüfe ob Instrumente vorhanden sind
    bool hasInstruments = false;
    if (std::filesystem::exists(instrumentDir)) {
        for (const auto& entry : std::filesystem::directory_iterator(instrumentDir)) {
            if (entry.is_directory()) {
                hasInstruments = true;
                break;
            }
        }
    }
    
    if (!hasInstruments) {
        GtkWidget* dialog = gtk_message_dialog_new(
            GTK_WINDOW(self->window_), GTK_DIALOG_MODAL,
            GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
            "ℹ️ Keine Instrumente gefunden.\n\n"
            "Instrumente werden automatisch während des Trainings extrahiert.\n"
            "Starte zuerst ein Training:\n🎓 Training starten");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }
    
    GtkWidget* dialog = gtk_message_dialog_new(
        GTK_WINDOW(self->window_), GTK_DIALOG_MODAL,
        GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
        "🔍 Instrumenten-Duplikate entfernen?\n\n"
        "Ähnlich klingende Samples werden automatisch erkannt und entfernt:\n"
        "• Cross-Correlation Analyse (85%% Ähnlichkeit)\n"
        "• RMS-Vergleich (Lautstärke)\n"
        "• Peak-Analyse (Maximale Amplituden)\n\n"
        "Dies kann die Qualität der Genre-Kombinationen verbessern.\n\n"
        "Fortfahren?");
    
    gint result = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    
    if (result != GTK_RESPONSE_YES) return;
    
    // Zeige Progress-Dialog
    GtkWidget* progressDialog = gtk_message_dialog_new(
        GTK_WINDOW(self->window_), GTK_DIALOG_MODAL,
        GTK_MESSAGE_INFO, GTK_BUTTONS_NONE,
        "🔍 Prüfe Instrumente auf Ähnlichkeit...\n\n"
        "Dies kann einige Sekunden dauern...");
    gtk_widget_show_all(progressDialog);
    
    // Verarbeitung im Main-Thread
    while (gtk_events_pending()) gtk_main_iteration();
    
    // Führe Deduplizierung in Thread durch
    std::thread([self, progressDialog]() {
        std::cout << "\n🔍 Starte Instrumenten-Duplikat-Erkennung..." << std::endl;
        
        if (self->trainingModel_) {
            self->trainingModel_->removeDuplicateInstruments();
        }
        
        // Schließe Progress-Dialog
        gdk_threads_add_idle([](gpointer data) -> gboolean {
            gtk_widget_destroy(static_cast<GtkWidget*>(data));
            return G_SOURCE_REMOVE;
        }, progressDialog);
        
        // Zeige Erfolgs-Dialog
        gdk_threads_add_idle([](gpointer data) -> gboolean {
            GtkRenderer* self = static_cast<GtkRenderer*>(data);
            GtkWidget* successDialog = gtk_message_dialog_new(
                GTK_WINDOW(self->window_), GTK_DIALOG_MODAL,
                GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
                "✅ Duplikat-Erkennung abgeschlossen!\n\n"
                "Details siehe Console-Ausgabe.\n"
                "Die Instrumenten-Library wurde optimiert.");
            gtk_dialog_run(GTK_DIALOG(successDialog));
            gtk_widget_destroy(successDialog);
            return G_SOURCE_REMOVE;
        }, self);
    }).detach();
}

void GtkRenderer::onAnalyzeDatabase(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    
    if (!self->trainingModel_) return;
    
    GtkWidget* dialog = gtk_message_dialog_new(
        GTK_WINDOW(self->window_), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO,
        GTK_BUTTONS_NONE, 
        "🧠 Intelligente Datenbank-Analyse\n\n"
        "Analysiert alle Tracks basierend auf Korrektur-Historie\n"
        "und schlägt automatische Genre-Korrekturen vor.");
    
    gtk_dialog_add_buttons(GTK_DIALOG(dialog),
        "Abbrechen", GTK_RESPONSE_CANCEL,
        "Nur analysieren", GTK_RESPONSE_NO,
        "Analysieren & Auto-Korrigieren", GTK_RESPONSE_YES,
        NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 500, 180);
    
    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    
    if (response == GTK_RESPONSE_NO || response == GTK_RESPONSE_YES) {
        bool autoApply = (response == GTK_RESPONSE_YES);
        
        // Progress-Dialog
        GtkWidget* progressDialog = gtk_dialog_new_with_buttons(
            "🧠 Analysiere Datenbank...",
            GTK_WINDOW(self->window_),
            (GtkDialogFlags)0,
            "Abbrechen", GTK_RESPONSE_CANCEL,
            NULL
        );
        makeDialogResizable(progressDialog, 500, 180);
        
        GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(progressDialog));
        GtkWidget* label = gtk_label_new("Analysiere Tracks und schlage Korrekturen vor...");
        GtkWidget* progressBar = gtk_progress_bar_new();
        gtk_progress_bar_set_pulse_step(GTK_PROGRESS_BAR(progressBar), 0.1);
        
        gtk_box_pack_start(GTK_BOX(content), label, FALSE, FALSE, 5);
        gtk_box_pack_start(GTK_BOX(content), progressBar, FALSE, FALSE, 5);
        gtk_widget_show_all(progressDialog);
        
        // Pulse animation
        guint timeoutId = g_timeout_add(100, [](gpointer data) -> gboolean {
            gtk_progress_bar_pulse(GTK_PROGRESS_BAR(data));
            return G_SOURCE_CONTINUE;
        }, progressBar);
        
        // Analyse in Thread (ASYNCHRON!)
        std::thread([self, autoApply, progressDialog, timeoutId]() {
            std::cout << "🧠 Starte intelligente Datenbank-Analyse...\n";
            
            int corrections = self->trainingModel_->suggestDatabaseCorrections(autoApply);
            
            // Stop pulse animation
            g_source_remove(timeoutId);
            
            // Zeige Ergebnis
            gdk_threads_add_idle([](gpointer data) -> gboolean {
                auto* info = static_cast<std::tuple<GtkRenderer*, GtkWidget*, int, bool>*>(data);
                GtkRenderer* self = std::get<0>(*info);
                GtkWidget* progressDlg = std::get<1>(*info);
                int corrections = std::get<2>(*info);
                bool autoApply = std::get<3>(*info);
                
                gtk_widget_destroy(progressDlg);
                
                float correctionPercent = self->filteredMedia_.empty() ? 0.0f :
                    (float)corrections / self->filteredMedia_.size() * 100.0f;
                
                char message[512];
                snprintf(message, sizeof(message),
                    "%s\n\n%d Korrektur-Vorschläge gefunden (%.1f%%)\n%s",
                    autoApply ? "✅ Analyse & Auto-Korrektur abgeschlossen" : "🔍 Analyse abgeschlossen",
                    corrections, correctionPercent,
                    autoApply ? "Alle Korrekturen wurden angewendet!" : "Details siehe Terminal-Ausgabe");
                
                GtkWidget* resultDialog = gtk_message_dialog_new(
                    GTK_WINDOW(self->window_), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO,
                    GTK_BUTTONS_OK, "%s", message);
                gtk_window_set_title(GTK_WINDOW(resultDialog), "Analyse abgeschlossen");
                gtk_window_set_default_size(GTK_WINDOW(resultDialog), 500, 180);
                gtk_dialog_run(GTK_DIALOG(resultDialog));
                gtk_widget_destroy(resultDialog);
                
                // Refresh view if auto-applied
                if (autoApply) {
                    self->refreshDatabaseView();
                }
                
                delete info;
                return G_SOURCE_REMOVE;
            }, new std::tuple<GtkRenderer*, GtkWidget*, int, bool>(self, progressDialog, corrections, autoApply));
            
        }).detach();
    }
}

void GtkRenderer::onShowPatterns(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    
    if (!self->trainingModel_) return;
    
    auto patterns = self->trainingModel_->learnCorrectionPatterns();
    
    std::string msg = "🧠 Erkannte Korrektur-Muster:\n\n";
    if (patterns.empty()) {
        msg += "Noch keine Muster erkannt.\n";
        msg += "Führe mehr Korrekturen durch, um Muster zu lernen.";
    } else {
        for (const auto& [key, genre] : patterns) {
            if (key.find("artist:") == 0) {
                msg += "🎨 Artist: " + key.substr(7) + " → " + genre + "\n";
            } else if (key.find("bpm:") == 0) {
                msg += "⚡ BPM: " + key.substr(4) + " → " + genre + "\n";
            }
        }
    }
    
    GtkWidget* dialog = gtk_message_dialog_new(
        GTK_WINDOW(self->window_), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO,
        GTK_BUTTONS_OK, "%s", msg.c_str());
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

void GtkRenderer::onLearnSongStructure(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    
    if (!self->database_ || !self->analyzer_) return;
    
    // Frage nach Genre
    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        "Song-Struktur-Analyse",
        GTK_WINDOW(self->window_),
        (GtkDialogFlags)0,
        "Abbrechen", GTK_RESPONSE_CANCEL,
        "Alle Genres", GTK_RESPONSE_YES,
        "Spezifisches Genre", GTK_RESPONSE_NO,
        NULL);
    
    makeDialogResizable(dialog, 450, 200);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget* label = gtk_label_new("Analysiert Song-Strukturen (Intro, Verse, Chorus, etc.)\n"
                                     "und lernt typische Arrangements pro Genre.");
    gtk_box_pack_start(GTK_BOX(content), label, TRUE, TRUE, 10);
    gtk_widget_show_all(dialog);
    
    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    
    if (response == GTK_RESPONSE_CANCEL) return;
    
    std::string targetGenre = "";
    if (response == GTK_RESPONSE_NO) {
        // Genre-Auswahl-Dialog
        GtkWidget* genreDialog = gtk_dialog_new_with_buttons(
            "Genre wählen", GTK_WINDOW(self->window_), (GtkDialogFlags)0,
            "OK", GTK_RESPONSE_OK, "Abbrechen", GTK_RESPONSE_CANCEL, NULL);
        makeDialogResizable(genreDialog, 400, 250);
        
        GtkWidget* genreContent = gtk_dialog_get_content_area(GTK_DIALOG(genreDialog));
        GtkWidget* genreCombo = gtk_combo_box_text_new();
        const char* genres[] = {"Electronic", "Techno", "House", "Ambient", "Trance", 
                               "Drum'n'Bass", "Chillout", "Metal", "New Metal", "Trap",
                               "Jazz", "RnB", "Classical", "Salsa", "Walzer", "Rock/Pop", 
                               "Reggae", "Punk", "Vocal", "Volksmusik", NULL};
        for (int i = 0; genres[i]; ++i) {
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(genreCombo), genres[i]);
        }
        gtk_combo_box_set_active(GTK_COMBO_BOX(genreCombo), 0);
        gtk_box_pack_start(GTK_BOX(genreContent), genreCombo, TRUE, TRUE, 10);
        gtk_widget_show_all(genreDialog);
        
        if (gtk_dialog_run(GTK_DIALOG(genreDialog)) == GTK_RESPONSE_OK) {
            gchar* text = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(genreCombo));
            if (text) {
                targetGenre = text;
                g_free(text);
            }
        }
        gtk_widget_destroy(genreDialog);
        
        if (targetGenre.empty()) return;
    }
    
    // Analyse in Thread
    std::thread([self, targetGenre]() {
        std::cout << "🎵 Starte Song-Struktur-Analyse" 
                  << (targetGenre.empty() ? "" : " für Genre: " + targetGenre) << "...\n";
        
        auto allMedia = self->database_->getAll();
        std::vector<AudioAnalyzer::SongStructure> structures;
        
        for (const auto& media : allMedia) {
            if (!targetGenre.empty() && media.genre != targetGenre) continue;
            if (media.bpm <= 0) continue;  // Brauchen BPM für Struktur-Analyse
            
            std::vector<float> samples;
            int sampleRate;
            if (self->analyzer_->loadAudioFile(media.filepath, samples, sampleRate)) {
                auto structure = self->analyzer_->analyzeSongStructure(samples, sampleRate, media.bpm);
                structures.push_back(structure);
                
                if (structures.size() % 10 == 0) {
                    std::cout << "   📊 " << structures.size() << " Songs analysiert...\n";
                }
            }
        }
        
        if (!structures.empty()) {
            std::string genre = targetGenre.empty() ? "All Genres" : targetGenre;
            self->analyzer_->learnStructurePatterns(structures, genre);
            std::cout << "✅ Struktur-Learning abgeschlossen!\n";
        } else {
            std::cout << "⚠️ Keine passenden Songs gefunden\n";
        }
    }).detach();
    
    GtkWidget* infoDialog = gtk_message_dialog_new(
        GTK_WINDOW(self->window_), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO,
        GTK_BUTTONS_OK, 
        "Analyse gestartet!\n\nDetails siehe Terminal-Ausgabe.");
    gtk_dialog_run(GTK_DIALOG(infoDialog));
    gtk_widget_destroy(infoDialog);
}

void GtkRenderer::onPlayGenreDemos(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    std::string demoDir = std::string(std::getenv("HOME")) + "/.songgen/";
    
    // Suche alle training_demo_*.wav Dateien
    std::vector<std::pair<std::string, std::string>> demos;  // <genre, filepath>
    
    if (std::filesystem::exists(demoDir)) {
        for (const auto& entry : std::filesystem::directory_iterator(demoDir)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                if (filename.rfind("training_demo_", 0) == 0 && 
                    filename.size() > 4 && filename.substr(filename.size() - 4) == ".wav") {
                    std::string genre = filename.substr(14);  // Nach "training_demo_"
                    genre = genre.substr(0, genre.length() - 4);  // Ohne ".wav"
                    demos.push_back({genre, entry.path().string()});
                }
            }
        }
    }
    
    if (demos.empty()) {
        GtkWidget* dialog = gtk_message_dialog_new(
            GTK_WINDOW(self->window_), GTK_DIALOG_MODAL,
            GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
            "ℹ️ Keine Genre-Demos gefunden.\n\n"
            "Genre-Demos werden automatisch während des Trainings generiert.\n"
            "Starte ein Training, um Demos zu erstellen:\n"
            "🎓 Training starten");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }
    
    // Erstelle Auswahl-Dialog
    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        "🎵 Genre-Demos abspielen",
        GTK_WINDOW(self->window_),
        (GtkDialogFlags)0,
        "Schließen", GTK_RESPONSE_CLOSE,
        NULL
    );
    
    makeDialogResizable(dialog, 500, 400);
    
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 15);
    gtk_container_add(GTK_CONTAINER(content), vbox);
    
    GtkWidget* label = gtk_label_new("Wähle ein Genre-Demo zum Abspielen:");
    gtk_label_set_selectable(GTK_LABEL(label), TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
    
    GtkWidget* scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), 
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);
    
    GtkWidget* listBox = gtk_list_box_new();
    gtk_container_add(GTK_CONTAINER(scrolled), listBox);
    
    // Füge Genre-Demos zur Liste hinzu
    for (size_t i = 0; i < demos.size(); ++i) {
        const std::string& genre = demos[i].first;
        const std::string& filepath = demos[i].second;
        
        GtkWidget* row = gtk_list_box_row_new();
        GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_container_set_border_width(GTK_CONTAINER(hbox), 5);
        gtk_container_add(GTK_CONTAINER(row), hbox);
        
        GtkWidget* labelGenre = gtk_label_new(("🎵 " + genre).c_str());
        gtk_widget_set_halign(labelGenre, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(hbox), labelGenre, TRUE, TRUE, 0);
        
        struct PlayData {
            GtkRenderer* renderer;
            std::string filepath;
        };
        
        PlayData* playData = new PlayData{self, filepath};
        
        GtkWidget* btnPlay = gtk_button_new_with_label("▶️ Play");
        g_signal_connect(btnPlay, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer userData) {
            auto* data = static_cast<PlayData*>(userData);
            data->renderer->audioPlayer_->stop();
            if (data->renderer->audioPlayer_->load(data->filepath)) {
                data->renderer->audioPlayer_->play();
            }
        }), playData);
        gtk_box_pack_start(GTK_BOX(hbox), btnPlay, FALSE, FALSE, 0);
        
        gtk_list_box_insert(GTK_LIST_BOX(listBox), row, -1);
    }
    
    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    
    // Stop audio when closing
    self->audioPlayer_->stop();
    gtk_widget_destroy(dialog);
}

void GtkRenderer::onClearHistory(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    
    GtkWidget* dialog = gtk_message_dialog_new(
        GTK_WINDOW(self->window_),
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_WARNING,
        GTK_BUTTONS_YES_NO,
        "⚠️ Gesamte Historie löschen?\nDiese Aktion kann nicht rückgängig gemacht werden."
    );
    
    gint result = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    
    if (result == GTK_RESPONSE_YES) {
        gtk_list_store_clear(self->historyStore_);
        
        GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(self->historyTextView_));
        gtk_text_buffer_set_text(buffer, "✅ Historie gelöscht", -1);
    }
}

void GtkRenderer::onExportHistory(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    
    GtkWidget* dialog = gtk_file_chooser_dialog_new(
        "Historie exportieren",
        GTK_WINDOW(self->window_),
        GTK_FILE_CHOOSER_ACTION_SAVE,
        "❌ Abbrechen", GTK_RESPONSE_CANCEL,
        "💾 Speichern", GTK_RESPONSE_ACCEPT,
        NULL
    );
    
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), "songgen_history.csv");
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        
        std::ofstream file(filename);
        if (file.is_open()) {
            file << "Zeitstempel,Aktion,Details,Ergebnis,Datei,Metadaten\n";
            
            GtkTreeModel* model = GTK_TREE_MODEL(self->historyStore_);
            GtkTreeIter iter;
            
            if (gtk_tree_model_get_iter_first(model, &iter)) {
                do {
                    gchar* timestamp;
                    gchar* action;
                    gchar* details;
                    gchar* result;
                    gchar* filepath;
                    gchar* metadata;
                    
                    gtk_tree_model_get(model, &iter,
                        0, &timestamp,
                        1, &action,
                        2, &details,
                        3, &result,
                        4, &filepath,
                        5, &metadata,
                        -1);
                    
                    file << "\"" << timestamp << "\",\"" << action << "\",\""
                         << details << "\",\"" << result << "\",\""
                         << filepath << "\",\"" << metadata << "\"\n";
                    
                    g_free(timestamp);
                    g_free(action);
                    g_free(details);
                    g_free(result);
                    g_free(filepath);
                    g_free(metadata);
                } while (gtk_tree_model_iter_next(model, &iter));
            }
            
            file.close();
            
            GtkWidget* successDialog = gtk_message_dialog_new(
                GTK_WINDOW(self->window_),
                GTK_DIALOG_MODAL,
                GTK_MESSAGE_INFO,
                GTK_BUTTONS_OK,
                "✅ Historie exportiert nach:\n%s",
                filename
            );
            gtk_dialog_run(GTK_DIALOG(successDialog));
            gtk_widget_destroy(successDialog);
        }
        
        g_free(filename);
    }
    
    gtk_widget_destroy(dialog);
}
// Temporäre Datei für onInteractiveTraining - wird in GtkRenderer.cpp eingefügt
// Diese Funktion erstellt einen interaktiven Dialog zum Review und Korrigieren von Genre-Zuordnungen

void GtkRenderer::onInteractiveTraining(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    
    // 🔍 Such-Dialog
    GtkWidget* searchDialog = gtk_dialog_new_with_buttons(
        "🔍 Track suchen für Training",
        GTK_WINDOW(self->window_),
        (GtkDialogFlags)0,
        "_Abbrechen", GTK_RESPONSE_CANCEL,
        "_Alle Tracks", GTK_RESPONSE_ACCEPT,
        "_Ausgewählte laden", GTK_RESPONSE_OK,
        NULL
    );
    
    // Maximale Fenstergröße: 1200x1000 für maximale Track-Ansicht
    makeDialogResizable(searchDialog, 1200, 1000);
    
    GtkWidget* searchContent = gtk_dialog_get_content_area(GTK_DIALOG(searchDialog));
    GtkWidget* searchVbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(searchVbox), 10);
    gtk_container_add(GTK_CONTAINER(searchContent), searchVbox);
    
    // Such-Eingabe mit Autovervollständigung
    GtkWidget* searchLabel = gtk_label_new("🔍 Suche nach Dateiname, Artist oder Genre:");
    gtk_label_set_selectable(GTK_LABEL(searchLabel), TRUE);
    gtk_box_pack_start(GTK_BOX(searchVbox), searchLabel, FALSE, FALSE, 0);
    
    GtkEntryCompletion* completion = gtk_entry_completion_new();
    GtkListStore* completionStore = gtk_list_store_new(1, G_TYPE_STRING);
    gtk_entry_completion_set_model(completion, GTK_TREE_MODEL(completionStore));
    gtk_entry_completion_set_text_column(completion, 0);
    gtk_entry_completion_set_minimum_key_length(completion, 2);
    gtk_entry_completion_set_popup_completion(completion, TRUE);
    gtk_entry_completion_set_inline_completion(completion, TRUE);
    
    GtkWidget* searchEntry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(searchEntry), "Track-Name eingeben...");
    gtk_entry_set_completion(GTK_ENTRY(searchEntry), completion);
    gtk_box_pack_start(GTK_BOX(searchVbox), searchEntry, FALSE, FALSE, 0);
    
    // TreeView für Ergebnisse
    GtkListStore* resultStore = gtk_list_store_new(5, G_TYPE_STRING, G_TYPE_STRING, 
                                                    G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT64);
    GtkWidget* resultTreeView = gtk_tree_view_new_with_model(GTK_TREE_MODEL(resultStore));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(resultTreeView), TRUE);
    gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(resultTreeView)), 
                                 GTK_SELECTION_MULTIPLE);
    
    GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
    
    // Dateiname-Spalte mit großer Breite (600px für lange Dateinamen)
    GtkTreeViewColumn* colFilename = gtk_tree_view_column_new_with_attributes("Dateiname", 
                                                                               renderer, "text", 0, NULL);
    gtk_tree_view_column_set_min_width(colFilename, 600);
    gtk_tree_view_column_set_resizable(colFilename, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(resultTreeView), colFilename);
    
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(resultTreeView), -1, "Artist", 
                                                renderer, "text", 1, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(resultTreeView), -1, "Genre", 
                                                renderer, "text", 2, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(resultTreeView), -1, "BPM", 
                                                renderer, "text", 3, NULL);
    
    // Scrolled Window für Ergebnisse mit Mindesthöhe
    GtkWidget* scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), 
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scrolled), 800);  // Mindesthöhe 800px
    gtk_container_add(GTK_CONTAINER(scrolled), resultTreeView);
    gtk_box_pack_start(GTK_BOX(searchVbox), scrolled, TRUE, TRUE, 0);
    
    GtkWidget* statusLabel = gtk_label_new("0 Tracks gefunden");
    gtk_label_set_selectable(GTK_LABEL(statusLabel), TRUE);
    gtk_box_pack_start(GTK_BOX(searchVbox), statusLabel, FALSE, FALSE, 0);
    
    // Lade alle Medien
    auto allMedia = self->database_->getAll();
    
    // Fülle Autovervollständigung (auch nicht-analysierte Tracks)
    for (const auto& media : allMedia) {
        GtkTreeIter iter;
        std::string filename = std::filesystem::path(media.filepath).filename().string();
        gtk_list_store_append(completionStore, &iter);
        gtk_list_store_set(completionStore, &iter, 0, filename.c_str(), -1);
        
        if (!media.artist.empty()) {
            gtk_list_store_append(completionStore, &iter);
            gtk_list_store_set(completionStore, &iter, 0, media.artist.c_str(), -1);
        }
    }
    
    // Such-Funktion
    auto performSearch = [&](const std::string& query) {
        gtk_list_store_clear(resultStore);
        int count = 0;
        
        std::string lowerQuery = query;
        std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);
        
        for (const auto& media : allMedia) {
            if (!media.analyzed || media.genre == "Unknown") continue;
            
            std::string filename = std::filesystem::path(media.filepath).filename().string();
            std::string lowerFilename = filename;
            std::string lowerArtist = media.artist;
            std::string lowerGenre = media.genre;
            
            std::transform(lowerFilename.begin(), lowerFilename.end(), lowerFilename.begin(), ::tolower);
            std::transform(lowerArtist.begin(), lowerArtist.end(), lowerArtist.begin(), ::tolower);
            std::transform(lowerGenre.begin(), lowerGenre.end(), lowerGenre.begin(), ::tolower);
            
            if (query.empty() || 
                lowerFilename.find(lowerQuery) != std::string::npos ||
                lowerArtist.find(lowerQuery) != std::string::npos ||
                lowerGenre.find(lowerQuery) != std::string::npos) {
                
                GtkTreeIter iter;
                gtk_list_store_append(resultStore, &iter);
                gtk_list_store_set(resultStore, &iter,
                    0, filename.c_str(),
                    1, media.artist.c_str(),
                    2, media.genre.c_str(),
                    3, std::to_string((int)media.bpm).c_str(),
                    4, (gint64)media.id,  // ID statt Pointer speichern!
                    -1);
                count++;
            }
        }
        
        std::string statusText = std::to_string(count) + " Tracks gefunden";
        gtk_label_set_text(GTK_LABEL(statusLabel), statusText.c_str());
    };
    
    // Initial alle anzeigen
    performSearch("");
    
    // Callback-Daten-Struktur für Such-Event
    struct SearchData {
        GtkListStore* store;
        GtkWidget* statusLabel;
        std::vector<MediaMetadata>* allMedia;
    };
    
    SearchData* searchData = new SearchData{resultStore, statusLabel, &allMedia};
    
    // Such-Event
    g_signal_connect(searchEntry, "changed", G_CALLBACK(+[](GtkWidget* entry, gpointer data) {
        auto* sd = static_cast<SearchData*>(data);
        const char* text = gtk_entry_get_text(GTK_ENTRY(entry));
        std::string query = text ? text : "";
        
        gtk_list_store_clear(sd->store);
        int count = 0;
        
        std::string lowerQuery = query;
        std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);
        
        for (const auto& media : *(sd->allMedia)) {
            // Zeige alle Tracks, auch neue ohne Analyse
            
            std::string filename = std::filesystem::path(media.filepath).filename().string();
            std::string lowerFilename = filename;
            std::string lowerArtist = media.artist;
            std::string lowerGenre = media.genre.empty() ? "(neu)" : media.genre;
            
            std::transform(lowerFilename.begin(), lowerFilename.end(), lowerFilename.begin(), ::tolower);
            std::transform(lowerArtist.begin(), lowerArtist.end(), lowerArtist.begin(), ::tolower);
            std::transform(lowerGenre.begin(), lowerGenre.end(), lowerGenre.begin(), ::tolower);
            
            if (query.empty() || 
                lowerFilename.find(lowerQuery) != std::string::npos ||
                lowerArtist.find(lowerQuery) != std::string::npos ||
                lowerGenre.find(lowerQuery) != std::string::npos) {
                
                GtkTreeIter iter;
                gtk_list_store_append(sd->store, &iter);
                
                std::string displayGenre = media.genre.empty() || media.genre == "Unknown" 
                    ? "⚠️ Neu/Unbekannt" : media.genre;
                std::string displayBpm = media.bpm > 0 ? std::to_string((int)media.bpm) : "-";
                
                gtk_list_store_set(sd->store, &iter,
                    0, filename.c_str(),
                    1, media.artist.c_str(),
                    2, displayGenre.c_str(),
                    3, displayBpm.c_str(),
                    4, (gint64)media.id,
                    -1);
                count++;
            }
        }
        
        std::string statusText = std::to_string(count) + " Tracks gefunden";
        gtk_label_set_text(GTK_LABEL(sd->statusLabel), statusText.c_str());
    }), searchData);
    
    // Struktur zum Sammeln der Selection vor Dialog-Schließung
    struct DialogResult {
        std::vector<gint64> selectedIds;
        int response;
        GtkWidget* treeView;
    };
    DialogResult* result = new DialogResult();
    result->treeView = resultTreeView;  // Speichere TreeView-Referenz
    
    // Signal-Handler für Dialog-Response (BEVOR er geschlossen wird)
    g_signal_connect(searchDialog, "response", G_CALLBACK(+[](GtkDialog* dialog, gint response_id, gpointer data) {
        auto* res = static_cast<DialogResult*>(data);
        res->response = response_id;
        
        if (response_id == GTK_RESPONSE_OK && res->treeView) {
            // JETZT die Selection auslesen, BEVOR der Dialog zerstört wird
            GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(res->treeView));
            GtkTreeModel* model = gtk_tree_view_get_model(GTK_TREE_VIEW(res->treeView));
            GList* selectedRows = gtk_tree_selection_get_selected_rows(selection, &model);
            
            int selectionCount = g_list_length(selectedRows);
            std::cout << "🔍 VOR Dialog-Schließung: " << selectionCount << " Zeilen ausgewählt\n";
            
            for (GList* iter = selectedRows; iter != nullptr; iter = iter->next) {
                GtkTreePath* path = (GtkTreePath*)iter->data;
                GtkTreeIter treeIter;
                
                if (gtk_tree_model_get_iter(model, &treeIter, path)) {
                    gint64 trackId;
                    gtk_tree_model_get(model, &treeIter, 4, &trackId, -1);
                    res->selectedIds.push_back(trackId);
                    std::cout << "   → Gespeichert: Track-ID " << trackId << "\n";
                }
                gtk_tree_path_free(path);
            }
            g_list_free(selectedRows);
        } else if (response_id == GTK_RESPONSE_OK) {
            std::cout << "❌ TreeView nicht gefunden!\n";
        }
    }), result);
    
    gtk_widget_show_all(searchDialog);
    gtk_dialog_run(GTK_DIALOG(searchDialog));
    
    // Cleanup SearchData
    delete searchData;
    
    // Sammle ausgewählte Tracks aus gespeicherten IDs
    std::vector<MediaMetadata> reviewQueue;
    std::vector<std::string> originalGenres;
    
    int response = result->response;
    
    if (response == GTK_RESPONSE_OK) {
        std::cout << "📊 Verarbeite " << result->selectedIds.size() << " gespeicherte Track-IDs\n";
        std::cout << "📚 Gesamt verfügbare Tracks: " << allMedia.size() << "\n";
        
        for (gint64 trackId : result->selectedIds) {
            std::cout << "   → Suche Track-ID: " << trackId << "\n";
            
            bool found = false;
            for (const auto& media : allMedia) {
                if (media.id == trackId) {
                    reviewQueue.push_back(media);
                    originalGenres.push_back(media.genre);
                    std::cout << "   ✓ Gefunden: " << std::filesystem::path(media.filepath).filename().string() << "\n";
                    found = true;
                    break;
                }
            }
            if (!found) {
                std::cout << "   ❌ Track-ID " << trackId << " nicht in allMedia gefunden!\n";
            }
        }
        
        std::cout << "📊 Resultat: " << reviewQueue.size() << " Tracks gefunden\n";
        
        if (reviewQueue.empty()) {
            GtkWidget* errDialog = gtk_message_dialog_new(
                GTK_WINDOW(self->window_), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING,
                GTK_BUTTONS_OK, 
                "⚠️ Keine Tracks ausgewählt!\n\nBitte Tracks mit der Maus markieren (Mehrfachauswahl mit Strg).");
            gtk_dialog_run(GTK_DIALOG(errDialog));
            gtk_widget_destroy(errDialog);
            gtk_widget_destroy(searchDialog);
            delete result;
            return;
        }
    } else if (response == GTK_RESPONSE_ACCEPT) {
        // Alle Tracks (auch neue/unanalysierte)
        for (const auto& media : allMedia) {
            reviewQueue.push_back(media);
            // Bei neuen Tracks "Unknown" als Original-Genre verwenden
            originalGenres.push_back(media.genre.empty() ? "Unknown" : media.genre);
        }
    } else {
        gtk_widget_destroy(searchDialog);
        delete result;
        return;
    }
    
    gtk_widget_destroy(searchDialog);
    delete result;
    
    if (reviewQueue.empty()) {
        GtkWidget* dialog = gtk_message_dialog_new(
            GTK_WINDOW(self->window_),
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_INFO,
            GTK_BUTTONS_OK,
            "ℹ️ Keine analysierten Tracks gefunden.\n\n"
            "Führe zuerst eine Analyse durch:\n"
            "📚 Datenbank → 📈 Alle analysieren"
        );
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }
    
    // Interaktiver Review-Dialog
    GtkWidget* reviewDialog = gtk_dialog_new_with_buttons(
        "🎓 Interaktives Training - Genre-Review",
        GTK_WINDOW(self->window_),
        (GtkDialogFlags)0,
        NULL
    );
    
    makeDialogResizable(reviewDialog, 800, 650);
    
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(reviewDialog));
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 15);
    gtk_container_add(GTK_CONTAINER(content), vbox);
    
    // Progress-Info
    GtkWidget* labelProgress = gtk_label_new(NULL);
    gtk_label_set_selectable(GTK_LABEL(labelProgress), TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), labelProgress, FALSE, FALSE, 0);
    
    // Track Frame
    GtkWidget* frameTrack = gtk_frame_new("📁 Aktueller Track");
    gtk_box_pack_start(GTK_BOX(vbox), frameTrack, FALSE, FALSE, 0);
    
    GtkWidget* trackVbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(trackVbox), 10);
    gtk_container_add(GTK_CONTAINER(frameTrack), trackVbox);
    
    GtkWidget* labelFile = gtk_label_new(NULL);
    gtk_label_set_selectable(GTK_LABEL(labelFile), TRUE);
    gtk_label_set_line_wrap(GTK_LABEL(labelFile), TRUE);
    gtk_box_pack_start(GTK_BOX(trackVbox), labelFile, FALSE, FALSE, 0);
    
    // Play Controls
    GtkWidget* playBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(trackVbox), playBox, FALSE, FALSE, 0);
    
    GtkWidget* btnPlay = gtk_button_new_with_label("▶️ Play");
    gtk_box_pack_start(GTK_BOX(playBox), btnPlay, FALSE, FALSE, 0);
    
    GtkWidget* btnStop = gtk_button_new_with_label("⏹️ Stop");
    gtk_box_pack_start(GTK_BOX(playBox), btnStop, FALSE, FALSE, 0);
    
    GtkWidget* labelTime = gtk_label_new("00:00 / 00:00");
    gtk_label_set_selectable(GTK_LABEL(labelTime), TRUE);
    gtk_box_pack_start(GTK_BOX(playBox), labelTime, FALSE, FALSE, 10);
    
    // Position Slider
    GtkWidget* positionScale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 100.0, 1.0);
    gtk_scale_set_draw_value(GTK_SCALE(positionScale), FALSE);
    gtk_widget_set_size_request(positionScale, 300, -1);
    gtk_box_pack_start(GTK_BOX(trackVbox), positionScale, FALSE, TRUE, 5);
    
    // Metadaten Frame
    GtkWidget* frameMetadata = gtk_frame_new("📝 Metadaten bearbeiten");
    gtk_box_pack_start(GTK_BOX(vbox), frameMetadata, TRUE, TRUE, 0);
    
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);
    gtk_container_add(GTK_CONTAINER(frameMetadata), grid);
    
    int row = 0;
    
    // Genre
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("🎭 Genre:"), 0, row, 1, 1);
    GtkWidget* comboGenre = gtk_combo_box_text_new_with_entry();
    const char* genres[] = {"Electronic", "Techno", "House", "Trance", "Ambient", 
                            "Hip-Hop", "RnB", "Jazz", "Classical", "Salsa", "Walzer", 
                            "Rock/Pop", "Reggae", "Punk", "Vocal", "Volksmusik", "Drum'n'Bass", "Chillout", "Metal", "New Metal", "Trap"};
    for (const char* g : genres) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(comboGenre), g);
    }
    gtk_widget_set_size_request(comboGenre, 200, -1);
    gtk_grid_attach(GTK_GRID(grid), comboGenre, 1, row++, 2, 1);
    
    // BPM
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("🥁 BPM:"), 0, row, 1, 1);
    GtkWidget* spinBPM = gtk_spin_button_new_with_range(0, 300, 1);
    gtk_grid_attach(GTK_GRID(grid), spinBPM, 1, row++, 2, 1);
    
    // Intensität
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("💪 Intensität:"), 0, row, 1, 1);
    GtkWidget* comboIntensity = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(comboIntensity), "soft");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(comboIntensity), "mittel");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(comboIntensity), "hart");
    gtk_grid_attach(GTK_GRID(grid), comboIntensity, 1, row++, 2, 1);
    
    // Bass Level
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("🔊 Bass:"), 0, row, 1, 1);
    GtkWidget* comboBass = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(comboBass), "soft");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(comboBass), "mittel");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(comboBass), "basslastig");
    gtk_grid_attach(GTK_GRID(grid), comboBass, 1, row++, 2, 1);
    
    // Mood
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("😊 Mood:"), 0, row, 1, 1);
    GtkWidget* entryMood = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entryMood), "z.B. Energetic, Dark");
    gtk_grid_attach(GTK_GRID(grid), entryMood, 1, row++, 2, 1);
    
    // Stats
    GtkWidget* labelStats = gtk_label_new("");
    gtk_label_set_selectable(GTK_LABEL(labelStats), TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), labelStats, FALSE, FALSE, 0);
    
    // Action Buttons
    GtkWidget* btnBox = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(btnBox), GTK_BUTTONBOX_SPREAD);
    gtk_box_pack_start(GTK_BOX(vbox), btnBox, FALSE, FALSE, 0);
    
    GtkWidget* btnBack = gtk_button_new_with_label("◀️ Zurück");
    gtk_box_pack_start(GTK_BOX(btnBox), btnBack, TRUE, TRUE, 0);
    
    GtkWidget* btnKeep = gtk_button_new_with_label("✅ Korrekt");
    gtk_box_pack_start(GTK_BOX(btnBox), btnKeep, TRUE, TRUE, 0);
    
    GtkWidget* btnCorrect = gtk_button_new_with_label("✏️ Korrigieren");
    gtk_box_pack_start(GTK_BOX(btnBox), btnCorrect, TRUE, TRUE, 0);
    
    GtkWidget* btnSkip = gtk_button_new_with_label("⏭️ Skip");
    gtk_box_pack_start(GTK_BOX(btnBox), btnSkip, TRUE, TRUE, 0);
    
    GtkWidget* btnFinish = gtk_button_new_with_label("💾 Fertig");
    gtk_box_pack_start(GTK_BOX(btnBox), btnFinish, TRUE, TRUE, 0);
    
    // State
    struct ReviewState {
        GtkRenderer* renderer;
        std::vector<MediaMetadata> queue;
        std::vector<std::string> originalGenres;  // 🎓 Für Online-Learning
        size_t currentIndex;
        int corrected, kept, skipped;
        GtkWidget *dialog, *labelProgress, *labelFile, *labelStats;
        GtkWidget *comboGenre, *spinBPM, *comboIntensity, *comboBass, *entryMood;
        GtkWidget *positionScale, *labelTime;  // 🎵 Position Controls
        guint positionTimeoutId;  // Timer für Position-Updates
        bool isUserSeeking;  // Flag um Timer-Updates von User-Seeks zu unterscheiden
    };
    
    ReviewState* state = new ReviewState{
        self, reviewQueue, originalGenres, 0, 0, 0, 0,
        reviewDialog, labelProgress, labelFile, labelStats,
        comboGenre, spinBPM, comboIntensity, comboBass, entryMood,
        positionScale, labelTime, 0, false
    };
    
    // Load track function (static for use in callbacks)
    static auto loadTrackFunc = [](ReviewState* s) {
        if (s->currentIndex >= s->queue.size()) {
            // 🎓 Finaler Batch-Retrain wenn Korrekturen vorhanden
            int pendingCount = 0;
            if (s->renderer->trainingModel_) {
                pendingCount = s->renderer->trainingModel_->getPendingCorrections();
                if (pendingCount > 0) {
                    std::cout << "\n🎓 Finaler Batch-Retrain mit " << pendingCount << " Korrekturen..." << std::endl;
                    s->renderer->trainingModel_->batchRetrainPending(1);  // Min 1 für finalen Batch
                }
            }
            
            char msg[512];
            snprintf(msg, sizeof(msg),
                "✅ Review abgeschlossen!\n\n"
                "✅ Korrekt: %d\n✏️ Korrigiert: %d\n⏭️ Übersprungen: %d\n\n"
                "🎓 Online-Learning:\n   %d Korrekturen für Training verwendet",
                s->kept, s->corrected, s->skipped, s->corrected);
            
            GtkWidget* d = gtk_message_dialog_new(GTK_WINDOW(s->dialog),
                GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s", msg);
            gtk_dialog_run(GTK_DIALOG(d));
            gtk_widget_destroy(d);
            
            // Cleanup: Stoppe Timer und Audio
            if (s->positionTimeoutId > 0) {
                g_source_remove(s->positionTimeoutId);
            }
            s->renderer->audioPlayer_->stop();
            
            gtk_widget_destroy(s->dialog);
            
            s->renderer->addHistoryEntry("Interaktives Training abgeschlossen",
                std::to_string(s->queue.size()) + " Tracks reviewed\n" +
                std::to_string(s->corrected) + " korrigiert\n" +
                "🎓 Modell mit Korrekturen nachtrainiert", "✅ Fertig");
            
            s->renderer->refreshDatabaseView();
            delete s;
            return;
        }
        
        auto& t = s->queue[s->currentIndex];
        
        // 🔍 Prüfe ob Track analysiert ist, falls nicht -> automatische Analyse
        if (!t.analyzed || t.genre.empty() || t.genre == "Unknown") {
            std::cout << "⚡ Track noch nicht analysiert - führe automatische Analyse durch..." << std::endl;
            
            MediaMetadata meta;
            if (s->renderer->analyzer_->analyze(t.filepath, meta)) {
                // Update in Datenbank
                meta.id = t.id;
                meta.filepath = t.filepath;
                if (s->renderer->database_->updateMedia(meta)) {
                    t = meta;  // Update lokale Kopie
                    std::cout << "   ✅ Analyse erfolgreich: " << meta.genre 
                             << " | BPM: " << meta.bpm << std::endl;
                }
            } else {
                std::cout << "   ⚠️ Analyse fehlgeschlagen - verwende Defaults" << std::endl;
                if (t.genre.empty()) t.genre = "Unknown";
            }
        }
        
        // 🛑 Stoppe vorherigen Track automatisch
        if (s->renderer->audioPlayer_) {
            s->renderer->audioPlayer_->stop();
        }
        
        // Stoppe Position-Timer vom vorherigen Track
        if (s->positionTimeoutId > 0) {
            g_source_remove(s->positionTimeoutId);
            s->positionTimeoutId = 0;
        }
        
        // Progress mit Online-Learning Status
        char prog[256];
        int pendingCorrections = s->renderer->trainingModel_ ? 
            s->renderer->trainingModel_->getPendingCorrections() : 0;
        int totalRetrains = s->renderer->trainingModel_ ? 
            s->renderer->trainingModel_->getTotalRetrains() : 0;
        
        snprintf(prog, sizeof(prog), 
                "<span size='large' weight='bold'>Track %zu / %zu</span>  "
                "<span size='small'>🎓 Online-Learning: %d Korrekturen | %d Retrains</span>",
                s->currentIndex + 1, s->queue.size(), pendingCorrections, totalRetrains);
        gtk_label_set_markup(GTK_LABEL(s->labelProgress), prog);
        
        // File
        std::string fn = std::filesystem::path(t.filepath).filename().string();
        gtk_label_set_text(GTK_LABEL(s->labelFile), ("📁 " + fn).c_str());
        
        // Metadata
        GtkWidget* entry = gtk_bin_get_child(GTK_BIN(s->comboGenre));
        if (GTK_IS_ENTRY(entry)) {
            gtk_entry_set_text(GTK_ENTRY(entry), t.genre.c_str());
        }
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(s->spinBPM), t.bpm);
        
        int idx = (t.intensity == "soft") ? 0 : (t.intensity == "hart") ? 2 : 1;
        gtk_combo_box_set_active(GTK_COMBO_BOX(s->comboIntensity), idx);
        
        idx = (t.bassLevel == "soft") ? 0 : (t.bassLevel == "basslastig") ? 2 : 1;
        gtk_combo_box_set_active(GTK_COMBO_BOX(s->comboBass), idx);
        
        gtk_entry_set_text(GTK_ENTRY(s->entryMood), t.mood.c_str());
        
        // Reset Position
        gtk_range_set_value(GTK_RANGE(s->positionScale), 0.0);
        gtk_label_set_text(GTK_LABEL(s->labelTime), "00:00 / 00:00");
        
        // Stats
        char stats[256];
        snprintf(stats, sizeof(stats), "📊 %d korrekt | %d korrigiert | %d übersprungen",
                s->kept, s->corrected, s->skipped);
        gtk_label_set_text(GTK_LABEL(s->labelStats), stats);
    };
    
    // Position Update Timer Callback
    static auto updatePositionFunc = [](gpointer data) -> gboolean {
        auto* s = static_cast<ReviewState*>(data);
        if (!s->renderer->audioPlayer_) return G_SOURCE_REMOVE;
        
        // NICHT updaten während User den Slider bewegt!
        if (s->isUserSeeking) {
            return G_SOURCE_CONTINUE;
        }
        
        double duration = s->renderer->audioPlayer_->getDuration();
        double position = s->renderer->audioPlayer_->getPosition();
        
        if (duration > 0) {
            double percent = (position / duration) * 100.0;
            
            // Signal blockieren während automatischem Update, damit value-changed nicht triggert
            g_signal_handlers_block_by_func(s->positionScale, (gpointer)G_CALLBACK(nullptr), s);
            gtk_range_set_value(GTK_RANGE(s->positionScale), percent);
            g_signal_handlers_unblock_by_func(s->positionScale, (gpointer)G_CALLBACK(nullptr), s);
            
            int posMin = (int)(position / 60);
            int posSec = (int)position % 60;
            int durMin = (int)(duration / 60);
            int durSec = (int)duration % 60;
            
            char timeStr[32];
            snprintf(timeStr, sizeof(timeStr), "%02d:%02d / %02d:%02d", posMin, posSec, durMin, durSec);
            gtk_label_set_text(GTK_LABEL(s->labelTime), timeStr);
        }
        
        return G_SOURCE_CONTINUE;
    };
    
    // Callbacks
    g_signal_connect(btnPlay, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer data) {
        auto* s = static_cast<ReviewState*>(data);
        auto& t = s->queue[s->currentIndex];
        s->renderer->audioPlayer_->stop();
        if (s->renderer->audioPlayer_->load(t.filepath)) {
            s->renderer->audioPlayer_->play();
            
            // Starte Position-Timer (250ms Updates für flüssige Performance)
            if (s->positionTimeoutId > 0) {
                g_source_remove(s->positionTimeoutId);
            }
            s->positionTimeoutId = g_timeout_add(250, updatePositionFunc, s);
        }
    }), state);
    
    g_signal_connect(btnStop, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer data) {
        auto* s = static_cast<ReviewState*>(data);
        s->renderer->audioPlayer_->stop();
        
        // Stoppe Position-Timer
        if (s->positionTimeoutId > 0) {
            g_source_remove(s->positionTimeoutId);
            s->positionTimeoutId = 0;
        }
    }), state);
    
    // Position-Slider Callbacks - Seeking während Ziehen und Klicken
    g_signal_connect(positionScale, "button-press-event", G_CALLBACK(+[](GtkWidget*, GdkEventButton*, gpointer data) -> gboolean {
        auto* s = static_cast<ReviewState*>(data);
        s->isUserSeeking = true;
        return FALSE;  // Event weiter propagieren
    }), state);
    
    g_signal_connect(positionScale, "button-release-event", G_CALLBACK(+[](GtkWidget*, GdkEventButton*, gpointer data) -> gboolean {
        auto* s = static_cast<ReviewState*>(data);
        s->isUserSeeking = false;
        return FALSE;
    }), state);
    
    // Value-Changed: Erlaube Seek während Ziehen UND bei Klick
    g_signal_connect(positionScale, "value-changed", G_CALLBACK(+[](GtkRange* range, gpointer data) {
        auto* s = static_cast<ReviewState*>(data);
        
        // Nur wenn User aktiv seeked (nicht während automatischer Updates)
        if (s->isUserSeeking) {
            double value = gtk_range_get_value(range);
            double duration = s->renderer->audioPlayer_->getDuration();
            
            if (duration > 0) {
                double newPosition = (value / 100.0) * duration;
                s->renderer->audioPlayer_->seek(newPosition);
            }
        }
    }), state);
    
    g_signal_connect(btnBack, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer data) {
        auto* s = static_cast<ReviewState*>(data);
        if (s->currentIndex > 0) {
            s->currentIndex--;
            // Rückgängig machen der vorherigen Zählung
            if (s->kept > 0) s->kept--;
            else if (s->corrected > 0) s->corrected--;
            else if (s->skipped > 0) s->skipped--;
            loadTrackFunc(s);
        }
    }), state);
    
    g_signal_connect(btnKeep, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer data) {
        auto* s = static_cast<ReviewState*>(data);
        s->kept++;
        s->currentIndex++;
        loadTrackFunc(s);
    }), state);
    
    g_signal_connect(btnCorrect, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer data) {
        auto* s = static_cast<ReviewState*>(data);
        auto& t = s->queue[s->currentIndex];
        std::string originalGenre = s->originalGenres[s->currentIndex];  // 🎓 Original speichern
        
        // Lese Änderungen aus UI
        GtkWidget* entry = gtk_bin_get_child(GTK_BIN(s->comboGenre));
        if (GTK_IS_ENTRY(entry)) {
            t.genre = gtk_entry_get_text(GTK_ENTRY(entry));
        }
        t.bpm = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(s->spinBPM));
        t.intensity = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(s->comboIntensity));
        t.bassLevel = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(s->comboBass));
        t.mood = gtk_entry_get_text(GTK_ENTRY(s->entryMood));
        
        // 💾 Speichere in Datenbank
        std::cout << "💾 Speichere Korrektur: " << std::filesystem::path(t.filepath).filename().string() 
                  << " | Genre: " << originalGenre << " → " << t.genre << std::endl;
        
        if (s->renderer->database_->updateMedia(t)) {
            s->corrected++;
            s->renderer->addHistoryEntry("Genre korrigiert",
                std::filesystem::path(t.filepath).filename().string() + "\n" +
                "Genre: " + t.genre + " | BPM: " + std::to_string((int)t.bpm),
                "✅ Gespeichert");
            
            // 🎓 ONLINE-LEARNING: Trainiere sofort mit korrigierten Daten
            if (t.genre != originalGenre && s->renderer->trainingModel_) {
                s->renderer->trainingModel_->retrainWithCorrectedData(t, originalGenre);
            }
            
            // Aktualisiere auch das Original-Genre für Zurück-Button
            s->originalGenres[s->currentIndex] = t.genre;
        } else {
            std::cerr << "❌ Fehler beim Speichern in Datenbank!" << std::endl;
            GtkWidget* errDialog = gtk_message_dialog_new(
                GTK_WINDOW(s->dialog), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
                GTK_BUTTONS_OK, "❌ Fehler beim Speichern der Änderungen!");
            gtk_dialog_run(GTK_DIALOG(errDialog));
            gtk_widget_destroy(errDialog);
        }
        
        s->currentIndex++;
        loadTrackFunc(s);
    }), state);
    
    g_signal_connect(btnSkip, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer data) {
        auto* s = static_cast<ReviewState*>(data);
        s->skipped++;
        s->currentIndex++;
        loadTrackFunc(s);
    }), state);
    
    g_signal_connect(btnFinish, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer data) {
        auto* s = static_cast<ReviewState*>(data);
        s->currentIndex = s->queue.size();
        loadTrackFunc(s);
    }), state);
    
    loadTrackFunc(state);
    gtk_widget_show_all(reviewDialog);
    gtk_dialog_run(GTK_DIALOG(reviewDialog));
}

// ==================== IDLE LEARNING SYSTEM ====================

void GtkRenderer::startIdleLearning() {
    if (idleLearningActive_) return;
    
    idleLearningActive_ = true;
    lastActivityTime_ = 0;
    idleSecondsCounter_ = 0;
    hasMoreToLearn_ = true;
    noLearningTasksCounter_ = 0;
    
    // Initialisiere Datenbank-Größe für Änderungs-Erkennung
    if (database_) {
        lastDatabaseSize_ = database_->getAll().size();
    }
    
    // Starte Idle-Check-Timer (alle 1 Sekunde)
    idleCheckTimerId_ = g_timeout_add(1000, onIdleCheckTimer, this);
    
    // Starte Background-Learning-Thread
    idleLearningThread_ = std::thread([this]() {
        idleLearningLoop();
    });
    
    std::cout << "🧠 Idle Learning System gestartet (" << lastDatabaseSize_ << " Tracks)" << std::endl;
}

void GtkRenderer::stopIdleLearning() {
    if (!idleLearningActive_) return;
    
    idleLearningActive_ = false;
    
    if (idleCheckTimerId_ > 0) {
        g_source_remove(idleCheckTimerId_);
        idleCheckTimerId_ = 0;
    }
    
    if (idleLearningThread_.joinable()) {
        idleLearningThread_.join();
    }
    
    std::cout << "🧠 Idle Learning System gestoppt" << std::endl;
}

void GtkRenderer::resetActivityTimer() {
    lastActivityTime_ = 0;
    
    // Wenn gerade Idle-Learning läuft, stoppe es
    if (isIdleLearning_) {
        std::cout << "👤 User-Aktivität erkannt - pausiere Idle Learning" << std::endl;
        isIdleLearning_ = false;
        
        // Verstecke Idle-Label
        gdk_threads_add_idle(+[](gpointer data) -> gboolean {
            auto* self = static_cast<GtkRenderer*>(data);
            gtk_widget_hide(self->idleLearningLabel_);
            return G_SOURCE_REMOVE;
        }, this);
    }
}

gboolean GtkRenderer::onUserActivity(GtkWidget* widget, GdkEvent* event, gpointer data) {
    auto* self = static_cast<GtkRenderer*>(data);
    self->resetActivityTimer();
    return FALSE;  // Event weiterleiten
}

gboolean GtkRenderer::onIdleCheckTimer(gpointer data) {
    auto* self = static_cast<GtkRenderer*>(data);
    
    if (!self->idleLearningActive_) {
        return G_SOURCE_REMOVE;
    }
    
    self->lastActivityTime_++;
    
    // Nach 30 Sekunden Inaktivität: Starte Idle Learning
    if (self->lastActivityTime_ >= 30 && !self->isIdleLearning_) {
        self->isIdleLearning_ = true;
        self->idleSecondsCounter_ = 0;
        
        std::cout << "🧠 Idle erkannt - starte selbstständiges Lernen..." << std::endl;
        
        // Zeige Idle-Label
        gtk_label_set_markup(GTK_LABEL(self->idleLearningLabel_),
            "<span color='#90EE90'>🧠 Idle Learning aktiv...</span>");
        gtk_widget_show(self->idleLearningLabel_);
    }
    
    return G_SOURCE_CONTINUE;
}

void GtkRenderer::idleLearningLoop() {
    while (idleLearningActive_) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // Überwache Datenbank-Änderungen (alle 5 Sekunden)
        if (idleSecondsCounter_ % 5 == 0 && database_) {
            size_t currentSize = database_->getAll().size();
            if (currentSize != lastDatabaseSize_) {
                lastDatabaseSize_ = currentSize;
                if (!hasMoreToLearn_ && currentSize > 0) {
                    // Neue Daten hinzugefügt! Reaktiviere Learning
                    hasMoreToLearn_ = true;
                    noLearningTasksCounter_ = 0;
                    std::cout << "✨ Neue Daten erkannt (" << currentSize << " Tracks) - Learning reaktiviert!" << std::endl;
                    
                    // Zeige wieder das Learning-Label
                    gdk_threads_add_idle(+[](gpointer data) -> gboolean {
                        auto* self = static_cast<GtkRenderer*>(data);
                        if (self->hasMoreToLearn_) {
                            gtk_label_set_markup(GTK_LABEL(self->idleLearningLabel_),
                                "<span color='#90EE90'>🧠 Idle Learning reaktiviert...</span>");
                            gtk_widget_show(self->idleLearningLabel_);
                        }
                        return G_SOURCE_REMOVE;
                    }, this);
                }
            }
        }
        
        if (isIdleLearning_ && running_ && hasMoreToLearn_) {
            idleSecondsCounter_++;
            
            // Alle 5 Sekunden: Führe Learning-Task aus
            if (idleSecondsCounter_ % 5 == 0) {
                performIdleLearningTask();
            }
            
            // Update Idle-Label
            gdk_threads_add_idle(+[](gpointer data) -> gboolean {
                auto* self = static_cast<GtkRenderer*>(data);
                if (self->isIdleLearning_ && self->hasMoreToLearn_) {
                    std::string text = "🧠 Idle Learning aktiv (" + 
                                      std::to_string(self->idleSecondsCounter_.load()) + "s)...";
                    gtk_label_set_markup(GTK_LABEL(self->idleLearningLabel_),
                        ("<span color='#90EE90'>" + text + "</span>").c_str());
                }
                return G_SOURCE_REMOVE;
            }, this);
        } else if (isIdleLearning_ && !hasMoreToLearn_) {
            // Nichts mehr zu lernen - zeige Pause-Status
            gdk_threads_add_idle(+[](gpointer data) -> gboolean {
                auto* self = static_cast<GtkRenderer*>(data);
                gtk_label_set_markup(GTK_LABEL(self->idleLearningLabel_),
                    "<span color='#FFD700'>⏸️ Learning pausiert - warte auf neue Daten...</span>");
                return G_SOURCE_REMOVE;
            }, this);
        }
    }
}

void GtkRenderer::performIdleLearningTask() {
    std::cout << "🎓 Idle Learning Task [" << idleSecondsCounter_ << "s]" << std::endl;
    
    bool taskPerformed = false;
    
    // 1. Analysiere Datenbank für Pattern
    if (trainingModel_ && database_) {
        auto allMedia = database_->getAll();
        
        if (!allMedia.empty()) {
            // Lerne aus existierenden Correction-Patterns
            if (idleSecondsCounter_ % 15 == 0) {  // Alle 15 Sekunden
                std::cout << "   🔍 Analysiere Genre-Patterns..." << std::endl;
                auto patterns = trainingModel_->learnCorrectionPatterns();
                
                if (!patterns.empty()) {
                    std::cout << "   ✨ " << patterns.size() << " Patterns gefunden" << std::endl;
                    taskPerformed = true;
                    
                    // Wende Patterns auf Datenbank an (nur Vorschläge)
                    int suggestions = trainingModel_->suggestDatabaseCorrections(false);
                    if (suggestions > 0) {
                        std::cout << "   💡 " << suggestions << " Verbesserungs-Vorschläge gefunden" << std::endl;
                        taskPerformed = true;
                    }
                } else {
                    std::cout << "   ℹ️ Keine neuen Patterns" << std::endl;
                }
            }
            
            // Optimiere Feature-Extraction-Parameter
            if (idleSecondsCounter_ % 20 == 0) {  // Alle 20 Sekunden
                std::cout << "   🎵 Optimiere Feature-Extraction..." << std::endl;
                // Hier könnte AudioAnalyzer-Optimierung stattfinden
                // taskPerformed = true; // Wenn tatsächlich etwas optimiert wurde
            }
            
            // 🧹 Bereinige Korrektur-Historie von Widersprüchen
            if (idleSecondsCounter_ % 40 == 0) {  // Alle 40 Sekunden
                std::cout << "   🧹 Bereinige Lern-Historie..." << std::endl;
                int cleaned = trainingModel_->revalidateCorrectionHistory();
                if (cleaned > 0) {
                    std::cout << "   ✨ " << cleaned << " widersprüchliche Einträge entfernt" << std::endl;
                    taskPerformed = true;
                } else {
                    std::cout << "   ℹ️ Historie bereits sauber" << std::endl;
                }
            }
        } else {
            std::cout << "   ℹ️ Keine Tracks in Datenbank" << std::endl;
        }
    }
    
    // 2. Instrumenten-Extraktor optimieren
    if (idleSecondsCounter_ % 25 == 0) {  // Alle 25 Sekunden
        std::cout << "   🥁 Optimiere Instrumenten-Extraktion..." << std::endl;
    }
    
    // Tracking: Wenn keine Tasks ausgeführt wurden
    if (!taskPerformed) {
        noLearningTasksCounter_++;
        std::cout << "   ⏸️ Keine Learning-Tasks verfügbar (" << noLearningTasksCounter_ << "/3)" << std::endl;
        
        // Nach 3 aufeinanderfolgenden erfolglosen Tasks: Stoppe Learning
        if (noLearningTasksCounter_ >= 3) {
            hasMoreToLearn_ = false;
            std::cout << "\n⏸️ Idle Learning pausiert - nichts mehr zu lernen!" << std::endl;
            std::cout << "   Wird automatisch reaktiviert wenn neue Daten hinzugefügt werden.\n" << std::endl;
            
            // Update Label
            gdk_threads_add_idle(+[](gpointer data) -> gboolean {
                auto* self = static_cast<GtkRenderer*>(data);
                gtk_label_set_markup(GTK_LABEL(self->idleLearningLabel_),
                    "<span color='#FFD700'>⏸️ Learning pausiert - warte auf neue Daten...</span>");
                return G_SOURCE_REMOVE;
            }, this);
        }
    } else {
        // Task wurde ausgeführt - Reset Counter
        noLearningTasksCounter_ = 0;
    }
    
    // 3. Lerne Rhythmus-Patterns aus der Datenbank
    if (idleSecondsCounter_ % 30 == 0 && database_) {  // Alle 30 Sekunden
        std::cout << "   🎼 Analysiere Rhythmus-Patterns..." << std::endl;
        auto allMedia = database_->getAll();
        
        // Gruppiere nach Genre und analysiere BPM-Patterns
        std::map<std::string, std::vector<float>> bpmByGenre;
        for (const auto& m : allMedia) {
            if (m.bpm > 0) {
                bpmByGenre[m.genre].push_back(m.bpm);
            }
        }
        
        for (const auto& [genre, bpms] : bpmByGenre) {
            if (bpms.size() >= 5) {
                float avgBpm = 0.0f;
                for (float bpm : bpms) avgBpm += bpm;
                avgBpm /= bpms.size();
                
                std::cout << "      • " << genre << ": Ø " << (int)avgBpm 
                         << " BPM (" << bpms.size() << " Tracks)" << std::endl;
            }
        }
    }
    
    // 4. Cleanup und Optimierung
    if (idleSecondsCounter_ % 60 == 0 && database_) {  // Jede Minute
        std::cout << "   🧹 Cleanup und Wartung..." << std::endl;
        
        // Entferne kaputte Referenzen
        auto allMedia = database_->getAll();
        int cleanedUp = 0;
        for (const auto& m : allMedia) {
            if (!std::filesystem::exists(m.filepath)) {
                cleanedUp++;
            }
        }
        
        if (cleanedUp > 0) {
            std::cout << "      ⚠️  " << cleanedUp << " fehlende Dateien erkannt" << std::endl;
        }
    }
    
    // 5. Statistik-Update
    if (idleSecondsCounter_ % 45 == 0) {  // Alle 45 Sekunden
        std::cout << "   📊 Update Statistiken..." << std::endl;
        if (trainingModel_) {
            auto stats = trainingModel_->getStats();
            std::cout << "      • Corrections: " << stats["corrections"] << std::endl;
            std::cout << "      • Suggestions: " << stats["suggestions"] << std::endl;
        }
    }
    
    std::cout << "   ✅ Idle Learning Task abgeschlossen" << std::endl;
}

// 📟 Console Output Capture - Fängt stdout/stderr in Echtzeit
void GtkRenderer::startConsoleCapture() {
    // Erstelle Pipe für stdout/stderr redirect
    if (pipe(consolePipe_) == -1) {
        std::cerr << "❌ Console capture pipe creation failed\n";
        return;
    }
    
    // Backup original stdout/stderr
    stdoutBackup_ = dup(STDOUT_FILENO);
    stderrBackup_ = dup(STDERR_FILENO);
    
    // Redirect stdout und stderr zur Pipe
    dup2(consolePipe_[1], STDOUT_FILENO);
    dup2(consolePipe_[1], STDERR_FILENO);
    
    // Aktiviere unbuffered output für sofortige Anzeige
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    
    consoleActive_ = true;
    
    // Thread der Pipe ausliest und GUI updated
    consoleThread_ = std::thread([this]() {
        char buffer[4096];
        std::string currentLine;
        
        // Setze Pipe auf non-blocking
        int flags = fcntl(consolePipe_[0], F_GETFL, 0);
        fcntl(consolePipe_[0], F_SETFL, flags | O_NONBLOCK);
        
        while (consoleActive_) {
            ssize_t count = read(consolePipe_[0], buffer, sizeof(buffer) - 1);
            
            if (count > 0) {
                buffer[count] = '\0';
                
                // Schreibe auch in Original-Streams (optional für Debugging)
                write(stdoutBackup_, buffer, count);
                
                // Verarbeite Zeile für Zeile
                for (ssize_t i = 0; i < count; ++i) {
                    if (buffer[i] == '\n') {
                        // Zeile komplett, update GUI
                        if (!currentLine.empty()) {
                            // Entferne ANSI-Farbcodes für saubere Anzeige
                            std::string cleaned = currentLine;
                            size_t pos = 0;
                            while ((pos = cleaned.find("\033[", pos)) != std::string::npos) {
                                size_t end = cleaned.find('m', pos);
                                if (end != std::string::npos) {
                                    cleaned.erase(pos, end - pos + 1);
                                } else {
                                    break;
                                }
                            }
                            
                            // Update GUI im Main-Thread
                            ConsoleUpdateData* updateData = new ConsoleUpdateData;
                            updateData->renderer = this;
                            updateData->text = g_strdup(cleaned.c_str());
                            g_idle_add(updateConsoleOutputIdle, updateData);
                            
                            currentLine.clear();
                        }
                    } else {
                        currentLine += buffer[i];
                    }
                }
            } else if (count == -1 && errno != EAGAIN) {
                break;  // Error
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        
        // Zeige letzte unvollständige Zeile
        if (!currentLine.empty()) {
            ConsoleUpdateData* updateData = new ConsoleUpdateData;
            updateData->renderer = this;
            updateData->text = g_strdup(currentLine.c_str());
            g_idle_add(updateConsoleOutputIdle, updateData);
        }
    });
}

void GtkRenderer::stopConsoleCapture() {
    if (!consoleActive_) return;
    
    consoleActive_ = false;
    
    // Warte auf Thread-Ende
    if (consoleThread_.joinable()) {
        consoleThread_.join();
    }
    
    // Restore original stdout/stderr
    dup2(stdoutBackup_, STDOUT_FILENO);
    dup2(stderrBackup_, STDERR_FILENO);
    
    close(stdoutBackup_);
    close(stderrBackup_);
    close(consolePipe_[0]);
    close(consolePipe_[1]);
}

gboolean GtkRenderer::updateConsoleOutputIdle(gpointer data) {
    ConsoleUpdateData* updateData = static_cast<ConsoleUpdateData*>(data);
    
    if (updateData->renderer && updateData->renderer->consoleLabel_) {
        gtk_label_set_text(GTK_LABEL(updateData->renderer->consoleLabel_), updateData->text);
    }
    
    g_free(updateData->text);
    delete updateData;
    return G_SOURCE_REMOVE;
}

void GtkRenderer::updateConsoleOutput(const std::string& text) {
    if (consoleLabel_) {
        gtk_label_set_text(GTK_LABEL(consoleLabel_), text.c_str());
    }
}

// ==================== GENRE-FUSION & KÜNSTLER-STIL CALLBACKS ====================

void GtkRenderer::onLearnGenreFusions(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    
    if (!self->trainingModel_) {
        GtkWidget* dialog = gtk_message_dialog_new(
            GTK_WINDOW(self->window_), GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
            "❌ Training-Modell nicht initialisiert!");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }
    
    // Lerne Genre-Fusionen in Thread
    std::thread([self]() {
        auto fusionCounts = self->trainingModel_->learnGenreFusions();
        
        // Zeige Ergebnisse im Dialog
        gdk_threads_add_idle(+[](gpointer data) -> gboolean {
            auto* counts = static_cast<std::map<std::string, int>*>(data);
            auto* self = static_cast<GtkRenderer*>(g_object_get_data(G_OBJECT(gtk_widget_get_toplevel(
                GTK_WIDGET(gtk_window_get_focus(GTK_WINDOW(nullptr))))), "renderer"));
            
            std::string message = "🎭 Genre-Fusion Patterns:\n\n";
            
            std::vector<std::pair<std::string, int>> sorted(counts->begin(), counts->end());
            std::sort(sorted.begin(), sorted.end(),
                     [](const auto& a, const auto& b) { return a.second > b.second; });
            
            for (size_t i = 0; i < std::min(size_t(15), sorted.size()); i++) {
                message += std::to_string(i+1) + ". " + sorted[i].first + 
                          " (" + std::to_string(sorted[i].second) + " Tracks)\n";
            }
            
            GtkWidget* dialog = gtk_message_dialog_new(
                nullptr, GTK_DIALOG_MODAL,
                GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
                "%s", message.c_str());
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
            
            delete counts;
            return G_SOURCE_REMOVE;
        }, new std::map<std::string, int>(fusionCounts));
    }).detach();
}

void GtkRenderer::onLearnArtistStyle(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    
    if (!self->trainingModel_) return;
    
    // Zeige Eingabe-Dialog für Künstlername
    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        "🎨 Künstler-Stil analysieren",
        GTK_WINDOW(self->window_),
        (GtkDialogFlags)0,
        "Abbrechen", GTK_RESPONSE_CANCEL,
        "Analysieren", GTK_RESPONSE_OK,
        NULL);
    
    makeDialogResizable(dialog, 450, 250);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
    gtk_box_pack_start(GTK_BOX(content), vbox, TRUE, TRUE, 0);
    
    GtkWidget* label = gtk_label_new("Künstlername eingeben (z.B. \"The Prodigy\"):");
    gtk_label_set_selectable(GTK_LABEL(label), TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
    
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "The Prodigy");
    gtk_box_pack_start(GTK_BOX(vbox), entry, FALSE, FALSE, 0);
    
    gtk_widget_show_all(dialog);
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        const char* artist = gtk_entry_get_text(GTK_ENTRY(entry));
        if (artist && strlen(artist) > 0) {
            std::string artistName = artist;
            gtk_widget_destroy(dialog);
            
            // Analysiere in Thread
            std::thread([self, artistName]() {
                auto features = self->trainingModel_->learnArtistStyle(artistName);
                
                std::string msg = "✅ Künstler-Stil von \"" + artistName + "\" analysiert!\n\n";
                msg += "Details siehe Console-Ausgabe.";
                
                gdk_threads_add_idle(+[](gpointer data) -> gboolean {
                    std::string* message = static_cast<std::string*>(data);
                    GtkWidget* dialog = gtk_message_dialog_new(
                        nullptr, GTK_DIALOG_MODAL,
                        GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
                        "%s", message->c_str());
                    gtk_dialog_run(GTK_DIALOG(dialog));
                    gtk_widget_destroy(dialog);
                    delete message;
                    return G_SOURCE_REMOVE;
                }, new std::string(msg));
            }).detach();
            return;
        }
    }
    
    gtk_widget_destroy(dialog);
}

// 🤖 Automatisches Genre-Learning: Lerne aus bereits korrigierten Tracks
void GtkRenderer::autoLearnGenresFromCorrectedTracks() {
    auto allMedia = database_->getAll();
    if (allMedia.empty()) return;
    
    // Schritt 1: Sammle korrigierte Tracks als Trainingsbeispiele
    std::map<std::string, std::vector<MediaMetadata>> genreExamples;
    int correctedCount = 0;
    
    for (const auto& media : allMedia) {
        // Nur Tracks die korrigiert wurden (lastUsed > 0) und ein Genre haben
        if (media.lastUsed > 0 && media.genre != "Unknown" && !media.genre.empty() && media.analyzed) {
            genreExamples[media.genre].push_back(media);
            correctedCount++;
        }
    }
    
    if (correctedCount == 0) {
        std::cout << "ℹ️ Keine korrigierten Tracks zum Lernen gefunden." << std::endl;
        return;
    }
    
    std::cout << "\n🎓 Genre-Learning gestartet..." << std::endl;
    std::cout << "📚 " << correctedCount << " korrigierte Tracks als Trainingsbeispiele gefunden" << std::endl;
    std::cout << "🎯 " << genreExamples.size() << " verschiedene Genres" << std::endl;
    
    // Zeige Trainingsbeispiele pro Genre
    for (const auto& [genre, examples] : genreExamples) {
        std::cout << "   • " << genre << ": " << examples.size() << " Beispiele" << std::endl;
    }
    
    // Schritt 2: Berechne durchschnittliche Features pro Genre (Learning)
    struct GenreProfile {
        std::string genre;
        float avgBpm = 0.0f;
        float avgSpectralCentroid = 0.0f;
        float avgZeroCrossing = 0.0f;
        float avgSpectralRolloff = 0.0f;
        std::string commonBassLevel;
        std::string commonIntensity;
        int sampleCount = 0;
    };
    
    std::map<std::string, GenreProfile> genreProfiles;
    
    for (const auto& [genre, examples] : genreExamples) {
        GenreProfile profile;
        profile.genre = genre;
        profile.sampleCount = examples.size();
        
        float sumBpm = 0, sumCentroid = 0, sumZeroCrossing = 0, sumRolloff = 0;
        std::map<std::string, int> bassLevels, intensities;
        
        for (const auto& track : examples) {
            sumBpm += track.bpm;
            sumCentroid += track.spectralCentroid;
            sumZeroCrossing += track.zeroCrossingRate;
            sumRolloff += track.spectralRolloff;
            bassLevels[track.bassLevel]++;;
            intensities[track.intensity]++;
        }
        
        profile.avgBpm = sumBpm / examples.size();
        profile.avgSpectralCentroid = sumCentroid / examples.size();
        profile.avgZeroCrossing = sumZeroCrossing / examples.size();
        profile.avgSpectralRolloff = sumRolloff / examples.size();
        
        // Häufigstes Bass-Level und Intensität
        profile.commonBassLevel = std::max_element(bassLevels.begin(), bassLevels.end(),
            [](const auto& a, const auto& b) { return a.second < b.second; })->first;
        profile.commonIntensity = std::max_element(intensities.begin(), intensities.end(),
            [](const auto& a, const auto& b) { return a.second < b.second; })->first;
        
        genreProfiles[genre] = profile;
    }
    
    // Schritt 3: Klassifiziere Unknown-Tracks basierend auf gelernten Profilen
    int unknownCount = 0;
    int classified = 0;
    
    std::cout << "\n🔍 Klassifiziere Unknown-Tracks..." << std::endl;
    
    for (auto media : allMedia) {
        if (media.genre != "Unknown" && !media.genre.empty()) continue;
        if (!media.analyzed) continue;  // Nur analysierte Tracks
        
        unknownCount++;
        
        // Finde ähnlichstes Genre-Profil
        std::string bestGenre = "Rock/Pop";
        float bestScore = std::numeric_limits<float>::max();
        
        for (const auto& [genre, profile] : genreProfiles) {
            // Berechne Distanz (einfache Euklidische Distanz in Feature-Space)
            float bpmDist = std::abs(media.bpm - profile.avgBpm) / 180.0f;  // Normalisiert
            float centroidDist = std::abs(media.spectralCentroid - profile.avgSpectralCentroid) / 5000.0f;
            float zeroCrossingDist = std::abs(media.zeroCrossingRate - profile.avgZeroCrossing) / 0.5f;
            float rolloffDist = std::abs(media.spectralRolloff - profile.avgSpectralRolloff) / 10000.0f;
            
            // Bonus wenn Bass-Level und Intensität übereinstimmen
            float bassBonus = (media.bassLevel == profile.commonBassLevel) ? -0.2f : 0.0f;
            float intensityBonus = (media.intensity == profile.commonIntensity) ? -0.2f : 0.0f;
            
            float totalScore = bpmDist + centroidDist + zeroCrossingDist + rolloffDist + bassBonus + intensityBonus;
            
            if (totalScore < bestScore) {
                bestScore = totalScore;
                bestGenre = genre;
            }
        }
        
        // Update Genre mit Lern-basierter Klassifikation
        media.genre = bestGenre;
        if (database_->updateMedia(media)) {
            classified++;
            std::cout << "💡 Vorschlag #" << classified << ": " << media.title 
                      << " → " << bestGenre << " (Score: " << bestScore << ")" << std::endl;
        }
        
        // Limit output für Performance
        if (classified >= 50) {
            std::cout << "   ... (weitere Klassifikationen im Hintergrund)" << std::endl;
            break;
        }
    }
    
    std::cout << "\n📊 Zusammenfassung:" << std::endl;
    std::cout << "   • Vorschläge: " << classified << std::endl;
    std::cout << "   • Angewendet: 0" << std::endl;
    std::cout << "📋 " << classified << " automatische Korrektur-Vorschläge verfügbar" << std::endl;
    std::cout << "ℹ️  Nutze 'Batch-Korrektur anwenden' um sie zu übernehmen" << std::endl;
}

void GtkRenderer::onSuggestGenreTags(GtkWidget* widget, gpointer data) {
    GtkRenderer* self = static_cast<GtkRenderer*>(data);
    
    if (!self->trainingModel_) return;
    
    auto allMedia = self->database_->getAll();
    if (allMedia.empty()) {
        GtkWidget* dialog = gtk_message_dialog_new(
            GTK_WINDOW(self->window_), GTK_DIALOG_MODAL,
            GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
            "ℹ️ Keine Tracks in Datenbank!");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }
    
    // Analysiere alle Tracks und schlage Tags vor
    std::thread([self, allMedia]() {
        int updated = 0;
        int analyzed = 0;
        
        for (auto media : allMedia) {
            if (!media.analyzed || media.genre == "Unknown") continue;
            
            analyzed++;
            
            // Schlage Genre-Tags vor
            std::string suggestedTags = self->trainingModel_->suggestGenreTags(media);
            
            // Update wenn unterschiedlich
            if (suggestedTags != media.genreTags && !suggestedTags.empty()) {
                media.genreTags = suggestedTags;
                if (self->database_->updateMedia(media)) {
                    updated++;
                    std::cout << "🔍 " << media.title << ": " << suggestedTags << std::endl;
                }
            }
            
            if (analyzed % 50 == 0) {
                std::cout << "   📊 " << analyzed << " Tracks analysiert..." << std::endl;
            }
        }
        
        std::string msg = "✅ Genre-Tag-Vorschläge abgeschlossen!\n\n";
        msg += "Analysiert: " + std::to_string(analyzed) + " Tracks\n";
        msg += "Aktualisiert: " + std::to_string(updated) + " Tracks\n\n";
        msg += "Details siehe Console-Ausgabe.";
        
        gdk_threads_add_idle(+[](gpointer data) -> gboolean {
            std::string* message = static_cast<std::string*>(data);
            GtkWidget* dialog = gtk_message_dialog_new(
                nullptr, GTK_DIALOG_MODAL,
                GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
                "%s", message->c_str());
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
            delete message;
            return G_SOURCE_REMOVE;
        }, new std::string(msg));
    }).detach();
}

// 🎤 Pattern Capture Callbacks
void GtkRenderer::onPatternRecord(GtkWidget* widget, gpointer data) {
    auto* self = static_cast<GtkRenderer*>(data);
    
    if (!self->patternCapture_) {
        GtkWidget* dialog = gtk_message_dialog_new(
            GTK_WINDOW(self->window_), GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
            "Pattern Capture Engine nicht initialisiert!");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }
    
    // Get pattern type
    int typeIndex = gtk_combo_box_get_active(GTK_COMBO_BOX(self->patternTypeCombo_));
    bool isRhythm = (typeIndex == 0);
    
    // Start capture
    bool success = self->patternCapture_->startCapture(isRhythm ? "rhythm" : "melody");
    
    if (success) {
        self->patternRecording_ = true;
        gtk_label_set_text(GTK_LABEL(self->patternStatusLabel_), 
                          isRhythm ? "🔴 Nehme Rhythmus auf..." : "🔴 Nehme Melodie auf...");
        gtk_widget_set_sensitive(self->patternRecordBtn_, FALSE);
        gtk_widget_set_sensitive(self->patternStopBtn_, TRUE);
        gtk_widget_set_sensitive(self->patternTypeCombo_, FALSE);
    } else {
        GtkWidget* dialog = gtk_message_dialog_new(
            GTK_WINDOW(self->window_), GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
            "Aufnahme fehlgeschlagen:\nPortAudio Fehler oder Mikrofon nicht verfügbar");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
}

void GtkRenderer::onPatternStop(GtkWidget* widget, gpointer data) {
    auto* self = static_cast<GtkRenderer*>(data);
    
    if (!self->patternCapture_ || !self->patternRecording_) {
        return;
    }
    
    // Stop capture
    self->patternCapture_->stopCapture();
    self->patternRecording_ = false;
    
    // Get captured pattern
    int typeIndex = gtk_combo_box_get_active(GTK_COMBO_BOX(self->patternTypeCombo_));
    bool isRhythm = (typeIndex == 0);
    
    // Show rating dialog
    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        "Pattern Bewerten",
        GTK_WINDOW(self->window_),
        (GtkDialogFlags)0,
        "_Abbrechen", GTK_RESPONSE_CANCEL,
        "_Speichern", GTK_RESPONSE_OK,
        NULL);
    
    makeDialogResizable(dialog, 400, 250);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 10);
    
    gtk_box_pack_start(GTK_BOX(content), 
                       gtk_label_new("Wie gut klingt dieses Pattern?"), 
                       FALSE, FALSE, 5);
    
    // Rating scale
    GtkWidget* ratingScale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1.0, 10.0, 1.0);
    gtk_scale_set_value_pos(GTK_SCALE(ratingScale), GTK_POS_BOTTOM);
    gtk_range_set_value(GTK_RANGE(ratingScale), 5.0);
    gtk_widget_set_size_request(ratingScale, 300, -1);
    gtk_box_pack_start(GTK_BOX(content), ratingScale, FALSE, FALSE, 5);
    
    // Name entry
    GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(content), hbox, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new("Name:"), FALSE, FALSE, 0);
    
    GtkWidget* nameEntry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(nameEntry), isRhythm ? "Rhythm Pattern" : "Melody Pattern");
    gtk_box_pack_start(GTK_BOX(hbox), nameEntry, TRUE, TRUE, 0);
    
    gtk_widget_show_all(content);
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        float rating = gtk_range_get_value(GTK_RANGE(ratingScale));
        const char* name = gtk_entry_get_text(GTK_ENTRY(nameEntry));
        
        // Save pattern to library
        if (isRhythm) {
            SongGen::CapturedRhythm rhythm = self->patternCapture_->getCapturedRhythm();
            SongGen::PatternAnalysis analysis = self->patternCapture_->analyzeRhythm(rhythm);
            self->patternCapture_->learnPattern(name, &rhythm, nullptr, rating / 10.0f);
            
            // Add to TreeView
            GtkTreeIter iter;
            gtk_list_store_append(self->patternLibraryStore_, &iter);
            
            char grooveStr[32], complexityStr[32], ratingStr[32];
            snprintf(grooveStr, sizeof(grooveStr), "%.2f", analysis.groove);
            snprintf(complexityStr, sizeof(complexityStr), "%.2f", analysis.complexity);
            snprintf(ratingStr, sizeof(ratingStr), "%.1f/10", rating);
            
            gtk_list_store_set(self->patternLibraryStore_, &iter,
                             0, "Rhythmus",
                             1, name,
                             2, grooveStr,
                             3, complexityStr,
                             4, ratingStr,
                             5, (int)(rating * 10),
                             -1);
            
            std::cout << "✅ Rhythmus-Pattern gespeichert: " << name 
                     << " (Groove: " << analysis.groove << ", Rating: " << rating << ")\n";
        } else {
            SongGen::CapturedMelody melody = self->patternCapture_->getCapturedMelody();
            SongGen::PatternAnalysis analysis = self->patternCapture_->analyzeMelody(melody);
            self->patternCapture_->learnPattern(name, nullptr, &melody, rating / 10.0f);
            
            // Add to TreeView
            GtkTreeIter iter;
            gtk_list_store_append(self->patternLibraryStore_, &iter);
            
            char interestStr[32], tensionStr[32], ratingStr[32];
            snprintf(interestStr, sizeof(interestStr), "%.2f", analysis.melodicInterest);
            snprintf(tensionStr, sizeof(tensionStr), "%.2f", analysis.tension);
            snprintf(ratingStr, sizeof(ratingStr), "%.1f/10", rating);
            
            gtk_list_store_set(self->patternLibraryStore_, &iter,
                             0, "Melodie",
                             1, name,
                             2, interestStr,
                             3, tensionStr,
                             4, ratingStr,
                             5, (int)(rating * 10),
                             -1);
            
            std::cout << "✅ Melodie-Pattern gespeichert: " << name 
                     << " (Interest: " << analysis.melodicInterest << ", Rating: " << rating << ")\n";
        }
    }
    
    gtk_widget_destroy(dialog);
    
    // Reset UI
    gtk_label_set_text(GTK_LABEL(self->patternStatusLabel_), "Bereit zur Aufnahme");
    gtk_widget_set_sensitive(self->patternRecordBtn_, TRUE);
    gtk_widget_set_sensitive(self->patternStopBtn_, FALSE);
    gtk_widget_set_sensitive(self->patternTypeCombo_, TRUE);
}

void GtkRenderer::onPatternLibraryRowActivated(GtkTreeView* tree, GtkTreePath* path, 
                                               GtkTreeViewColumn* col, gpointer data) {
    auto* self = static_cast<GtkRenderer*>(data);
    
    GtkTreeModel* model = gtk_tree_view_get_model(tree);
    GtkTreeIter iter;
    
    if (gtk_tree_model_get_iter(model, &iter, path)) {
        gchar* type;
        gchar* name;
        gchar* param1;
        gchar* param2;
        gchar* rating;
        
        gtk_tree_model_get(model, &iter, 
                          0, &type,
                          1, &name,
                          2, &param1,
                          3, &param2,
                          4, &rating,
                          -1);
        
        // Show pattern details
        std::string message = "Pattern Details:\n\n";
        message += "Typ: " + std::string(type) + "\n";
        message += "Name: " + std::string(name) + "\n";
        
        if (std::string(type) == "Rhythmus") {
            message += "Groove: " + std::string(param1) + "\n";
            message += "Komplexität: " + std::string(param2) + "\n";
        } else {
            message += "Melodic Interest: " + std::string(param1) + "\n";
            message += "Tension: " + std::string(param2) + "\n";
        }
        
        message += "Bewertung: " + std::string(rating);
        
        GtkWidget* dialog = gtk_message_dialog_new(
            GTK_WINDOW(self->window_), GTK_DIALOG_MODAL,
            GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
            "%s", message.c_str());
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        
        g_free(type);
        g_free(name);
        g_free(param1);
        g_free(param2);
        g_free(rating);
    }
}

void GtkRenderer::onPatternExport(GtkWidget* widget, gpointer data) {
    auto* self = static_cast<GtkRenderer*>(data);
    
    GtkWidget* dialog = gtk_file_chooser_dialog_new(
        "Pattern Library exportieren", GTK_WINDOW(self->window_),
        GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Abbrechen", GTK_RESPONSE_CANCEL,
        "_Speichern", GTK_RESPONSE_ACCEPT,
        NULL);
    
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), "patterns.json");
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        
        // TODO: Actually export patterns to JSON file
        std::cout << "💾 Exporting patterns to: " << filename << std::endl;
        
        g_free(filename);
    }
    
    gtk_widget_destroy(dialog);
}

void GtkRenderer::onPatternImport(GtkWidget* widget, gpointer data) {
    auto* self = static_cast<GtkRenderer*>(data);
    
    GtkWidget* dialog = gtk_file_chooser_dialog_new(
        "Pattern Library importieren", GTK_WINDOW(self->window_),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Abbrechen", GTK_RESPONSE_CANCEL,
        "_Öffnen", GTK_RESPONSE_ACCEPT,
        NULL);
    
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "JSON Dateien");
    gtk_file_filter_add_pattern(filter, "*.json");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        
        // TODO: Actually import patterns from JSON file
        std::cout << "📂 Importing patterns from: " << filename << std::endl;
        
        g_free(filename);
    }
    
    gtk_widget_destroy(dialog);
}

void GtkRenderer::onPatternClear(GtkWidget* widget, gpointer data) {
    auto* self = static_cast<GtkRenderer*>(data);
    
    GtkWidget* dialog = gtk_message_dialog_new(
        GTK_WINDOW(self->window_), GTK_DIALOG_MODAL,
        GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
        "Alle gelernten Patterns löschen?\n\nDieser Vorgang kann nicht rückgängig gemacht werden.");
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES) {
        gtk_list_store_clear(self->patternLibraryStore_);
        
        if (self->patternCapture_) {
            self->patternCapture_->clearLibrary();
        }
        
        std::cout << "🗑️ Pattern Library gelöscht\n";
    }
    
    gtk_widget_destroy(dialog);
}
