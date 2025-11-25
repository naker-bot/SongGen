#ifndef AUDIOPLAYER_H
#define AUDIOPLAYER_H

#include <string>
#include <mpv/client.h>
#include <atomic>
#include <thread>
#include <mutex>

/**
 * AudioPlayer - libmpv-basierter Audio-Player für Datenbank-Browser
 * 
 * Features:
 * - Play/Pause/Stop
 * - Volume-Control
 * - Position-Seeking
 * - Unterstützt alle Formate die mpv kann (MP3, WAV, FLAC, OGG, MP4, etc.)
 */
class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();
    
    bool initialize();
    void shutdown();
    
    // Playback-Control
    bool load(const std::string& filepath);
    void play();
    void pause();
    void stop();
    bool isPlaying() const { return isPlaying_; }
    bool isPaused() const { return isPaused_; }
    
    // Position-Control
    float getPosition() const;  // Sekunden
    float getDuration() const;  // Sekunden
    void seek(float seconds);
    
    // Volume-Control (0.0 - 1.0)
    void setVolume(float volume);
    float getVolume() const { return volume_; }
    
    // Audio-Effekte
    void setCompressor(float threshold, float ratio);  // Softener/Hardener
    void setFadeIn(float durationSec);
    void setFadeOut(float durationSec);
    void setSoloChannel(int channel);  // 0=Stereo, 1=Left, 2=Right
    void setNormalization(bool enabled);  // Auto-Lautstärke
    
    // Geschwindigkeit und Tonhöhe
    void setSpeed(float speed);  // 0.5 = halb so schnell, 2.0 = doppelt so schnell
    void setPitch(int semitones);  // -12 bis +12 Halbtonschritte
    void setPreserveVocals(bool enabled);  // Vokal-Erhaltung bei Pitch-Shift
    
    // Effekt-Parameter abrufen
    float getCompressorThreshold() const { return compressorThreshold_; }
    float getCompressorRatio() const { return compressorRatio_; }
    float getFadeInDuration() const { return fadeInDuration_; }
    float getFadeOutDuration() const { return fadeOutDuration_; }
    int getSoloChannel() const { return soloChannel_; }
    bool getNormalization() const { return normalizationEnabled_; }
    float getSpeed() const { return speed_; }
    int getPitch() const { return pitchSemitones_; }
    bool getPreserveVocals() const { return preserveVocals_; }
    
private:
    mpv_handle* mpv_ = nullptr;
    
    std::atomic<bool> isPlaying_{false};
    std::atomic<bool> isPaused_{false};
    std::atomic<float> volume_{0.5f};
    
    // Audio-Effekt Parameter
    std::atomic<float> compressorThreshold_{-20.0f};  // dB
    std::atomic<float> compressorRatio_{4.0f};  // 1:1 (soft) bis 20:1 (hard)
    std::atomic<float> fadeInDuration_{0.0f};  // Sekunden
    std::atomic<float> fadeOutDuration_{0.0f};  // Sekunden
    std::atomic<int> soloChannel_{0};  // 0=Stereo, 1=Left, 2=Right
    std::atomic<bool> normalizationEnabled_{false};
    std::atomic<float> speed_{1.0f};  // Wiedergabe-Geschwindigkeit
    std::atomic<int> pitchSemitones_{0};  // Tonhöhen-Verschiebung
    std::atomic<bool> preserveVocals_{true};  // Vokal-Erhaltung
    
    std::string currentFile_;
    std::mutex mpvMutex_;
    std::thread eventThread_;
    std::atomic<bool> stopEventThread_{false};
    
    // Event handling
    void processEvents();
};

#endif // AUDIOPLAYER_H
