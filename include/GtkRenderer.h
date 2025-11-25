#ifndef GTKRENDERER_H
#define GTKRENDERER_H

#include "MediaDatabase.h"
#include "AudioAnalyzer.h"
#include "FileBrowser.h"
#include "SongGenerator.h"
#include "HVSCDownloader.h"
#include "AudioPlayer.h"
#include <gtk/gtk.h>
#include <memory>
#include <string>
#include <atomic>
#include <thread>

/**
 * GtkRenderer - Native GTK/XFCE GUI für SongGen
 * Ersetzt ImGui komplett durch GTK3 Widgets
 */
class GtkRenderer {
public:
    GtkRenderer();
    ~GtkRenderer();
    
    bool initialize();
    void run();
    void shutdown();
    
private:
    // GTK Widgets
    GtkWidget* window_;
    GtkWidget* notebook_;  // Tab-Container
    GtkWidget* statusbar_;
    GtkWidget* gpuProgressBar_;
    GtkWidget* gpuLabel_;
    
    // Tab-Widgets
    GtkWidget* dbTab_;
    GtkWidget* dbTreeView_;
    GtkWidget* dbInfoLabel_;
    GtkWidget* dbSearchEntry_;
    GtkWidget* dbGenreCombo_;
    GtkWidget* browserTab_;
    GtkWidget* hvscTab_;
    GtkWidget* hvscProgressBar_;
    GtkWidget* hvscStatusLabel_;
    GtkWidget* generatorTab_;
    GtkWidget* genGenreCombo_;
    GtkWidget* genBpmSpin_;
    GtkWidget* genDurationSpin_;
    GtkWidget* genIntensityCombo_;
    GtkWidget* settingsTab_;
    GtkWidget* trainingTab_;
    GtkWidget* analyzerTab_;
    GtkWidget* trainingProgressBar_;
    GtkWidget* trainingStatusLabel_;
    GtkWidget* historyTreeView_;
    
    // Components
    std::unique_ptr<MediaDatabase> database_;
    std::unique_ptr<AudioAnalyzer> analyzer_;
    std::unique_ptr<FileBrowser> fileBrowser_;
    std::unique_ptr<SongGenerator> generator_;
    std::unique_ptr<HVSCDownloader> hvscDownloader_;
    std::unique_ptr<AudioPlayer> audioPlayer_;
    
    // State
    std::vector<MediaMetadata> filteredMedia_;
    std::atomic<bool> running_{false};
    std::atomic<bool> isDeleting_{false};
    std::atomic<size_t> deleteProgress_{0};
    std::atomic<size_t> deleteTotal_{0};
    std::atomic<size_t> mp3Extracted_{0};
    std::atomic<size_t> mp3Total_{0};
    std::atomic<bool> stopExtraction_{false};
    std::thread autoSyncThread_;
    std::atomic<bool> autoSyncRunning_{false};
    std::atomic<bool> isTraining_{false};
    std::atomic<size_t> trainingEpoch_{0};
    std::atomic<size_t> trainingMaxEpochs_{0};
    std::string currentSortColumn_;
    bool sortAscending_{true};
    std::string currentAudioFile_;  // Für Play-Button in Decision Dialog
    
    // Tab-Aufbau
    void buildDatabaseTab();
    void buildBrowserTab();
    void buildHVSCTab();
    void buildGeneratorTab();
    void buildSettingsTab();
    void buildTrainingTab();
    void buildAnalyzerTab();
    void refreshDatabaseView();
    void sortDatabaseBy(const std::string& column);
    void saveGeneratorPreset(const std::string& name);
    void loadGeneratorPreset(const std::string& name);
    
    // Callbacks
    static void onDeleteDatabase(GtkWidget* widget, gpointer data);
    static void onCleanupMissing(GtkWidget* widget, gpointer data);
    static void onAutoDetectGenres(GtkWidget* widget, gpointer data);
    static void onAnalyzeAll(GtkWidget* widget, gpointer data);
    static void onRepairClipping(GtkWidget* widget, gpointer data);
    static void onFindDuplicates(GtkWidget* widget, gpointer data);
    static void onShowTrainingStats(GtkWidget* widget, gpointer data);
    static void onBalanceDataset(GtkWidget* widget, gpointer data);
    static void onExtractHVSC(GtkWidget* widget, gpointer data);
    static void onDownloadHVSC(GtkWidget* widget, gpointer data);
    static void onBrowseLocal(GtkWidget* widget, gpointer data);
    static void onBrowseSMB(GtkWidget* widget, gpointer data);
    static void onBrowseFTP(GtkWidget* widget, gpointer data);
    static void onAddressBarGo(GtkWidget* widget, gpointer data);
    static void onAddSelectedFiles(GtkWidget* widget, gpointer data);
    static void onAddCurrentFolder(GtkWidget* widget, gpointer data);
    static void onBookmarkClick(GtkWidget* widget, gpointer data);
    static void onFolderChanged(GtkFileChooser* chooser, gpointer data);
    static void onPlaySong(GtkTreeView* tree_view, GtkTreePath* path, GtkTreeViewColumn* column, gpointer data);
    static void onSearchChanged(GtkEntry* entry, gpointer data);
    static void onGenreChanged(GtkWidget* widget, gpointer data);
    static void onQuickPresetChillout(GtkWidget* widget, gpointer data);
    static void onQuickPresetTechno(GtkWidget* widget, gpointer data);
    static void onQuickPresetAmbient(GtkWidget* widget, gpointer data);
    static void onQuickPresetHouse(GtkWidget* widget, gpointer data);
    static void onQuickPresetJazz(GtkWidget* widget, gpointer data);
    static void onQuickPresetSalsa(GtkWidget* widget, gpointer data);
    static void onQuickPresetWalzer(GtkWidget* widget, gpointer data);
    static void onQuickPresetRnB(GtkWidget* widget, gpointer data);
    static void onGenreFilterChanged(GtkComboBox* combo, gpointer data);
    static void onClearSearch(GtkWidget* widget, gpointer data);
    static void onColumnHeaderClicked(GtkWidget* widget, gpointer data);
    static void onTrainModel(GtkWidget* widget, gpointer data);
    static void onSaveModel(GtkWidget* widget, gpointer data);
    static void onLoadModel(GtkWidget* widget, gpointer data);
    static void onSavePreset(GtkWidget* widget, gpointer data);
    static void onLoadPreset(GtkWidget* widget, gpointer data);
    static void onAnalyzeFile(GtkWidget* widget, gpointer data);
    static void onStopExtraction(GtkWidget* widget, gpointer data);
    static void onGenerateSong(GtkWidget* widget, gpointer data);
    static void onDBSync(GtkWidget* widget, gpointer data);
    static void onDestroy(GtkWidget* widget, gpointer data);
    static void onShowDecisionHistory(GtkWidget* widget, gpointer data);
    
    // Helper
    void startAutoSync();
    void stopAutoSync();
    void updateStatusBar(const std::string& message);
    void updateGPUUsage();
    static gboolean onGPUUpdateTimer(gpointer data);
    void showProgressDialog(const std::string& title, std::atomic<size_t>& progress, std::atomic<size_t>& total, std::atomic<bool>& running);
    
    // Interactive Training
    std::string showDecisionDialog(const std::string& question, const std::vector<std::string>& options, const std::string& context = "", const std::string& audioFile = "");
    static void onPlayAudioSample(GtkWidget* widget, gpointer data);
    static void onStopAudioSample(GtkWidget* widget, gpointer data);
};

#endif // GTKRENDERER_H
