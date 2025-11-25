# 🎵 SongGen - KI-gestützter Song Generator

Ein leistungsstarker, KI-basierter Song-Generator mit umfassender Mediendatenbank, Audio-Analyse und Netzwerk-Datei-Browser.

## ✨ Features

### 🗂️ Mediendatenbank
- SQLite-basierte Datenbank mit 20+ Metadaten-Feldern
- Genre, BPM, Intensität, Bass-Level, Instrumente
- Spektrale Features (Zentroid, Rolloff, Flux)
- MFCC-basierter Audio-Fingerprint
- Volltext-Suche und Filter

### 🎵 Audio-Playback
- SDL2-basierter Audio-Player
- Play/Pause/Stop Controls
- Seek-Slider für Navigation
- Volume-Control (0-100%)
- Echtzeit-Fortschrittsanzeige

### 📊 Audio-Analyse
- FFT-basierte Spektralanalyse (FFTW3 + Fallback)
- BPM-Erkennung via Autocorrelation (60-200 BPM)
- MFCC-Extraktion (26 Mel-Filter)
- Multi-Feature Genre-Klassifikation (10+ Genres):
  - Trap, Techno, Trance, House, Dubstep
  - Rock, Metal, Pop, Klassik, Electronic
- Instrument-Erkennung (Bass, Drums, Synth, Guitar, etc.)

### 🎹 Song-Generator
- Genre-basierte Melodie-Synthese mit Tonleitern
- Professionelle Rhythmus-Pattern:
  - Kick Drum (Beat 1 & 3)
  - Snare mit Noise-Synthese (Beat 2 & 4)
  - Hi-Hat für Techno/Trap/Trance
  - Genre-spezifische Claps
- Instrument-Layering aus Datenbank
- Formant-basierte Vocal-Synthese
- WAV-Export mit Normalisierung

### 📁 File-Browser
- Lokale Dateiverwaltung
- Netzwerk-Unterstützung via GIO/GVFS:
  - SMB/CIFS (Windows-Shares)
  - FTP/FTPS
  - GIO-Mount-Browser
- Mehrfachauswahl und Batch-Import
- Drag & Drop Zone
- Datei-Filter (.wav, .mp3, .flac, .ogg)

### 🎮 HVSC SID-Integration
- Automatischer Download des HVSC-Archivs
- Mirror-Speed-Testing (3 Mirrors)
- Batch-Konvertierung von SID → WAV (200 Threads)
- Automatische Datenbank-Integration
- Fortschritts-Tracking mit Echtzeit-Speed

### 🎨 Benutzeroberfläche
- ImGui-basierte Modern UI mit Dark Theme
- 6 Tabs: Datenbank, Browser, Analyse, Generator, HVSC, Settings
- Keyboard-Shortcuts:
  - `1-6`: Tab-Wechsel
  - `Space`: Play/Pause
  - `ESC`: Stop
  - `Ctrl+Q`: Beenden
- Status-Bar mit DB-Stats, Playback-Info, FPS
- Drag & Drop Support
- Runde Ecken und moderne Farbpalette

## 🔧 Technische Details

### Abhängigkeiten
- **C++17** Standard
- **CMake 3.16+** Build-System
- **SDL2 2.32.58** - UI & Audio
- **SQLite3 3.51.0** - Datenbank
- **libsidplayfp 2.15.2** - SID-Konvertierung
- **CURL 8.17.0** - HTTP-Downloads
- **ImGui (docking)** - User Interface
- **FFTW3 3.3.10** (optional) - FFT
- **libsamplerate 0.2.2** (optional) - Resampling
- **GIO/GVFS** - Netzwerk-Dateisystem (Linux)

### Architektur
```
SongGen/
├── include/
│   ├── MediaDatabase.h      - SQLite-Backend
│   ├── AudioAnalyzer.h      - FFT, BPM, MFCC, Genre
│   ├── AudioPlayer.h        - SDL Audio Playback
│   ├── FileBrowser.h        - Local + Network
│   ├── SongGenerator.h      - AI Composition
│   ├── HVSCDownloader.h     - HVSC Integration
│   ├── SIDLibConverter.h    - SID → WAV
│   └── ImGuiRenderer.h      - Main UI
├── src/
│   ├── *.cpp                - Implementierungen
│   └── main_imgui.cpp       - Entry Point
├── imgui_src/               - ImGui Library
└── CMakeLists.txt           - Build Config
```

### Datenbank-Schema
```sql
CREATE TABLE media (
    id INTEGER PRIMARY KEY,
    filepath TEXT UNIQUE,
    title TEXT,
    artist TEXT,
    bpm REAL,
    duration REAL,
    genre TEXT,
    subgenre TEXT,
    intensity TEXT,
    bassLevel TEXT,
    instruments TEXT,
    melodySignature TEXT,
    rhythmPattern TEXT,
    spectralCentroid REAL,
    spectralRolloff REAL,
    spectralFlux REAL,
    zeroCrossingRate REAL,
    mfccHash TEXT,
    added DATETIME,
    lastPlayed DATETIME,
    playCount INTEGER,
    analyzed BOOLEAN
);
```

## 🚀 Build & Installation

### Voraussetzungen (Arch Linux)
```bash
sudo pacman -S cmake gcc sdl2 sqlite curl libsidplayfp fftw libsamplerate gvfs openvino
```

### Kompilieren
```bash
cd /home/nex/c++/SongGen
mkdir -p build
cd build
cmake ..
make -j8
```

### Ausführen
```bash
./songgen
```

## 📖 Verwendung

### 1. Mediendateien hinzufügen
- Tab "Browser" (Taste `2`) öffnen
- Zu Audio-Ordner navigieren
- Dateien auswählen (oder "Alle Auswählen")
- "Zu Datenbank hinzufügen" klicken

### 2. Audio analysieren
- Tab "Analyse" (Taste `3`) öffnen
- "Alle unanalysierten Dateien verarbeiten" klicken
- Fortschritt beobachten

### 3. Songs generieren
- Tab "Generator" (Taste `4`) öffnen
- Genre auswählen (Trap, Techno, Rock, etc.)
- BPM, Intensität, Bass-Level einstellen
- "Song generieren" klicken
- WAV-Datei wird in ~/Music/SongGen/ gespeichert

### 4. HVSC SID-Archive nutzen
- Tab "HVSC" (Taste `5`) öffnen
- "HVSC herunterladen" klicken
- Automatischer Download + Konvertierung + DB-Import

### 5. Netzwerk-Shares durchsuchen
- Tab "Browser" (Taste `2`)
- "SMB verbinden" oder "FTP verbinden"
- Server/Share eingeben
- Credentials (optional)
- Mount → Durchsuchen

## 🎯 Performance

- **Compilation**: ~8 Sekunden (8 Threads)
- **Binary-Größe**: 1.7 MB
- **FFT**: 8192 Samples (FFTW3) oder 2048 (Fallback)
- **BPM-Erkennung**: Autocorrelation, ~1 Sekunde pro Song
- **SID-Konvertierung**: 200 Threads parallel
- **UI-Framerate**: 60+ FPS (VSync)

## 🎨 Keyboard-Shortcuts

| Taste | Aktion |
|-------|--------|
| `1-6` | Tab-Wechsel (Datenbank, Browser, Analyse, Generator, HVSC, Settings) |
| `Space` | Play/Pause |
| `ESC` | Stop Playback |
| `Ctrl+Q` | Programm beenden |

## 🔮 Geplante Features

- [ ] NPU-Integration für ML-basierte Features
- [ ] MP3/FLAC-Support via libsndfile
- [ ] Erweiterte Vocal-Synthese
- [ ] Direct libsmbclient Integration
- [ ] libzip für HVSC-Extraktion
- [ ] Advanced Instrument-Layering
- [ ] Real-time Audio-Effekte
- [ ] Export zu MP3/FLAC

## 📄 Lizenz

Dieses Projekt wurde als KI-gestützter Song-Generator entwickelt und steht zur freien Verwendung.

## 🙏 Credits

- **ImGui** - Dear ImGui (docking branch)
- **SDL2** - Simple DirectMedia Layer
- **SQLite** - Public Domain Database
- **libsidplayfp** - C64 SID Emulation
- **FFTW3** - Fastest Fourier Transform in the West
- **HVSC** - High Voltage SID Collection

---

**Version**: 1.0.0  
**Build**: 24. November 2025  
**Entwickelt mit**: C++17, CMake, SDL2, ImGui
