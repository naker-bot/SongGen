#include "AudioPlayer.h"
#include <iostream>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <clocale>

AudioPlayer::AudioPlayer() {
}

AudioPlayer::~AudioPlayer() {
    shutdown();
}

bool AudioPlayer::initialize() {
    // mpv benötigt C locale
    std::setlocale(LC_NUMERIC, "C");
    
    mpv_ = mpv_create();
    if (!mpv_) {
        std::cerr << "❌ mpv_create() failed" << std::endl;
        std::cerr << "   mpv version 0.40.0 gefunden - eventuell API-Inkompatibilität" << std::endl;
        std::cerr << "   Fallback: AudioPlayer läuft ohne mpv" << std::endl;
        return false;
    }
    
    // Konfiguration VOR mpv_initialize()
    mpv_set_option_string(mpv_, "video", "no");
    mpv_set_option_string(mpv_, "vo", "null");
    mpv_set_option_string(mpv_, "ao", "pulse,alsa,sdl");
    mpv_set_option_string(mpv_, "terminal", "no");
    mpv_set_option_string(mpv_, "msg-level", "all=no");
    mpv_set_option_string(mpv_, "input-default-bindings", "no");
    mpv_set_option_string(mpv_, "input-vo-keyboard", "no");
    mpv_set_option_string(mpv_, "osc", "no");
    
    // Initialisiere mpv
    int error = mpv_initialize(mpv_);
    if (error < 0) {
        std::cerr << "❌ mpv_initialize() failed: " << mpv_error_string(error) << std::endl;
        mpv_terminate_destroy(mpv_);
        mpv_ = nullptr;
        return false;
    }
    
    // Observe pause property für Event-Thread
    mpv_observe_property(mpv_, 0, "pause", MPV_FORMAT_FLAG);
    
    // Setze Standard-Lautstärke
    double vol = 50.0;
    mpv_set_property(mpv_, "volume", MPV_FORMAT_DOUBLE, &vol);
    
    // Starte Event-Thread
    stopEventThread_ = false;
    eventThread_ = std::thread(&AudioPlayer::processEvents, this);
    
    std::cout << "✅ libmpv Audio-Player initialisiert" << std::endl;
    return true;
}

void AudioPlayer::shutdown() {
    stop();
    
    // Stoppe Event-Thread
    stopEventThread_ = true;
    if (eventThread_.joinable()) {
        eventThread_.join();
    }
    
    if (mpv_) {
        mpv_terminate_destroy(mpv_);
        mpv_ = nullptr;
    }
}

bool AudioPlayer::load(const std::string& filepath) {
    if (!mpv_) return false;
    
    stop();
    
    std::lock_guard<std::mutex> lock(mpvMutex_);
    
    // Lade Datei mit mpv
    const char* cmd[] = {"loadfile", filepath.c_str(), nullptr};
    int error = mpv_command(mpv_, cmd);
    
    if (error < 0) {
        std::cerr << "❌ mpv_command loadfile failed: " << mpv_error_string(error) << std::endl;
        return false;
    }
    
    currentFile_ = filepath;
    
    // Pausiere direkt nach dem Laden
    int pause = 1;
    mpv_set_property(mpv_, "pause", MPV_FORMAT_FLAG, &pause);
    
    std::cout << "✅ Loaded: " << filepath << std::endl;
    return true;
}

void AudioPlayer::play() {
    if (!mpv_ || currentFile_.empty()) return;
    
    int pause = 0;
    mpv_set_property(mpv_, "pause", MPV_FORMAT_FLAG, &pause);
    isPlaying_ = true;
    isPaused_ = false;
}

void AudioPlayer::pause() {
    if (!mpv_) return;
    
    int pause_state = isPaused_ ? 0 : 1;
    mpv_set_property(mpv_, "pause", MPV_FORMAT_FLAG, &pause_state);
    isPaused_ = !isPaused_;
}

void AudioPlayer::stop() {
    if (!mpv_) return;
    
    const char* cmd[] = {"stop", nullptr};
    mpv_command(mpv_, cmd);
    isPlaying_ = false;
    isPaused_ = false;
}

float AudioPlayer::getPosition() const {
    if (!mpv_) return 0.0f;
    
    double pos = 0.0;
    mpv_get_property(mpv_, "time-pos", MPV_FORMAT_DOUBLE, &pos);
    return static_cast<float>(pos);
}

float AudioPlayer::getDuration() const {
    if (!mpv_) return 0.0f;
    
    double duration = 0.0;
    mpv_get_property(mpv_, "duration", MPV_FORMAT_DOUBLE, &duration);
    return static_cast<float>(duration);
}

void AudioPlayer::seek(float seconds) {
    if (!mpv_) return;
    
    double pos = static_cast<double>(seconds);
    mpv_set_property(mpv_, "time-pos", MPV_FORMAT_DOUBLE, &pos);
}

void AudioPlayer::setVolume(float volume) {
    volume_ = std::clamp(volume, 0.0f, 1.0f);
    
    if (mpv_) {
        double mpv_volume = volume_ * 100.0;  // mpv benutzt 0-100
        mpv_set_property_async(mpv_, 0, "volume", MPV_FORMAT_DOUBLE, &mpv_volume);
    }
}

void AudioPlayer::setCompressor(float threshold, float ratio) {
    compressorThreshold_ = threshold;
    compressorRatio_ = std::clamp(ratio, 1.0f, 20.0f);
    // mpv: af=acompressor=threshold=...
}

void AudioPlayer::setFadeIn(float durationSec) {
    fadeInDuration_ = std::max(0.0f, durationSec);
}

void AudioPlayer::setFadeOut(float durationSec) {
    fadeOutDuration_ = std::max(0.0f, durationSec);
}

void AudioPlayer::setSoloChannel(int channel) {
    soloChannel_ = std::clamp(channel, 0, 2);
    if (!mpv_) return;
    
    // mpv audio filter: pan
    std::string filter;
    if (channel == 1) filter = "pan=1c|c0=c0";  // Left only
    else if (channel == 2) filter = "pan=1c|c0=c1";  // Right only
    else filter = "";  // Stereo
    
    mpv_set_option_string(mpv_, "af", filter.c_str());
}

void AudioPlayer::setNormalization(bool enabled) {
    normalizationEnabled_ = enabled;
    if (!mpv_) return;
    
    // mpv: dynaudnorm filter
    if (enabled) {
        mpv_set_option_string(mpv_, "af", "dynaudnorm");
    } else {
        mpv_set_option_string(mpv_, "af", "");
    }
}

void AudioPlayer::setSpeed(float speed) {
    speed_ = std::clamp(speed, 0.25f, 4.0f);
    if (!mpv_) return;
    
    double mpv_speed = static_cast<double>(speed_);
    mpv_set_property(mpv_, "speed", MPV_FORMAT_DOUBLE, &mpv_speed);
}

void AudioPlayer::setPitch(int semitones) {
    pitchSemitones_ = std::clamp(semitones, -12, 12);
    if (!mpv_) return;
    
    // mpv: rubberband filter mit pitch-shift
    std::string filter = "rubberband=pitch=" + std::to_string(std::pow(2.0, semitones / 12.0));
    mpv_set_option_string(mpv_, "af", filter.c_str());
}

void AudioPlayer::setPreserveVocals(bool enabled) {
    preserveVocals_ = enabled;
}

// Event-Thread für mpv Events
void AudioPlayer::processEvents() {
    while (!stopEventThread_) {
        if (!mpv_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        mpv_event* event = mpv_wait_event(mpv_, 0.1);
        
        if (event->event_id == MPV_EVENT_END_FILE) {
            isPlaying_ = false;
            isPaused_ = false;
        }
        else if (event->event_id == MPV_EVENT_PLAYBACK_RESTART) {
            isPlaying_ = true;
        }
        else if (event->event_id == MPV_EVENT_PROPERTY_CHANGE) {
            mpv_event_property* prop = (mpv_event_property*)event->data;
            if (prop && std::string(prop->name) == "pause") {
                if (prop->format == MPV_FORMAT_FLAG) {
                    int pause_state = *(int*)prop->data;
                    isPaused_ = (pause_state == 1);
                }
            }
        }
    }
}

// Alte Funktionen entfernt - nicht mehr benötigt mit libmpv
// === Ende der AudioPlayer Implementation ===
// Alle alten Funktionen wurden durch libmpv ersetzt

#if 0
// Alte SDL/FFmpeg/RubberBand Code (nicht mehr verwendet)
void AudioPlayer::applyTimeStretch_OLD() {
#ifdef WITH_RUBBERBAND
    if (!needsReprocessing_ || audioBuffer_.empty()) return;
    
    float speed = speed_.load();
    int pitch = pitchSemitones_.load();
    bool preserveVocals = preserveVocals_.load();
    
    // Kein Processing nötig wenn alles auf Standard
    if (speed == 1.0f && pitch == 0) {
        processedBuffer_ = audioBuffer_;
        needsReprocessing_ = false;
        return;
    }
    
    // RubberBand Stretcher initialisieren
    int sampleRate = 44100;
    int channels = 2;
    
    RubberBandStretcher::Options options = 
        RubberBandStretcher::OptionProcessRealTime |
        RubberBandStretcher::OptionThreadingNever;
    
    // Vokal-Erhaltung
    if (preserveVocals) {
        options |= RubberBandStretcher::OptionFormantPreserved;
    }
    
    RubberBandStretcher stretcher(sampleRate, channels, options);
    
    // Setze Time-Ratio (Geschwindigkeit) und Pitch-Ratio
    stretcher.setTimeRatio(1.0 / speed);  // Invertiert: 2.0x speed = 0.5 time ratio
    stretcher.setPitchScale(std::pow(2.0, pitch / 12.0));  // Semitones zu Ratio
    
    // Konvertiere interleaved zu planar
    size_t frameCount = audioBuffer_.size() / 2;
    std::vector<float> leftChannel(frameCount);
    std::vector<float> rightChannel(frameCount);
    
    for (size_t i = 0; i < frameCount; i++) {
        leftChannel[i] = audioBuffer_[i * 2];
        rightChannel[i] = audioBuffer_[i * 2 + 1];
    }
    
    float* inputBuffers[2] = { leftChannel.data(), rightChannel.data() };
    
    // Studiere und process
    stretcher.study(inputBuffers, frameCount, true);
    stretcher.process(inputBuffers, frameCount, true);
    
    // Alte SDL/FFmpeg/RubberBand Funktionen wurden entfernt
    // libmpv übernimmt jetzt alles automatisch!
    
    if (available > 0) {
        std::vector<float> outLeft(available);
        std::vector<float> outRight(available);
        float* outputBuffers[2] = { outLeft.data(), outRight.data() };
        
        size_t retrieved = stretcher.retrieve(outputBuffers, available);
        
        // Konvertiere zurück zu interleaved
        for (size_t i = 0; i < retrieved; i++) {
            processedBuffer_.push_back(outLeft[i]);
            processedBuffer_.push_back(outRight[i]);
        }
    }
    
    needsReprocessing_ = false;
    std::cout << "🎛️ Time-Stretch: " << audioBuffer_.size() << " → " 
              << processedBuffer_.size() << " samples (Speed: " << speed 
              << "x, Pitch: " << pitch << " semitones)" << std::endl;
#else
    // Fallback: Simple Speed-Change ohne Pitch-Preservation
    float speed = speed_.load();
    if (speed == 1.0f) {
        processedBuffer_ = audioBuffer_;
    } else {
        processedBuffer_.clear();
        for (size_t i = 0; i < audioBuffer_.size(); i += 2) {
            size_t srcPos = static_cast<size_t>(i / speed) * 2;
            if (srcPos + 1 < audioBuffer_.size()) {
                processedBuffer_.push_back(audioBuffer_[srcPos]);
                processedBuffer_.push_back(audioBuffer_[srcPos + 1]);
            }
        }
    }
    needsReprocessing_ = false;
#endif
}

#ifdef WITH_FFMPEG
bool AudioPlayer::loadFFmpeg(const std::string& filepath) {
    AVFormatContext* formatCtx = nullptr;
    AVCodecContext* codecCtx = nullptr;
    SwrContext* swrCtx = nullptr;
    
    // Öffne Audio-Datei
    if (avformat_open_input(&formatCtx, filepath.c_str(), nullptr, nullptr) < 0) {
        std::cerr << "❌ FFmpeg: Cannot open file: " << filepath << std::endl;
        return false;
    }
    
    if (avformat_find_stream_info(formatCtx, nullptr) < 0) {
        avformat_close_input(&formatCtx);
        return false;
    }
    
    // Finde Audio-Stream
    int audioStreamIndex = -1;
    for (unsigned i = 0; i < formatCtx->nb_streams; i++) {
        if (formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audioStreamIndex = i;
            break;
        }
    }
    
    if (audioStreamIndex == -1) {
        avformat_close_input(&formatCtx);
        return false;
    }
    
    AVStream* audioStream = formatCtx->streams[audioStreamIndex];
    const AVCodec* codec = avcodec_find_decoder(audioStream->codecpar->codec_id);
    if (!codec) {
        avformat_close_input(&formatCtx);
        return false;
    }
    
    codecCtx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codecCtx, audioStream->codecpar);
    
    if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
        avcodec_free_context(&codecCtx);
        avformat_close_input(&formatCtx);
        return false;
    }
    
    // Resampler für einheitliches Format (44.1kHz, Stereo, Float)
    // FFmpeg 5+ nutzt ch_layout statt channel_layout
    AVChannelLayout stereoLayout = AV_CHANNEL_LAYOUT_STEREO;
    
    swr_alloc_set_opts2(&swrCtx,
        &stereoLayout, AV_SAMPLE_FMT_FLT, 44100,  // Output: Stereo, Float, 44.1kHz
        &codecCtx->ch_layout, codecCtx->sample_fmt, codecCtx->sample_rate,  // Input
        0, nullptr);
    swr_init(swrCtx);
    
    // Decode und konvertiere alle Frames
    audioBuffer_.clear();
    AVPacket packet;
    AVFrame* frame = av_frame_alloc();
    
    while (av_read_frame(formatCtx, &packet) >= 0) {
        if (packet.stream_index == audioStreamIndex) {
            if (avcodec_send_packet(codecCtx, &packet) >= 0) {
                while (avcodec_receive_frame(codecCtx, frame) >= 0) {
                    // Resampling
                    int outSamples = swr_get_out_samples(swrCtx, frame->nb_samples);
                    float* buffer = nullptr;
                    av_samples_alloc((uint8_t**)&buffer, nullptr, 2, outSamples, AV_SAMPLE_FMT_FLT, 0);
                    
                    int converted = swr_convert(swrCtx, (uint8_t**)&buffer, outSamples,
                                               (const uint8_t**)frame->data, frame->nb_samples);
                    
                    // Füge zu Buffer hinzu
                    for (int i = 0; i < converted * 2; i++) {
                        audioBuffer_.push_back(buffer[i]);
                    }
                    
                    av_freep(&buffer);
                }
            }
        }
        av_packet_unref(&packet);
    }
    
    av_frame_free(&frame);
    swr_free(&swrCtx);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&formatCtx);
    
    std::cout << "✅ FFmpeg: Loaded " << audioBuffer_.size() << " samples from " << filepath << std::endl;
    return !audioBuffer_.empty();
}
#endif

void AudioPlayer::audioCallback(void* userdata, Uint8* stream, int len) {
    AudioPlayer* player = static_cast<AudioPlayer*>(userdata);
    
    std::memset(stream, 0, len);
    
    if (!player->isPlaying_ || player->isPaused_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(player->audioMutex_);
    
    // Time-Stretch bei Bedarf anwenden
    if (player->needsReprocessing_) {
        player->applyTimeStretch();
    }
    
    // Nutze processedBuffer wenn vorhanden, sonst audioBuffer
    const std::vector<float>& buffer = player->processedBuffer_.empty() ? 
        player->audioBuffer_ : player->processedBuffer_;
    
    if (buffer.empty()) {
        player->isPlaying_ = false;
        return;
    }
    
    float* output = reinterpret_cast<float*>(stream);
    size_t numSamples = len / sizeof(float);
    
    float volume = player->volume_.load();
    float compThreshold = player->compressorThreshold_.load();
    float compRatio = player->compressorRatio_.load();
    float fadeIn = player->fadeInDuration_.load();
    float fadeOut = player->fadeOutDuration_.load();
    int soloChannel = player->soloChannel_.load();
    bool normalize = player->normalizationEnabled_.load();
    
    float duration = static_cast<float>(buffer.size()) / player->audioSpec_.freq;
    
    for (size_t i = 0; i < numSamples; i += 2) {  // Stereo: 2 Samples pro Frame
        size_t pos = player->playbackPosition_.load();
        
        if (pos >= buffer.size() - 1) {
            player->isPlaying_ = false;
            break;
        }
        
        float leftSample = buffer[pos];
        float rightSample = buffer[pos + 1];
        
        // Solo-Kanal Auswahl
        if (soloChannel == 1) {  // Nur Links
            rightSample = leftSample;
        } else if (soloChannel == 2) {  // Nur Rechts
            leftSample = rightSample;
        }
        
        // Fade In
        if (fadeIn > 0.0f) {
            float currentTime = static_cast<float>(pos) / player->audioSpec_.freq;
            if (currentTime < fadeIn) {
                float fadeFactor = currentTime / fadeIn;
                leftSample *= fadeFactor;
                rightSample *= fadeFactor;
            }
        }
        
        // Fade Out
        if (fadeOut > 0.0f) {
            float currentTime = static_cast<float>(pos) / player->audioSpec_.freq;
            float fadeStart = duration - fadeOut;
            if (currentTime > fadeStart) {
                float fadeFactor = (duration - currentTime) / fadeOut;
                leftSample *= fadeFactor;
                rightSample *= fadeFactor;
            }
        }
        
        // Compressor/Limiter (Softener/Hardener)
        auto applyCompressor = [&](float sample) -> float {
            float absLevel = std::abs(sample);
            float dbLevel = 20.0f * std::log10(absLevel + 1e-10f);
            
            if (dbLevel > compThreshold) {
                float excess = dbLevel - compThreshold;
                float compressed = compThreshold + (excess / compRatio);
                float linearGain = std::pow(10.0f, (compressed - dbLevel) / 20.0f);
                sample *= linearGain;
            }
            return sample;
        };
        
        leftSample = applyCompressor(leftSample);
        rightSample = applyCompressor(rightSample);
        
        // Normalisierung (Auto-Gain)
        if (normalize) {
            float peak = std::max(std::abs(leftSample), std::abs(rightSample));
            if (peak > 0.9f) {
                float normGain = 0.9f / peak;
                leftSample *= normGain;
                rightSample *= normGain;
            }
        }
        
        // Lautstärke anwenden
        output[i] = leftSample * volume;
        output[i + 1] = rightSample * volume;
        
        player->playbackPosition_ += 2;
    }
}
#endif
