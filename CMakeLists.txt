cmake_minimum_required(VERSION 3.16)
project(SongGen VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native")

# Find required libraries
find_package(PkgConfig REQUIRED)
pkg_check_modules(SIDPLAYFP REQUIRED libsidplayfp)
pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
pkg_check_modules(MPV REQUIRED mpv)

find_package(SQLite3 REQUIRED)
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)

# Optional: libsamplerate for audio resampling
pkg_check_modules(SAMPLERATE samplerate)
if(SAMPLERATE_FOUND)
    add_definitions(-DWITH_SAMPLERATE)
endif()

# Optional: FFTW3 for FFT (falls nicht, verwenden wir einfache DFT)
pkg_check_modules(FFTW3 fftw3f)
if(FFTW3_FOUND)
    add_definitions(-DWITH_FFTW3)
endif()

# Optional: libsndfile for MP3/FLAC/OGG support
pkg_check_modules(SNDFILE sndfile)
if(SNDFILE_FOUND)
    add_definitions(-DWITH_SNDFILE)
    message(STATUS "libsndfile found - MP3/FLAC/OGG support enabled")
else()
    message(STATUS "libsndfile not found - only WAV support")
endif()

# Optional: OpenVINO for Intel NPU acceleration
find_package(OpenVINO QUIET)
if(OpenVINO_FOUND)
    add_definitions(-DWITH_OPENVINO)
    message(STATUS "OpenVINO found - NPU acceleration enabled")
else()
    message(STATUS "OpenVINO not found - using CPU fallback")
endif()

# Optional: FFmpeg for MP3, MP4, etc.
pkg_check_modules(FFMPEG libavformat libavcodec libavutil libswresample)
if(FFMPEG_FOUND)
    add_definitions(-DWITH_FFMPEG)
    message(STATUS "FFmpeg found - MP3/MP4/etc. support enabled")
else()
    message(STATUS "FFmpeg not found - only WAV support")
endif()

# Optional: RubberBand for time-stretching and pitch-shifting
pkg_check_modules(RUBBERBAND rubberband)
if(RUBBERBAND_FOUND)
    add_definitions(-DWITH_RUBBERBAND)
    message(STATUS "RubberBand found - Time-stretch/Pitch-shift enabled")
else()
    message(STATUS "RubberBand not found - using simple resampling")
endif()

# ImGui
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/imgui_src)
include_directories(${IMGUI_DIR} ${IMGUI_DIR}/backends)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${SIDPLAYFP_INCLUDE_DIRS})
include_directories(${SDL2_INCLUDE_DIRS})
include_directories(${GTK3_INCLUDE_DIRS})
include_directories(${SQLite3_INCLUDE_DIRS})
include_directories(${CURL_INCLUDE_DIRS})

# ImGui sources
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer2.cpp
)

# Application sources (ImGui version)
set(SOURCES_IMGUI
    src/SIDLibConverter.cpp
    src/SonglengthsDB.cpp
    src/MediaDatabase.cpp
    src/AudioAnalyzer.cpp
    src/AudioSegmenter.cpp
    src/InstrumentExtractor.cpp
    src/FileBrowser.cpp
    src/SongGenerator.cpp
    src/TrainingModel.cpp
    src/HVSCDownloader.cpp
    src/AudioPlayer.cpp
    src/ImGuiRenderer.cpp
    src/main_imgui.cpp
    ${IMGUI_SOURCES}
)

# Application sources (GTK version)
set(SOURCES_GTK
    src/SIDLibConverter.cpp
    src/SonglengthsDB.cpp
    src/MediaDatabase.cpp
    src/AudioAnalyzer.cpp
    src/AudioSegmenter.cpp
    src/InstrumentExtractor.cpp
    src/FileBrowser.cpp
    src/SongGenerator.cpp
    src/TrainingModel.cpp
    src/HVSCDownloader.cpp
    src/AudioPlayer.cpp
    src/GtkRenderer.cpp
    src/main_gtk.cpp
)

# Executables
add_executable(songgen ${SOURCES_IMGUI})
add_executable(songgen-gtk ${SOURCES_GTK})

# Link libraries (ImGui version)
target_link_libraries(songgen 
    ${SIDPLAYFP_LIBRARIES}
    ${SDL2_LIBRARIES}
    ${MPV_LIBRARIES}
    ${SQLite3_LIBRARIES}
    ${CURL_LIBRARIES}
    mp3lame
    crypto
    pthread
)

# Link libraries (GTK version)
target_link_libraries(songgen-gtk 
    ${SIDPLAYFP_LIBRARIES}
    ${GTK3_LIBRARIES}
    ${SDL2_LIBRARIES}
    ${MPV_LIBRARIES}
    ${SQLite3_LIBRARIES}
    ${CURL_LIBRARIES}
    mp3lame
    crypto
    pthread
)

if(SAMPLERATE_FOUND)
    target_link_libraries(songgen ${SAMPLERATE_LIBRARIES})
    target_link_libraries(songgen-gtk ${SAMPLERATE_LIBRARIES})
endif()

if(FFTW3_FOUND)
    target_link_libraries(songgen ${FFTW3_LIBRARIES})
    target_link_libraries(songgen-gtk ${FFTW3_LIBRARIES})
endif()

if(SNDFILE_FOUND)
    target_link_libraries(songgen ${SNDFILE_LIBRARIES})
    target_link_libraries(songgen-gtk ${SNDFILE_LIBRARIES})
endif()

if(OpenVINO_FOUND)
    target_link_libraries(songgen openvino::runtime)
    target_link_libraries(songgen-gtk openvino::runtime)
endif()

if(FFMPEG_FOUND)
    target_link_libraries(songgen ${FFMPEG_LIBRARIES})
    target_link_libraries(songgen-gtk ${FFMPEG_LIBRARIES})
endif()

if(RUBBERBAND_FOUND)
    target_link_libraries(songgen ${RUBBERBAND_LIBRARIES})
    target_link_libraries(songgen-gtk ${RUBBERBAND_LIBRARIES})
endif()

# Link OpenSSL
target_link_libraries(songgen OpenSSL::SSL OpenSSL::Crypto)
target_link_libraries(songgen-gtk OpenSSL::SSL OpenSSL::Crypto)

message(STATUS "✅ SongGen build configured")
message(STATUS "   libsidplayfp: ${SIDPLAYFP_VERSION}")
message(STATUS "   SDL2: ${SDL2_VERSION}")
message(STATUS "   SQLite3: ${SQLite3_VERSION}")
message(STATUS "   CURL: ${CURL_VERSION}")
if(SAMPLERATE_FOUND)
    message(STATUS "   libsamplerate: ${SAMPLERATE_VERSION}")
endif()
if(FFTW3_FOUND)
    message(STATUS "   FFTW3: ${FFTW3_VERSION}")
endif()

## Test convert helper (disabled for now - can be re-enabled if needed)
# add_executable(test_convert tools/test_convert.cpp)
# target_link_libraries(test_convert
#     ${SIDPLAYFP_LIBRARIES}
#     ${SQLite3_LIBRARIES}
#     ${CURL_LIBRARIES}
#     pthread
# )
# if(OpenVINO_FOUND)
#     target_link_libraries(test_convert openvino::runtime)
# endif()
# target_sources(test_convert PRIVATE src/SIDLibConverter.cpp)
