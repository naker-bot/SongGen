#include "FileBrowser.h"
#include <filesystem>
#include <algorithm>
#include <iostream>
#include <unistd.h>  // für getuid()

namespace fs = std::filesystem;

FileBrowser::FileBrowser() {
}

FileBrowser::~FileBrowser() {
    disconnect();
}

bool FileBrowser::navigate(const std::string& path) {
    currentPath_ = path;
    
    if (connectionType_.empty() || connectionType_ == "local") {
        return browseLocal(path);
    } else if (connectionType_ == "smb") {
        return browseSMB(path);
    } else if (connectionType_ == "ftp") {
        return browseFTP(path);
    } else if (connectionType_ == "gio") {
        return browseGIO(path);
    }
    
    return false;
}

bool FileBrowser::browseLocal(const std::string& path) {
    entries_.clear();
    
    if (!fs::exists(path) || !fs::is_directory(path)) {
        return false;
    }
    
    for (const auto& entry : fs::directory_iterator(path)) {
        FileEntry fileEntry;
        fileEntry.path = entry.path().string();
        fileEntry.name = entry.path().filename().string();
        fileEntry.isDirectory = entry.is_directory();
        fileEntry.size = entry.is_regular_file() ? fs::file_size(entry.path()) : 0;
        fileEntry.isRemote = false;
        fileEntry.protocol = "local";
        
        if ((filter_.empty() || fileEntry.isDirectory || matchesFilter(fileEntry.name)) &&
            (searchQuery_.empty() || fileEntry.isDirectory || matchesSearch(fileEntry.name))) {
            entries_.push_back(fileEntry);
        }
    }
    
    // Sortiere: Ordner zuerst, dann alphabetisch
    std::sort(entries_.begin(), entries_.end(), [](const FileEntry& a, const FileEntry& b) {
        if (a.isDirectory != b.isDirectory) return a.isDirectory;
        return a.name < b.name;
    });
    
    return true;
}

bool FileBrowser::browseSMB(const std::string& path) {
    entries_.clear();
    
    // GIO mount command als Workaround (funktioniert mit GVFS)
    std::string cmd = "gio mount -l | grep -q \"" + path + "\"";
    int mounted = system(cmd.c_str());
    
    if (mounted != 0) {
        std::cerr << "SMB share not mounted. Use: gio mount smb://server/share" << std::endl;
        return false;
    }
    
    // Browse via GVFS mount point
    std::string gvfsPath = "/run/user/" + std::to_string(getuid()) + "/gvfs/";
    
    // Suche SMB-Mount
    for (const auto& entry : fs::directory_iterator(gvfsPath)) {
        if (entry.path().string().find("smb") != std::string::npos) {
            return browseLocal(entry.path().string() + path);
        }
    }
    
    return false;
}

bool FileBrowser::browseFTP(const std::string& path) {
    entries_.clear();
    
    // GIO FTP mount als Workaround
    std::string cmd = "gio mount -l | grep -q \"ftp://\"";
    int mounted = system(cmd.c_str());
    
    if (mounted != 0) {
        std::cerr << "FTP not mounted. Use: gio mount ftp://server/" << std::endl;
        return false;
    }
    
    // Browse via GVFS FTP mount
    std::string gvfsPath = "/run/user/" + std::to_string(getuid()) + "/gvfs/";
    
    for (const auto& entry : fs::directory_iterator(gvfsPath)) {
        if (entry.path().string().find("ftp") != std::string::npos) {
            return browseLocal(entry.path().string() + path);
        }
    }
    
    return false;
}

bool FileBrowser::browseGIO(const std::string& path) {
    entries_.clear();
    
    // GIO-basiertes Browsing via GVFS
    std::string gvfsPath = "/run/user/" + std::to_string(getuid()) + "/gvfs/";
    
    if (!fs::exists(gvfsPath)) {
        std::cerr << "GVFS not available" << std::endl;
        return false;
    }
    
    // Liste alle GVFS mounts
    for (const auto& entry : fs::directory_iterator(gvfsPath)) {
        FileEntry fileEntry;
        fileEntry.path = entry.path().string();
        fileEntry.name = entry.path().filename().string();
        fileEntry.isDirectory = true;
        fileEntry.isRemote = true;
        fileEntry.protocol = "gio";
        
        entries_.push_back(fileEntry);
    }
    
    return true;
}

bool FileBrowser::navigateUp() {
    fs::path current(currentPath_);
    if (current.has_parent_path()) {
        return navigate(current.parent_path().string());
    }
    return false;
}

void FileBrowser::refresh() {
    navigate(currentPath_);
}

std::vector<FileEntry> FileBrowser::getSelectedEntries() const {
    std::vector<FileEntry> selected;
    for (const auto& entry : entries_) {
        if (entry.isSelected) {
            selected.push_back(entry);
        }
    }
    return selected;
}

bool FileBrowser::selectEntry(const std::string& path) {
    for (auto& entry : entries_) {
        if (entry.path == path) {
            entry.isSelected = true;
            return true;
        }
    }
    return false;
}

bool FileBrowser::deselectEntry(const std::string& path) {
    for (auto& entry : entries_) {
        if (entry.path == path) {
            entry.isSelected = false;
            return true;
        }
    }
    return false;
}

void FileBrowser::selectAll() {
    for (auto& entry : entries_) {
        if (!entry.isDirectory) {
            entry.isSelected = true;
        }
    }
}

void FileBrowser::deselectAll() {
    for (auto& entry : entries_) {
        entry.isSelected = false;
    }
}

void FileBrowser::setFilter(const std::string& extension) {
    filter_ = extension;
    refresh();
}

void FileBrowser::clearFilter() {
    filter_.clear();
    refresh();
}

bool FileBrowser::matchesFilter(const std::string& filename) {
    if (filter_.empty()) return true;
    
    // Split filter by comma
    size_t pos = 0;
    std::string filterCopy = filter_;
    while ((pos = filterCopy.find(',')) != std::string::npos) {
        std::string ext = filterCopy.substr(0, pos);
        if (filename.find(ext) != std::string::npos) {
            return true;
        }
        filterCopy.erase(0, pos + 1);
    }
    
    return filename.find(filterCopy) != std::string::npos;
}

void FileBrowser::setSearchQuery(const std::string& query) {
    searchQuery_ = query;
    refresh();
}

bool FileBrowser::matchesSearch(const std::string& filename) {
    if (searchQuery_.empty()) return true;
    
    // Case-insensitive search
    std::string lowerFilename = filename;
    std::string lowerQuery = searchQuery_;
    
    std::transform(lowerFilename.begin(), lowerFilename.end(), lowerFilename.begin(), ::tolower);
    std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);
    
    return lowerFilename.find(lowerQuery) != std::string::npos;
}

bool FileBrowser::connectSMB(const std::string& server, const std::string& share, 
                             const std::string& username, const std::string& password) {
    std::string uri = "smb://" + server + "/" + share;
    
    // Mount via GIO
    std::string cmd = "gio mount " + uri;
    if (!username.empty()) {
        cmd += " -u " + username;
    }
    
    int result = system(cmd.c_str());
    
    if (result == 0) {
        connectionType_ = "smb";
        isConnected_ = true;
        currentPath_ = uri;
        return browseSMB("/");
    }
    
    return false;
}

bool FileBrowser::connectFTP(const std::string& server, int port,
                             const std::string& username, const std::string& password) {
    std::string uri = "ftp://";
    if (!username.empty()) {
        uri += username;
        if (!password.empty()) {
            uri += ":" + password;
        }
        uri += "@";
    }
    uri += server;
    if (port != 21) {
        uri += ":" + std::to_string(port);
    }
    uri += "/";
    
    // Mount via GIO
    std::string cmd = "gio mount " + uri;
    int result = system(cmd.c_str());
    
    if (result == 0) {
        connectionType_ = "ftp";
        isConnected_ = true;
        currentPath_ = uri;
        return browseFTP("/");
    }
    
    return false;
}

bool FileBrowser::mountGIO(const std::string& uri) {
    std::string cmd = "gio mount " + uri;
    int result = system(cmd.c_str());
    
    if (result == 0) {
        connectionType_ = "gio";
        isConnected_ = true;
        currentPath_ = uri;
        return browseGIO("/");
    }
    
    return false;
}

void FileBrowser::disconnect() {
    connectionType_.clear();
    isConnected_ = false;
    connectionHandle_ = nullptr;
}
