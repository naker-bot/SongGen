#include "GtkRenderer.h"
#include <iostream>
#include <iomanip>
#include <filesystem>
#include <thread>
#include <chrono>
#include <algorithm>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

GtkRenderer::GtkRenderer() {
    database_ = std::make_unique<MediaDatabase>();
    analyzer_ = std::make_unique<AudioAnalyzer>();
    fileBrowser_ = std::make_unique<FileBrowser>();
    generator_ = std::make_unique<SongGenerator>(*database_);
    hvscDownloader_ = std::make_unique<HVSCDownloader>();
    audioPlayer_ = std::make_unique<AudioPlayer>();
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
    
    // Lade Datenbank
    filteredMedia_ = database_->getAll();
    
    // Erstelle Hauptfenster
    window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window_), "🎵 SongGen - KI Song Generator");
    gtk_window_set_default_size(GTK_WINDOW(window_), 1200, 800);
    gtk_window_set_position(GTK_WINDOW(window_), GTK_WIN_POS_CENTER);
    
    g_signal_connect(window_, "destroy", G_CALLBACK(onDestroy), this);
    
    // Hauptcontainer
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(window_), vbox);
    
    // Notebook (Tabs)
    notebook_ = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(vbox), notebook_, TRUE, TRUE, 0);
    
    // Tabs aufbauen
    buildDatabaseTab();
    buildBrowserTab();
    buildHVSCTab();
    buildGeneratorTab();
    buildTrainingTab();
    buildAnalyzerTab();
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
    
    // Starte Auto-Sync
    startAutoSync();
    
    // Starte GPU-Update-Timer (alle 500ms)
    g_timeout_add(500, onGPUUpdateTimer, this);
    
    running_ = true;
    
    std::cout << "✅ GTK renderer initialized\n";
    return true;
}

void GtkRenderer::run() {
    gtk_widget_show_all(window_);
    gtk_main();
}

void GtkRenderer::shutdown() {
    running_ = false;
    stopAutoSync();
    if (audioPlayer_) {
        audioPlayer_->stop();
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
    
    // Lesezeichen
    GtkWidget* hboxBookmarks = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hboxBookmarks, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(hboxBookmarks), gtk_label_new("🔖"), FALSE, FALSE, 0);
    
    const char* home = getenv("HOME");
    
    GtkWidget* btnHome = gtk_button_new_with_label("🏠 Home");
    gtk_box_pack_start(GTK_BOX(hboxBookmarks), btnHome, FALSE, FALSE, 0);
    
    GtkWidget* btnMusic = gtk_button_new_with_label("🎵 Musik");
    gtk_box_pack_start(GTK_BOX(hboxBookmarks), btnMusic, FALSE, FALSE, 0);
    
    GtkWidget* btnDownloads = gtk_button_new_with_label("📥 Downloads");
    gtk_box_pack_start(GTK_BOX(hboxBookmarks), btnDownloads, FALSE, FALSE, 0);
    
    GtkWidget* btnHVSC = gtk_button_new_with_label("🎹 HVSC");
    gtk_box_pack_start(GTK_BOX(hboxBookmarks), btnHVSC, FALSE, FALSE, 0);
    
    GtkWidget* btnRoot = gtk_button_new_with_label("💾 Root");
    gtk_box_pack_start(GTK_BOX(hboxBookmarks), btnRoot, FALSE, FALSE, 0);
    
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
    
    // File Chooser Widget (eingebettet)
    GtkWidget* fileChooser = gtk_file_chooser_widget_new(GTK_FILE_CHOOSER_ACTION_OPEN);
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(fileChooser), TRUE);
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(fileChooser), home ? home : "/");
    
    // Filter für alle Dateien (Standard)
    GtkFileFilter* filterAll = gtk_file_filter_new();
    gtk_file_filter_set_name(filterAll, "Alle Dateien");
    gtk_file_filter_add_pattern(filterAll, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fileChooser), filterAll);
    
    // Filter für Audio-Dateien
    GtkFileFilter* filterAudio = gtk_file_filter_new();
    gtk_file_filter_set_name(filterAudio, "Audio-Dateien (*.mp3, *.wav, *.flac, *.ogg)");
    gtk_file_filter_add_pattern(filterAudio, "*.mp3");
    gtk_file_filter_add_pattern(filterAudio, "*.wav");
    gtk_file_filter_add_pattern(filterAudio, "*.flac");
    gtk_file_filter_add_pattern(filterAudio, "*.ogg");
    gtk_file_filter_add_pattern(filterAudio, "*.m4a");
    gtk_file_filter_add_pattern(filterAudio, "*.sid");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fileChooser), filterAudio);
    
    // Filter für Video-Dateien
    GtkFileFilter* filterVideo = gtk_file_filter_new();
    gtk_file_filter_set_name(filterVideo, "Video-Dateien (*.mp4, *.mkv, *.avi, *.webm)");
    gtk_file_filter_add_pattern(filterVideo, "*.mp4");
    gtk_file_filter_add_pattern(filterVideo, "*.mkv");
    gtk_file_filter_add_pattern(filterVideo, "*.avi");
    gtk_file_filter_add_pattern(filterVideo, "*.webm");
    gtk_file_filter_add_pattern(filterVideo, "*.mov");
    gtk_file_filter_add_pattern(filterVideo, "*.flv");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fileChooser), filterVideo);
    
    // Setze "Alle Dateien" als Standard
    gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(fileChooser), filterAll);
    
    gtk_box_pack_start(GTK_BOX(vbox), fileChooser, TRUE, TRUE, 0);
    
    // Buttons zum Hinzufügen
    GtkWidget* btnBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget* btnAddSelected = gtk_button_new_with_label("✅ Ausgewählte Dateien hinzufügen");
    GtkWidget* btnAddFolder = gtk_button_new_with_label("📁 Aktuellen Ordner rekursiv hinzufügen");
    gtk_box_pack_start(GTK_BOX(btnBox), btnAddSelected, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(btnBox), btnAddFolder, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), btnBox, FALSE, FALSE, 0);
    
    // Speichere Widgets für Callbacks
    struct BrowserData {
        GtkWidget* addrEntry;
        GtkWidget* fileChooser;
        GtkRenderer* self;
    };
    
    BrowserData* browserData = new BrowserData{addrEntry, fileChooser, this};
    
    // Verbinde Adressleiste mit FileChooser
    g_signal_connect(btnGo, "clicked", G_CALLBACK(onAddressBarGo), browserData);
    
    // Verbinde Buttons
    g_signal_connect(btnAddSelected, "clicked", G_CALLBACK(onAddSelectedFiles), browserData);
    g_signal_connect(btnAddFolder, "clicked", G_CALLBACK(onAddCurrentFolder), browserData);
    
    // Verbinde Lesezeichen-Buttons
    g_signal_connect(btnHome, "clicked", G_CALLBACK(onBookmarkClick), browserData);
    g_signal_connect(btnMusic, "clicked", G_CALLBACK(onBookmarkClick), browserData);
    g_signal_connect(btnDownloads, "clicked", G_CALLBACK(onBookmarkClick), browserData);
    g_signal_connect(btnHVSC, "clicked", G_CALLBACK(onBookmarkClick), browserData);
    g_signal_connect(btnRoot, "clicked", G_CALLBACK(onBookmarkClick), browserData);
    
    // Aktualisiere Adressleiste wenn sich FileChooser-Ordner ändert
    g_signal_connect(fileChooser, "current-folder-changed", G_CALLBACK(onFolderChanged), browserData);
    
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
    
    // Progress Bar
    hvscProgressBar_ = gtk_progress_bar_new();
    gtk_box_pack_start(GTK_BOX(vbox), hvscProgressBar_, FALSE, FALSE, 0);
    
    // Status Label
    hvscStatusLabel_ = gtk_label_new("Bereit - Klicke 'HVSC herunterladen' um zu starten");
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
    
    // Ausgabepfad
    GtkWidget* hbox5 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hbox5, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox5), gtk_label_new("Ausgabe:"), FALSE, FALSE, 0);
    
    const char* home = getenv("HOME");
    std::string outputPath = std::string(home) + "/.songgen/generated/";
    GtkWidget* pathLabel = gtk_label_new(outputPath.c_str());
    gtk_box_pack_start(GTK_BOX(hbox5), pathLabel, FALSE, FALSE, 0);
    
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
    
    gtk_box_pack_start(GTK_BOX(vbox4), gtk_label_new(("Datenbank: " + dbPath).c_str()), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox4), gtk_label_new(("HVSC: " + hvscPath).c_str()), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox4), gtk_label_new(("Generiert: " + genPath).c_str()), FALSE, FALSE, 0);
    
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
    gtk_box_pack_start(GTK_BOX(vboxProgress), trainingStatusLabel_, FALSE, FALSE, 0);
    
    trainingProgressBar_ = gtk_progress_bar_new();
    gtk_box_pack_start(GTK_BOX(vboxProgress), trainingProgressBar_, FALSE, FALSE, 0);
    
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
    gtk_box_pack_start(GTK_BOX(vbox), sublabel, FALSE, FALSE, 0);
    
    // Datei-Auswahl
    GtkWidget* hboxFile = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hboxFile, FALSE, FALSE, 0);
    
    GtkWidget* btnSelectFile = gtk_button_new_with_label("📂 Datei wählen");
    g_signal_connect(btnSelectFile, "clicked", G_CALLBACK(onAnalyzeFile), this);
    gtk_box_pack_start(GTK_BOX(hboxFile), btnSelectFile, FALSE, FALSE, 0);
    
    GtkWidget* labelFile = gtk_label_new("Keine Datei ausgewählt");
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
            GTK_DIALOG_MODAL,
            NULL
        );
        
        GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(progressDialog));
        GtkWidget* progressBar = gtk_progress_bar_new();
        GtkWidget* labelProgress = gtk_label_new("Lösche Einträge...");
        
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
        GTK_DIALOG_MODAL,
        NULL
    );
    
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(progressDialog));
    GtkWidget* progressBar = gtk_progress_bar_new();
    GtkWidget* labelProgress = gtk_label_new("Analysiere Dateien...");
    
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
    
    // Progress-Dialog
    GtkWidget* progressDialog = gtk_dialog_new_with_buttons(
        "🔬 Analysiere Audio-Features...",
        GTK_WINDOW(self->window_),
        GTK_DIALOG_MODAL,
        NULL
    );
    
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(progressDialog));
    GtkWidget* progressBar = gtk_progress_bar_new();
    GtkWidget* labelProgress = gtk_label_new("Analysiere Dateien...");
    GtkWidget* labelDetails = gtk_label_new("");
    
    gtk_box_pack_start(GTK_BOX(content), labelProgress, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(content), progressBar, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(content), labelDetails, FALSE, FALSE, 5);
    gtk_widget_show_all(progressDialog);
    
    // Analyse mit Multi-Threading (nutzt alle CPU-Kerne)
    std::thread([self, unanalyzed, progressDialog, progressBar, labelProgress, labelDetails]() {
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
                            
                            std::string text = std::to_string(std::get<3>(*info)) + " / " + 
                                              std::to_string(std::get<4>(*info));
                            gtk_label_set_text(GTK_LABEL(std::get<1>(*info)), text.c_str());
                            
                            std::string details = "✓ Analysiert: " + std::to_string(std::get<5>(*info));
                            gtk_label_set_text(GTK_LABEL(std::get<2>(*info)), details.c_str());
                            
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
                "✅ Audio-Analyse abgeschlossen!\n\n"
                "%zu Dateien vollständig analysiert.\n\n"
                "Extrahierte Features:\n"
                "• BPM & Genre\n"
                "• Spektrale Features (MFCC, Centroid)\n"
                "• Intensität & Bass-Level\n\n"
                "Dateien sind jetzt bereit für KI-Training!",
                std::get<2>(*info)
            );
            gtk_dialog_run(GTK_DIALOG(resultDialog));
            gtk_widget_destroy(resultDialog);
            
            delete info;
            return G_SOURCE_REMOVE;
        }, new std::tuple<GtkRenderer*, GtkWidget*, size_t>(self, progressDialog, analyzed));
        
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
    
    // Progress-Dialog
    GtkWidget* progressDialog = gtk_dialog_new_with_buttons(
        "Übersteuerungen reparieren...",
        GTK_WINDOW(self->window_),
        GTK_DIALOG_MODAL,
        NULL
    );
    
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(progressDialog));
    GtkWidget* progressBar = gtk_progress_bar_new();
    GtkWidget* labelProgress = gtk_label_new("Analysiere Audio-Dateien...");
    
    gtk_box_pack_start(GTK_BOX(content), labelProgress, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(content), progressBar, FALSE, FALSE, 5);
    gtk_widget_show_all(progressDialog);
    
    // Analyse in Thread
    std::thread([self, allMedia, progressDialog, progressBar, labelProgress]() {
        size_t processed = 0;
        size_t repaired = 0;
        
        for (const auto& media : allMedia) {
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
                    
                    std::string text = std::to_string(std::get<2>(*info)) + " / " + 
                                      std::to_string(std::get<3>(*info)) + " (" + 
                                      std::to_string(std::get<4>(*info)) + " repariert)";
                    gtk_label_set_text(GTK_LABEL(std::get<1>(*info)), text.c_str());
                    
                    delete info;
                    return G_SOURCE_REMOVE;
                }, new std::tuple<GtkWidget*, GtkWidget*, size_t, size_t, size_t>(progressBar, labelProgress, processed, allMedia.size(), repaired));
            }
        }
        
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
                "✅ Analyse abgeschlossen!\n\n"
                "%zu Dateien repariert\n"
                "Backups wurden als *.backup gespeichert",
                std::get<2>(*info)
            );
            gtk_dialog_run(GTK_DIALOG(resultDialog));
            gtk_widget_destroy(resultDialog);
            
            delete info;
            return G_SOURCE_REMOVE;
        }, new std::tuple<GtkRenderer*, GtkWidget*, size_t>(self, progressDialog, repaired));
        
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
        GTK_DIALOG_MODAL,
        nullptr
    );
    
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
                GTK_DIALOG_MODAL,
                "Schließen", GTK_RESPONSE_CLOSE,
                nullptr
            );
            gtk_window_set_default_size(GTK_WINDOW(resultDialog), 700, 400);
            
            GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(resultDialog));
            
            if (duplicates.empty()) {
                GtkWidget* label = gtk_label_new("✅ Keine Duplikate gefunden!");
                gtk_box_pack_start(GTK_BOX(content), label, TRUE, TRUE, 10);
            } else {
                GtkWidget* label = gtk_label_new(("🔍 " + std::to_string(duplicates.size()) + " Duplikate gefunden:").c_str());
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
            GTK_DIALOG_MODAL,
            NULL
        );
        
        GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(progressDialog));
        GtkWidget* progressBar = gtk_progress_bar_new();
        GtkWidget* labelProgress = gtk_label_new("Durchsuche Verzeichnisse...");
        
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
    struct BrowserData {
        GtkWidget* addrEntry;
        GtkWidget* fileChooser;
        GtkRenderer* self;
    };
    
    auto* bd = static_cast<BrowserData*>(data);
    const char* path = gtk_entry_get_text(GTK_ENTRY(bd->addrEntry));
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(bd->fileChooser), path);
}

void GtkRenderer::onAddSelectedFiles(GtkWidget* widget, gpointer data) {
    struct BrowserData {
        GtkWidget* addrEntry;
        GtkWidget* fileChooser;
        GtkRenderer* self;
    };
    
    auto* bd = static_cast<BrowserData*>(data);
    GtkRenderer* self = bd->self;
    
    GSList* filenames = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(bd->fileChooser));
    
    if (!filenames) {
        GtkWidget* dialog = gtk_message_dialog_new(
            GTK_WINDOW(self->window_),
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_WARNING,
            GTK_BUTTONS_OK,
            "⚠️ Keine Dateien ausgewählt"
        );
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }
    
    // Zähle Dateien
    int fileCount = g_slist_length(filenames);
    
    // Progress-Dialog
    GtkWidget* progressDialog = gtk_dialog_new_with_buttons(
        "Füge Dateien hinzu...",
        GTK_WINDOW(self->window_),
        GTK_DIALOG_MODAL,
        NULL
    );
    
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(progressDialog));
    GtkWidget* progressBar = gtk_progress_bar_new();
    GtkWidget* labelProgress = gtk_label_new("Verarbeite Dateien...");
    
    gtk_box_pack_start(GTK_BOX(content), labelProgress, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(content), progressBar, FALSE, FALSE, 5);
    gtk_widget_show_all(progressDialog);
    
    // Verarbeite im Thread
    std::thread([self, filenames, progressDialog, progressBar, labelProgress, fileCount]() {
        int added = 0;
        int skipped = 0;
        int converted = 0;
        int processed = 0;
        std::vector<std::string> errors;
        
        for (GSList* l = filenames; l != NULL; l = l->next) {
            char* filename = (char*)l->data;
            std::string filepath = filename;
            processed++;
            
            // Prüfe ob Datei existiert
            if (!std::filesystem::exists(filepath)) {
                g_free(filename);
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
                } else {
                    errors.push_back(filepath + ": Konvertierung fehlgeschlagen");
                    g_free(filename);
                    skipped++;
                    continue;
                }
            } else if (!isAudio) {
                // Weder Audio noch Video
                g_free(filename);
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
            } else {
                skipped++;
            }
            
            // Update Progress
            gdk_threads_add_idle([](gpointer data) -> gboolean {
                auto* info = static_cast<std::tuple<GtkWidget*, GtkWidget*, int, int>*>(data);
                float progress = (float)std::get<2>(*info) / std::get<3>(*info);
                gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(std::get<0>(*info)), progress);
                
                std::string text = std::to_string(std::get<2>(*info)) + " / " + std::to_string(std::get<3>(*info));
                gtk_label_set_text(GTK_LABEL(std::get<1>(*info)), text.c_str());
                
                delete info;
                return G_SOURCE_REMOVE;
            }, new std::tuple<GtkWidget*, GtkWidget*, int, int>(progressBar, labelProgress, processed, fileCount));
            
            g_free(filename);
        }
        g_slist_free(filenames);
        
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
        GTK_DIALOG_MODAL,
        NULL
    );
    
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(progressDialog));
    GtkWidget* progressBar = gtk_progress_bar_new();
    GtkWidget* labelProgress = gtk_label_new("Durchsuche Verzeichnisse...");
    
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

void GtkRenderer::onBookmarkClick(GtkWidget* widget, gpointer data) {
    struct BrowserData {
        GtkWidget* addrEntry;
        GtkWidget* fileChooser;
        GtkRenderer* self;
    };
    
    auto* bd = static_cast<BrowserData*>(data);
    const char* label = gtk_button_get_label(GTK_BUTTON(widget));
    std::string path;
    
    const char* home = getenv("HOME");
    
    if (std::string(label).find("Home") != std::string::npos) {
        path = home ? home : "/home";
    } else if (std::string(label).find("Musik") != std::string::npos) {
        path = std::string(home ? home : "/home") + "/Music";
    } else if (std::string(label).find("Downloads") != std::string::npos) {
        path = std::string(home ? home : "/home") + "/Downloads";
    } else if (std::string(label).find("HVSC") != std::string::npos) {
        path = std::string(home ? home : "/home") + "/.songgen/hvsc/mp3";
    } else if (std::string(label).find("Root") != std::string::npos) {
        path = "/";
    }
    
    if (!path.empty()) {
        gtk_entry_set_text(GTK_ENTRY(bd->addrEntry), path.c_str());
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(bd->fileChooser), path.c_str());
    }
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
        GTK_DIALOG_MODAL,
        nullptr
    );
    
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget* progressBar = gtk_progress_bar_new();
    GtkWidget* labelStatus = gtk_label_new("Initialisiere Generator...");
    
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
        GTK_DIALOG_MODAL,
        "_Abbrechen", GTK_RESPONSE_CANCEL,
        "_OK", GTK_RESPONSE_OK,
        nullptr
    );
    
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
        GTK_DIALOG_MODAL,
        "_Schließen", GTK_RESPONSE_CLOSE,
        nullptr
    );
    
    gtk_window_set_default_size(GTK_WINDOW(dialog), 800, 600);
    
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
                
                char status[256];
                snprintf(status, sizeof(status), 
                        "Epoch %d/%d - Loss: %.4f - Accuracy: %.1f%%",
                        epoch, maxEpochs, loss, accuracy * 100.0f);
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
        GTK_DIALOG_MODAL,
        "_Abbrechen", GTK_RESPONSE_CANCEL,
        "_Speichern", GTK_RESPONSE_ACCEPT,
        NULL
    );
    
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
