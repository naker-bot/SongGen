#ifndef FILEBROWSER_H
#define FILEBROWSER_H

#include <string>
#include <vector>
#include <functional>
#include <memory>

/**
 * Dateieintrag im Browser
 */
struct FileEntry {
    std::string path;
    std::string name;
    bool isDirectory;
    size_t size;
    bool isSelected = false;
    
    // Netzwerk-Metadaten
    bool isRemote = false;
    std::string protocol;  // "smb", "ftp", "local"
    std::string remoteHost;
};

/**
 * FileBrowser - Datei-Browser mit Netzwerk-Unterstützung
 * 
 * Features:
 * - Lokale Dateien
 * - SMB/CIFS (Samba/Windows-Freigaben)
 * - FTP/FTPS
 * - GIO-Integration (GVFS)
 * - Mehrfachauswahl
 * - "Alle Auswählen" Button
 * - Vorschau-Player
 * - Drag & Drop zur Datenbank
 */
class FileBrowser {
public:
    FileBrowser();
    ~FileBrowser();
    
    // Navigation
    bool navigate(const std::string& path);
    bool navigateUp();
    void refresh();
    std::string getCurrentPath() const { return currentPath_; }
    
    // Datei-Operationen
    std::vector<FileEntry> getEntries() const { return entries_; }
    std::vector<FileEntry> getSelectedEntries() const;
    bool selectEntry(const std::string& path);
    bool deselectEntry(const std::string& path);
    void selectAll();
    void deselectAll();
    
    // Filter
    void setFilter(const std::string& extension);  // z.B. ".mp3,.wav,.flac"
    void clearFilter();
    
    // Suche
    void setSearchQuery(const std::string& query);
    std::string getSearchQuery() const { return searchQuery_; }
    
    // Netzwerk-Verbindungen
    bool connectSMB(const std::string& server, const std::string& share, 
                    const std::string& username = "", const std::string& password = "");
    bool connectFTP(const std::string& server, int port = 21,
                    const std::string& username = "anonymous", const std::string& password = "");
    bool mountGIO(const std::string& uri);  // gio mount smb://server/share
    
    // Disconnect
    void disconnect();
    bool isConnected() const { return isConnected_; }
    
private:
    std::string currentPath_;
    std::vector<FileEntry> entries_;
    std::string filter_;
    std::string searchQuery_;
    
    // Netzwerk-Status
    bool isConnected_ = false;
    std::string connectionType_;  // "smb", "ftp", "gio", "local"
    void* connectionHandle_ = nullptr;
    
    // Implementierungen
    bool browseLocal(const std::string& path);
    bool browseSMB(const std::string& path);
    bool browseFTP(const std::string& path);
    bool browseGIO(const std::string& path);
    
    bool matchesFilter(const std::string& filename);
    bool matchesSearch(const std::string& filename);
};

#endif // FILEBROWSER_H
