#include "MediaDatabase.h"
#include <filesystem>
#include <cstring>
#include <ctime>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

MediaDatabase::MediaDatabase(const std::string& dbPath) : dbPath_(expandPath(dbPath)) {
}

MediaDatabase::~MediaDatabase() {
    if (db_) {
        sqlite3_close(db_);
    }
}

std::string MediaDatabase::expandPath(const std::string& path) {
    if (path.empty()) return path;
    if (path[0] == '~') {
        const char* home = getenv("HOME");
        if (home) {
            return std::string(home) + path.substr(1);
        }
    }
    return path;
}

bool MediaDatabase::initialize() {
    std::lock_guard<std::mutex> lock(dbMutex_);
    
    // Erstelle Verzeichnis falls nötig
    fs::path dbFilePath(dbPath_);
    fs::create_directories(dbFilePath.parent_path());
    
    // Öffne Datenbank
    int rc = sqlite3_open(dbPath_.c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db_) << std::endl;
        return false;
    }
    
    // Erstelle Tabelle
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS media (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            filepath TEXT UNIQUE NOT NULL,
            title TEXT,
            artist TEXT,
            bpm REAL DEFAULT 0.0,
            duration REAL DEFAULT 0.0,
            genre TEXT,
            subgenre TEXT,
            genreTags TEXT,
            intensity TEXT,
            bassLevel TEXT,
            mood TEXT,
            instruments TEXT,
            melodySignature TEXT,
            rhythmPattern TEXT,
            spectralCentroid REAL DEFAULT 0.0,
            spectralRolloff REAL DEFAULT 0.0,
            zeroCrossingRate REAL DEFAULT 0.0,
            mfccHash REAL DEFAULT 0.0,
            addedTimestamp INTEGER,
            lastUsed INTEGER DEFAULT 0,
            useCount INTEGER DEFAULT 0,
            analyzed INTEGER DEFAULT 0,
            introStart REAL DEFAULT 0.0,
            introDuration REAL DEFAULT 0.0,
            verseStart REAL DEFAULT 0.0,
            verseDuration REAL DEFAULT 0.0,
            chorusStart REAL DEFAULT 0.0,
            chorusDuration REAL DEFAULT 0.0,
            bridgeStart REAL DEFAULT 0.0,
            bridgeDuration REAL DEFAULT 0.0,
            soloStart REAL DEFAULT 0.0,
            soloDuration REAL DEFAULT 0.0,
            outroStart REAL DEFAULT 0.0,
            outroDuration REAL DEFAULT 0.0,
            structurePattern TEXT,
            energyCurve TEXT
        );
        
        CREATE INDEX IF NOT EXISTS idx_genre ON media(genre);
        CREATE INDEX IF NOT EXISTS idx_intensity ON media(intensity);
        CREATE INDEX IF NOT EXISTS idx_bpm ON media(bpm);
        CREATE INDEX IF NOT EXISTS idx_analyzed ON media(analyzed);
        
        CREATE TABLE IF NOT EXISTS training_decisions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            question TEXT NOT NULL,
            options TEXT NOT NULL,
            userAnswer TEXT NOT NULL,
            confidence REAL DEFAULT 0.0,
            context TEXT,
            timestamp INTEGER NOT NULL,
            decisionType TEXT,
            answered INTEGER DEFAULT 0,
            audioFile TEXT
        );
        
        CREATE INDEX IF NOT EXISTS idx_decision_type ON training_decisions(decisionType);
        CREATE INDEX IF NOT EXISTS idx_timestamp ON training_decisions(timestamp);
    )";
    
    if (!executeSQL(sql)) {
        std::cerr << "Failed to create tables" << std::endl;
        return false;
    }
    
    // Migrate existing database - add new columns if they don't exist
    const char* migrationSQL = R"(
        ALTER TABLE training_decisions ADD COLUMN answered INTEGER DEFAULT 0;
        ALTER TABLE training_decisions ADD COLUMN audioFile TEXT;
        ALTER TABLE media ADD COLUMN genreTags TEXT;
    )";
    
    // Execute migration (ignore errors if columns already exist)
    char* errMsg = nullptr;
    sqlite3_exec(db_, migrationSQL, nullptr, nullptr, &errMsg);
    if (errMsg) {
        // Ignore "duplicate column name" errors
        sqlite3_free(errMsg);
    }
    
    std::cout << "✅ Database initialized: " << dbPath_ << std::endl;
    return true;
}

bool MediaDatabase::executeSQL(const std::string& sql) {
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

sqlite3_stmt* MediaDatabase::prepareStatement(const std::string& sql) {
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db_) << std::endl;
        return nullptr;
    }
    return stmt;
}

bool MediaDatabase::addMedia(const MediaMetadata& meta) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    
    // Prüfe ob Datei bereits existiert (Duplikat-Vermeidung)
    const char* checkSql = "SELECT COUNT(*) FROM media WHERE filepath = ?";
    sqlite3_stmt* checkStmt = prepareStatement(checkSql);
    if (!checkStmt) return false;
    
    sqlite3_bind_text(checkStmt, 1, meta.filepath.c_str(), -1, SQLITE_TRANSIENT);
    
    bool exists = false;
    if (sqlite3_step(checkStmt) == SQLITE_ROW) {
        exists = (sqlite3_column_int(checkStmt, 0) > 0);
    }
    sqlite3_finalize(checkStmt);
    
    if (exists) {
        return false;  // Duplikat gefunden, nicht hinzufügen
    }
    
    const char* sql = R"(
        INSERT INTO media (
            filepath, title, artist, bpm, duration, genre, subgenre, intensity, bassLevel, mood,
            instruments, melodySignature, rhythmPattern, spectralCentroid, spectralRolloff,
            zeroCrossingRate, mfccHash, addedTimestamp, analyzed
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )";
    
    sqlite3_stmt* stmt = prepareStatement(sql);
    if (!stmt) return false;
    
    int64_t timestamp = meta.addedTimestamp ? meta.addedTimestamp : std::time(nullptr);
    
    sqlite3_bind_text(stmt, 1, meta.filepath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, meta.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, meta.artist.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 4, meta.bpm);
    sqlite3_bind_double(stmt, 5, meta.duration);
    sqlite3_bind_text(stmt, 6, meta.genre.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, meta.subgenre.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, meta.intensity.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, meta.bassLevel.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 10, meta.mood.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 11, meta.instruments.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 12, meta.melodySignature.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 13, meta.rhythmPattern.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 14, meta.spectralCentroid);
    sqlite3_bind_double(stmt, 15, meta.spectralRolloff);
    sqlite3_bind_double(stmt, 16, meta.zeroCrossingRate);
    sqlite3_bind_double(stmt, 17, meta.mfccHash);
    sqlite3_bind_int64(stmt, 18, timestamp);
    sqlite3_bind_int(stmt, 19, meta.analyzed ? 1 : 0);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

std::vector<MediaMetadata> MediaDatabase::getAll() {
    std::lock_guard<std::mutex> lock(dbMutex_);
    
    std::vector<MediaMetadata> results;
    const char* sql = "SELECT * FROM media ORDER BY addedTimestamp DESC";
    
    sqlite3_stmt* stmt = prepareStatement(sql);
    if (!stmt) return results;
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        MediaMetadata meta;
        meta.id = sqlite3_column_int64(stmt, 0);
        meta.filepath = (const char*)sqlite3_column_text(stmt, 1);
        meta.title = (const char*)sqlite3_column_text(stmt, 2);
        meta.artist = (const char*)sqlite3_column_text(stmt, 3);
        meta.bpm = sqlite3_column_double(stmt, 4);
        meta.duration = sqlite3_column_double(stmt, 5);
        meta.genre = (const char*)sqlite3_column_text(stmt, 6);
        meta.subgenre = (const char*)sqlite3_column_text(stmt, 7);
        meta.intensity = (const char*)sqlite3_column_text(stmt, 8);
        meta.bassLevel = (const char*)sqlite3_column_text(stmt, 9);
        meta.mood = (const char*)sqlite3_column_text(stmt, 10);
        meta.instruments = (const char*)sqlite3_column_text(stmt, 11);
        meta.melodySignature = (const char*)sqlite3_column_text(stmt, 12);
        meta.rhythmPattern = (const char*)sqlite3_column_text(stmt, 13);
        meta.spectralCentroid = sqlite3_column_double(stmt, 14);
        meta.spectralRolloff = sqlite3_column_double(stmt, 15);
        meta.zeroCrossingRate = sqlite3_column_double(stmt, 16);
        meta.mfccHash = sqlite3_column_double(stmt, 17);
        meta.addedTimestamp = sqlite3_column_int64(stmt, 18);
        meta.lastUsed = sqlite3_column_int64(stmt, 19);
        meta.useCount = sqlite3_column_int(stmt, 20);
        meta.analyzed = sqlite3_column_int(stmt, 21) != 0;
        
        results.push_back(meta);
    }
    
    sqlite3_finalize(stmt);
    return results;
}

std::vector<MediaMetadata> MediaDatabase::searchByGenre(const std::string& genre) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    
    std::vector<MediaMetadata> results;
    const char* sql = "SELECT * FROM media WHERE genre = ? ORDER BY bpm";
    
    sqlite3_stmt* stmt = prepareStatement(sql);
    if (!stmt) return results;
    
    sqlite3_bind_text(stmt, 1, genre.c_str(), -1, SQLITE_TRANSIENT);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        MediaMetadata meta;
        meta.id = sqlite3_column_int64(stmt, 0);
        meta.filepath = (const char*)sqlite3_column_text(stmt, 1);
        meta.genre = (const char*)sqlite3_column_text(stmt, 6);
        meta.bpm = sqlite3_column_double(stmt, 4);
        meta.intensity = (const char*)sqlite3_column_text(stmt, 8);
        meta.bassLevel = (const char*)sqlite3_column_text(stmt, 9);
        // ... fülle restliche Felder bei Bedarf
        
        results.push_back(meta);
    }
    
    sqlite3_finalize(stmt);
    return results;
}

std::vector<MediaMetadata> MediaDatabase::getUnanalyzed() {
    std::lock_guard<std::mutex> lock(dbMutex_);
    
    std::vector<MediaMetadata> results;
    const char* sql = "SELECT * FROM media WHERE analyzed = 0";
    
    sqlite3_stmt* stmt = prepareStatement(sql);
    if (!stmt) return results;
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        MediaMetadata meta;
        meta.id = sqlite3_column_int64(stmt, 0);
        meta.filepath = (const char*)sqlite3_column_text(stmt, 1);
        meta.analyzed = false;
        results.push_back(meta);
    }
    
    sqlite3_finalize(stmt);
    return results;
}

size_t MediaDatabase::getTotalCount() {
    std::lock_guard<std::mutex> lock(dbMutex_);
    
    const char* sql = "SELECT COUNT(*) FROM media";
    sqlite3_stmt* stmt = prepareStatement(sql);
    if (!stmt) return 0;
    
    size_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int64(stmt, 0);
    }
    
    sqlite3_finalize(stmt);
    return count;
}

std::vector<std::string> MediaDatabase::getAllGenres() {
    std::lock_guard<std::mutex> lock(dbMutex_);
    
    std::vector<std::string> genres;
    const char* sql = "SELECT DISTINCT genre FROM media WHERE genre != '' ORDER BY genre";
    
    sqlite3_stmt* stmt = prepareStatement(sql);
    if (!stmt) return genres;
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        genres.emplace_back((const char*)sqlite3_column_text(stmt, 0));
    }
    
    sqlite3_finalize(stmt);
    return genres;
}

bool MediaDatabase::updateMedia(const MediaMetadata& meta) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    
    const char* sql = R"(
        UPDATE media SET
            title = ?, artist = ?, bpm = ?, duration = ?, genre = ?, subgenre = ?,
            intensity = ?, bassLevel = ?, mood = ?, instruments = ?, melodySignature = ?,
            rhythmPattern = ?, spectralCentroid = ?, spectralRolloff = ?, zeroCrossingRate = ?,
            mfccHash = ?, analyzed = ?
        WHERE filepath = ?
    )";
    
    sqlite3_stmt* stmt = prepareStatement(sql);
    if (!stmt) {
        std::cerr << "❌ Fehler beim Vorbereiten des UPDATE Statements!" << std::endl;
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, meta.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, meta.artist.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 3, meta.bpm);
    sqlite3_bind_double(stmt, 4, meta.duration);
    sqlite3_bind_text(stmt, 5, meta.genre.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, meta.subgenre.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, meta.intensity.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, meta.bassLevel.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, meta.mood.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 10, meta.instruments.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 11, meta.melodySignature.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 12, meta.rhythmPattern.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 13, meta.spectralCentroid);
    sqlite3_bind_double(stmt, 14, meta.spectralRolloff);
    sqlite3_bind_double(stmt, 15, meta.zeroCrossingRate);
    sqlite3_bind_double(stmt, 16, meta.mfccHash);
    sqlite3_bind_int(stmt, 17, meta.analyzed ? 1 : 0);
    sqlite3_bind_text(stmt, 18, meta.filepath.c_str(), -1, SQLITE_TRANSIENT);
    
    int rc = sqlite3_step(stmt);
    
    if (rc != SQLITE_DONE) {
        std::cerr << "❌ SQLite Fehler beim UPDATE: " << sqlite3_errmsg(db_) << " (Code: " << rc << ")" << std::endl;
        sqlite3_finalize(stmt);
        return false;
    }
    
    int changedRows = sqlite3_changes(db_);
    sqlite3_finalize(stmt);
    
    if (changedRows == 0) {
        std::cout << "⚠️ Keine Zeilen geändert - Datei noch nicht in Datenbank. Füge hinzu..." << std::endl;
        return addMedia(meta);
    }
    
    std::cout << "✅ Erfolgreich aktualisiert: " << meta.filepath << std::endl;
    return true;
}

bool MediaDatabase::deleteMedia(int64_t id) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    
    const char* sql = "DELETE FROM media WHERE id = ?";
    sqlite3_stmt* stmt = prepareStatement(sql);
    if (!stmt) return false;
    
    sqlite3_bind_int64(stmt, 1, id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

bool MediaDatabase::existsByPath(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    
    const char* sql = "SELECT COUNT(*) FROM media WHERE filepath = ?";
    sqlite3_stmt* stmt = prepareStatement(sql);
    if (!stmt) return false;
    
    sqlite3_bind_text(stmt, 1, filepath.c_str(), -1, SQLITE_TRANSIENT);
    
    bool exists = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        exists = (sqlite3_column_int(stmt, 0) > 0);
    }
    
    sqlite3_finalize(stmt);
    return exists;
}

// ===== SORTIERTE & ABSPIELFERTIGE ABFRAGEN =====

std::vector<MediaMetadata> MediaDatabase::getAllSortedByGenre() {
    std::lock_guard<std::mutex> lock(dbMutex_);
    std::vector<MediaMetadata> results;
    
    const char* sql = "SELECT * FROM media WHERE analyzed = 1 ORDER BY genre, bpm";
    sqlite3_stmt* stmt = prepareStatement(sql);
    if (!stmt) return results;
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        MediaMetadata meta;
        meta.id = sqlite3_column_int64(stmt, 0);
        meta.filepath = (const char*)sqlite3_column_text(stmt, 1);
        meta.title = (const char*)sqlite3_column_text(stmt, 2);
        meta.artist = (const char*)sqlite3_column_text(stmt, 3);
        meta.bpm = sqlite3_column_double(stmt, 4);
        meta.duration = sqlite3_column_double(stmt, 5);
        meta.genre = (const char*)sqlite3_column_text(stmt, 6);
        meta.subgenre = (const char*)sqlite3_column_text(stmt, 7);
        meta.intensity = (const char*)sqlite3_column_text(stmt, 8);
        meta.bassLevel = (const char*)sqlite3_column_text(stmt, 9);
        meta.mood = (const char*)sqlite3_column_text(stmt, 10);
        meta.analyzed = true;
        results.push_back(meta);
    }
    
    sqlite3_finalize(stmt);
    return results;
}

std::vector<MediaMetadata> MediaDatabase::getAllSortedByBPM() {
    std::lock_guard<std::mutex> lock(dbMutex_);
    std::vector<MediaMetadata> results;
    
    const char* sql = "SELECT * FROM media WHERE analyzed = 1 ORDER BY bpm, genre";
    sqlite3_stmt* stmt = prepareStatement(sql);
    if (!stmt) return results;
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        MediaMetadata meta;
        meta.id = sqlite3_column_int64(stmt, 0);
        meta.filepath = (const char*)sqlite3_column_text(stmt, 1);
        meta.bpm = sqlite3_column_double(stmt, 4);
        meta.genre = (const char*)sqlite3_column_text(stmt, 6);
        meta.analyzed = true;
        results.push_back(meta);
    }
    
    sqlite3_finalize(stmt);
    return results;
}

std::vector<MediaMetadata> MediaDatabase::getAllSortedByMood() {
    std::lock_guard<std::mutex> lock(dbMutex_);
    std::vector<MediaMetadata> results;
    
    const char* sql = "SELECT * FROM media WHERE analyzed = 1 AND mood != '' ORDER BY mood, genre";
    sqlite3_stmt* stmt = prepareStatement(sql);
    if (!stmt) return results;
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        MediaMetadata meta;
        meta.id = sqlite3_column_int64(stmt, 0);
        meta.filepath = (const char*)sqlite3_column_text(stmt, 1);
        meta.genre = (const char*)sqlite3_column_text(stmt, 6);
        meta.mood = (const char*)sqlite3_column_text(stmt, 10);
        meta.analyzed = true;
        results.push_back(meta);
    }
    
    sqlite3_finalize(stmt);
    return results;
}

std::vector<MediaMetadata> MediaDatabase::getPlayableByCategory(const std::string& category) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    std::vector<MediaMetadata> results;
    
    // Suche nach Genre oder Mood-Tag
    const char* sql = "SELECT * FROM media WHERE analyzed = 1 AND (genre LIKE ? OR mood LIKE ?) ORDER BY bpm";
    sqlite3_stmt* stmt = prepareStatement(sql);
    if (!stmt) return results;
    
    std::string pattern = "%" + category + "%";
    sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, pattern.c_str(), -1, SQLITE_TRANSIENT);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        MediaMetadata meta;
        meta.id = sqlite3_column_int64(stmt, 0);
        meta.filepath = (const char*)sqlite3_column_text(stmt, 1);
        meta.bpm = sqlite3_column_double(stmt, 4);
        meta.duration = sqlite3_column_double(stmt, 5);
        meta.genre = (const char*)sqlite3_column_text(stmt, 6);
        meta.mood = (const char*)sqlite3_column_text(stmt, 10);
        meta.analyzed = true;
        results.push_back(meta);
    }
    
    sqlite3_finalize(stmt);
    return results;
}

std::map<std::string, std::vector<MediaMetadata>> MediaDatabase::getGroupedByGenre() {
    auto allFiles = getAllSortedByGenre();
    std::map<std::string, std::vector<MediaMetadata>> grouped;
    
    for (const auto& file : allFiles) {
        if (!file.genre.empty()) {
            grouped[file.genre].push_back(file);
        }
    }
    
    return grouped;
}

std::map<std::string, std::vector<MediaMetadata>> MediaDatabase::getGroupedByBPMRange() {
    auto allFiles = getAllSortedByBPM();
    std::map<std::string, std::vector<MediaMetadata>> grouped;
    
    for (const auto& file : allFiles) {
        std::string range;
        if (file.bpm < 90) range = "Slow (60-90)";
        else if (file.bpm < 120) range = "Medium (90-120)";
        else if (file.bpm < 140) range = "Fast (120-140)";
        else if (file.bpm < 160) range = "Very Fast (140-160)";
        else range = "Ultra Fast (160+)";
        
        grouped[range].push_back(file);
    }
    
    return grouped;
}

// === Duplikatserkennung ===

std::vector<MediaDatabase::DuplicateInfo> MediaDatabase::findDuplicates(float threshold) {
    std::vector<DuplicateInfo> duplicates;
    
    // Kombiniere verschiedene Erkennungsmethoden
    auto identicalDups = findIdenticalFiles();
    auto audioDups = findAudioDuplicates(threshold);
    
    duplicates.insert(duplicates.end(), identicalDups.begin(), identicalDups.end());
    duplicates.insert(duplicates.end(), audioDups.begin(), audioDups.end());
    
    return duplicates;
}

std::vector<MediaDatabase::DuplicateInfo> MediaDatabase::findIdenticalFiles() {
    std::lock_guard<std::mutex> lock(dbMutex_);
    std::vector<DuplicateInfo> duplicates;
    
    // Finde Dateien mit identischem Pfad (sollte nicht vorkommen, aber sicher ist sicher)
    // Oder gleicher Dauer + BPM + Titel
    std::string sql = R"(
        SELECT m1.id, m2.id, m1.filepath, m2.filepath, m1.title, m1.duration, m1.bpm
        FROM media m1
        JOIN media m2 ON m1.id < m2.id
        WHERE (m1.title = m2.title AND m1.artist = m2.artist AND ABS(m1.duration - m2.duration) < 0.1)
           OR (m1.filepath = m2.filepath)
        ORDER BY m1.id
    )";
    
    sqlite3_stmt* stmt = prepareStatement(sql);
    if (!stmt) return duplicates;
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        DuplicateInfo dup;
        dup.id1 = sqlite3_column_int64(stmt, 0);
        dup.id2 = sqlite3_column_int64(stmt, 1);
        dup.filepath1 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        dup.filepath2 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        dup.similarity = 1.0f;
        dup.reason = "identical_metadata";
        
        duplicates.push_back(dup);
    }
    
    sqlite3_finalize(stmt);
    return duplicates;
}

std::vector<MediaDatabase::DuplicateInfo> MediaDatabase::findAudioDuplicates(float mfccThreshold) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    std::vector<DuplicateInfo> duplicates;
    
    // Finde Dateien mit ähnlichen Audio-Features
    // MFCC-Hash, Spectral Centroid, Duration, BPM
    std::string sql = R"(
        SELECT m1.id, m2.id, m1.filepath, m2.filepath,
               m1.mfccHash, m2.mfccHash,
               m1.spectralCentroid, m2.spectralCentroid,
               m1.duration, m2.duration, m1.bpm, m2.bpm
        FROM media m1
        JOIN media m2 ON m1.id < m2.id
        WHERE m1.analyzed = 1 AND m2.analyzed = 1
          AND ABS(m1.duration - m2.duration) < 2.0
          AND ABS(m1.bpm - m2.bpm) < 2.0
        ORDER BY m1.id
    )";
    
    sqlite3_stmt* stmt = prepareStatement(sql);
    if (!stmt) return duplicates;
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int64_t id1 = sqlite3_column_int64(stmt, 0);
        int64_t id2 = sqlite3_column_int64(stmt, 1);
        std::string path1 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        std::string path2 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        
        float mfcc1 = sqlite3_column_double(stmt, 4);
        float mfcc2 = sqlite3_column_double(stmt, 5);
        float sc1 = sqlite3_column_double(stmt, 6);
        float sc2 = sqlite3_column_double(stmt, 7);
        
        // Berechne Ähnlichkeit
        float mfccSim = 1.0f - std::abs(mfcc1 - mfcc2) / std::max(std::abs(mfcc1), std::abs(mfcc2));
        float scSim = 1.0f - std::abs(sc1 - sc2) / std::max(sc1, sc2);
        float similarity = (mfccSim * 0.7f + scSim * 0.3f);
        
        if (similarity >= mfccThreshold) {
            DuplicateInfo dup;
            dup.id1 = id1;
            dup.id2 = id2;
            dup.filepath1 = path1;
            dup.filepath2 = path2;
            dup.similarity = similarity;
            dup.reason = "similar_audio";
            
            duplicates.push_back(dup);
        }
    }
    
    sqlite3_finalize(stmt);
    return duplicates;
}

bool MediaDatabase::removeDuplicate(int64_t keepId, int64_t removeId) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    
    std::string sql = "DELETE FROM media WHERE id = ?";
    sqlite3_stmt* stmt = prepareStatement(sql);
    if (!stmt) return false;
    
    sqlite3_bind_int64(stmt, 1, removeId);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

// === Training-Dataset Verwaltung ===

MediaDatabase::TrainingStats MediaDatabase::getTrainingStats() {
    std::lock_guard<std::mutex> lock(dbMutex_);
    TrainingStats stats;
    stats.totalFiles = 0;
    stats.analyzedFiles = 0;
    stats.duplicates = 0;
    stats.avgBpm = 0.0f;
    stats.avgDuration = 0.0f;
    stats.filesWithoutGenre = 0;
    
    // Total Count
    sqlite3_stmt* stmt = prepareStatement("SELECT COUNT(*), SUM(analyzed), AVG(bpm), AVG(duration) FROM media");
    if (stmt && sqlite3_step(stmt) == SQLITE_ROW) {
        stats.totalFiles = sqlite3_column_int(stmt, 0);
        stats.analyzedFiles = sqlite3_column_int(stmt, 1);
        stats.avgBpm = sqlite3_column_double(stmt, 2);
        stats.avgDuration = sqlite3_column_double(stmt, 3);
    }
    sqlite3_finalize(stmt);
    
    // Genre Distribution
    stmt = prepareStatement("SELECT genre, COUNT(*) FROM media WHERE genre != '' GROUP BY genre ORDER BY COUNT(*) DESC");
    if (stmt) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string genre = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            size_t count = sqlite3_column_int(stmt, 1);
            stats.genreDistribution[genre] = count;
        }
        sqlite3_finalize(stmt);
    }
    
    // Intensity Distribution
    stmt = prepareStatement("SELECT intensity, COUNT(*) FROM media WHERE intensity != '' GROUP BY intensity");
    if (stmt) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string intensity = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            size_t count = sqlite3_column_int(stmt, 1);
            stats.intensityDistribution[intensity] = count;
        }
        sqlite3_finalize(stmt);
    }
    
    // Files without genre
    stmt = prepareStatement("SELECT COUNT(*) FROM media WHERE genre = '' OR genre IS NULL");
    if (stmt && sqlite3_step(stmt) == SQLITE_ROW) {
        stats.filesWithoutGenre = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    
    // Estimate duplicates
    stats.duplicates = findIdenticalFiles().size();
    
    return stats;
}

std::vector<MediaMetadata> MediaDatabase::getTrainingSet(const std::string& genre, size_t maxSamples) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    std::vector<MediaMetadata> results;
    
    std::string sql = "SELECT * FROM media WHERE analyzed = 1";
    if (!genre.empty()) {
        sql += " AND genre = '" + genre + "'";
    }
    sql += " ORDER BY RANDOM()";
    if (maxSamples > 0) {
        sql += " LIMIT " + std::to_string(maxSamples);
    }
    
    sqlite3_stmt* stmt = prepareStatement(sql);
    if (!stmt) return results;
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        MediaMetadata meta;
        meta.id = sqlite3_column_int64(stmt, 0);
        meta.filepath = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        meta.analyzed = sqlite3_column_int(stmt, 21);
        // ... weitere Felder laden
        results.push_back(meta);
    }
    
    sqlite3_finalize(stmt);
    return results;
}

bool MediaDatabase::markAsTrainingData(int64_t id, bool isTraining) {
    // Aktuell werden alle Dateien als Training-Daten verwendet
    // Diese Funktion kann später erweitert werden um spezifische Datensätze zu markieren
    return true;
}

bool MediaDatabase::balanceDataset() {
    auto stats = getTrainingStats();
    
    // Finde das Genre mit den wenigsten Samples
    if (stats.genreDistribution.empty()) return false;
    
    size_t minCount = SIZE_MAX;
    for (const auto& [genre, count] : stats.genreDistribution) {
        if (count < minCount) minCount = count;
    }
    
    // TODO: Implementiere Balancing-Strategie
    // z.B. durch Duplikation seltener Genres oder Entfernung überhäufiger
    
    std::cout << "📊 Dataset Balance: " << std::endl;
    for (const auto& [genre, count] : stats.genreDistribution) {
        std::cout << "   " << genre << ": " << count << " (" 
                  << (100.0f * count / stats.totalFiles) << "%)" << std::endl;
    }
    
    return true;
}

// Interaktive Training-Entscheidungen
bool MediaDatabase::saveDecision(const TrainingDecision& decision) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    
    // Konvertiere options-Vector zu JSON-String
    std::string optionsJson = "[";
    for (size_t i = 0; i < decision.options.size(); ++i) {
        if (i > 0) optionsJson += ",";
        optionsJson += "\"" + decision.options[i] + "\"";
    }
    optionsJson += "]";
    
    const char* sql = R"(
        INSERT INTO training_decisions (question, options, userAnswer, confidence, context, timestamp, decisionType, answered, audioFile)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
    )";
    
    sqlite3_stmt* stmt = prepareStatement(sql);
    if (!stmt) return false;
    
    sqlite3_bind_text(stmt, 1, decision.question.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, optionsJson.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, decision.userAnswer.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 4, decision.confidence);
    sqlite3_bind_text(stmt, 5, decision.context.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 6, decision.timestamp);
    sqlite3_bind_text(stmt, 7, decision.decisionType.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 8, decision.answered ? 1 : 0);
    sqlite3_bind_text(stmt, 9, decision.audioFile.c_str(), -1, SQLITE_TRANSIENT);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

std::vector<MediaDatabase::TrainingDecision> MediaDatabase::getDecisionHistory(size_t limit) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    std::vector<TrainingDecision> decisions;
    
    std::string sql = "SELECT id, question, options, userAnswer, confidence, context, timestamp, decisionType "
                     "FROM training_decisions ORDER BY timestamp DESC";
    if (limit > 0) {
        sql += " LIMIT " + std::to_string(limit);
    }
    
    sqlite3_stmt* stmt = prepareStatement(sql);
    if (!stmt) return decisions;
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        TrainingDecision decision;
        decision.id = sqlite3_column_int64(stmt, 0);
        decision.question = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        
        // Parse options JSON
        std::string optionsJson = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        // Simple JSON parsing: ["opt1","opt2","opt3"]
        size_t pos = 1;  // Skip opening [
        while (pos < optionsJson.size() - 1) {
            if (optionsJson[pos] == '"') {
                size_t end = optionsJson.find('"', pos + 1);
                if (end != std::string::npos) {
                    decision.options.push_back(optionsJson.substr(pos + 1, end - pos - 1));
                    pos = end + 1;
                }
            }
            pos++;
        }
        
        decision.userAnswer = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        decision.confidence = sqlite3_column_double(stmt, 4);
        decision.context = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        decision.timestamp = sqlite3_column_int64(stmt, 6);
        decision.decisionType = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        
        decisions.push_back(decision);
    }
    
    sqlite3_finalize(stmt);
    return decisions;
}

std::vector<MediaDatabase::TrainingDecision> MediaDatabase::findSimilarDecisions(
    const std::string& context, float threshold) {
    // Für jetzt: Finde Entscheidungen mit gleichem decisionType
    // TODO: Implementiere Context-Similarity-Matching (JSON parsing + Vergleich)
    std::lock_guard<std::mutex> lock(dbMutex_);
    std::vector<TrainingDecision> decisions;
    
    // Extrahiere decisionType aus context (vereinfachte Version)
    std::string type = "unknown";
    if (context.find("\"decisionType\":") != std::string::npos) {
        size_t start = context.find("\"decisionType\":") + 17;
        size_t end = context.find("\"", start);
        if (end != std::string::npos) {
            type = context.substr(start, end - start);
        }
    }
    
    const char* sql = "SELECT id, question, options, userAnswer, confidence, context, timestamp, decisionType "
                     "FROM training_decisions WHERE decisionType = ? ORDER BY timestamp DESC LIMIT 10";
    
    sqlite3_stmt* stmt = prepareStatement(sql);
    if (!stmt) return decisions;
    
    sqlite3_bind_text(stmt, 1, type.c_str(), -1, SQLITE_TRANSIENT);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        TrainingDecision decision;
        decision.id = sqlite3_column_int64(stmt, 0);
        decision.question = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        
        // Parse options JSON (same as getDecisionHistory)
        std::string optionsJson = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        size_t pos = 1;
        while (pos < optionsJson.size() - 1) {
            if (optionsJson[pos] == '"') {
                size_t end = optionsJson.find('"', pos + 1);
                if (end != std::string::npos) {
                    decision.options.push_back(optionsJson.substr(pos + 1, end - pos - 1));
                    pos = end + 1;
                }
            }
            pos++;
        }
        
        decision.userAnswer = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        decision.confidence = sqlite3_column_double(stmt, 4);
        decision.context = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        decision.timestamp = sqlite3_column_int64(stmt, 6);
        decision.decisionType = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        
        decisions.push_back(decision);
    }
    
    sqlite3_finalize(stmt);
    return decisions;
}

std::vector<MediaDatabase::TrainingDecision> MediaDatabase::getUnansweredQuestions() {
    std::lock_guard<std::mutex> lock(dbMutex_);
    std::vector<TrainingDecision> questions;
    
    const char* sql = "SELECT id, question, options, userAnswer, confidence, context, timestamp, decisionType, audioFile "
                     "FROM training_decisions WHERE answered = 0 ORDER BY timestamp DESC";
    
    sqlite3_stmt* stmt = prepareStatement(sql);
    if (!stmt) return questions;
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        TrainingDecision decision;
        decision.id = sqlite3_column_int64(stmt, 0);
        decision.question = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        
        // Parse options JSON
        std::string optionsJson = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        size_t pos = 1;
        while (pos < optionsJson.size() - 1) {
            if (optionsJson[pos] == '"') {
                size_t end = optionsJson.find('"', pos + 1);
                if (end != std::string::npos) {
                    decision.options.push_back(optionsJson.substr(pos + 1, end - pos - 1));
                    pos = end + 1;
                }
            }
            pos++;
        }
        
        decision.userAnswer = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        decision.confidence = sqlite3_column_double(stmt, 4);
        decision.context = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        decision.timestamp = sqlite3_column_int64(stmt, 6);
        decision.decisionType = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        decision.answered = false;
        
        const char* audioFilePtr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        if (audioFilePtr) {
            decision.audioFile = audioFilePtr;
        }
        
        questions.push_back(decision);
    }
    
    sqlite3_finalize(stmt);
    return questions;
}

bool MediaDatabase::markQuestionAsAnswered(int64_t id, const std::string& answer) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    
    const char* sql = "UPDATE training_decisions SET answered = 1, userAnswer = ? WHERE id = ?";
    sqlite3_stmt* stmt = prepareStatement(sql);
    if (!stmt) return false;
    
    sqlite3_bind_text(stmt, 1, answer.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, id);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

bool MediaDatabase::deleteDecision(int64_t id) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    
    const char* sql = "DELETE FROM training_decisions WHERE id = ?";
    sqlite3_stmt* stmt = prepareStatement(sql);
    if (!stmt) return false;
    
    sqlite3_bind_int64(stmt, 1, id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}
