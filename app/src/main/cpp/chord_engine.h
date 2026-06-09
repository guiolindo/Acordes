#pragma once
#include <array>
#include <string>
#include <functional>
#include <atomic>
#include <cstdint>

struct ChordResult {
    int chordIndex;       // 0-47 (48 chords: 12 major, 12 minor, 12 dom7, 12 min7)
    int keyIndex;         // 0-23 (12 major keys + 12 minor keys)
    float confidence;     // 0.0 - 1.0
    float rms;            // signal RMS level
};

using ChordCallback = std::function<void(const ChordResult&)>;

class ChordEngine {
public:
    static constexpr int SAMPLE_RATE = 44100;
    static constexpr int FFT_SIZE    = 4096;
    static constexpr int HOP_SIZE    = 512;
    static constexpr int NUM_CHORDS  = 48;
    static constexpr int NUM_KEYS    = 24;

    ChordEngine();
    ~ChordEngine();

    bool start(ChordCallback callback);
    void stop();
    bool isRunning() const { return running_.load(); }

    // Called from Oboe audio callback (real-time thread, no locks)
    void processAudio(const float* data, int numFrames);

private:
    void computeChroma(const float* window, float* chroma);
    int  matchChord(const float* chroma, float& confidence);
    int  detectKey(float& confidence);
    void updateKeyAccumulator(const float* chroma);

    // FFT
    void* fft_cfg_ = nullptr;
    float fft_input_[FFT_SIZE];
    float fft_window_[FFT_SIZE];  // Hanning window

    // Ring buffer for incoming audio
    float ring_buffer_[FFT_SIZE * 2];
    int   ring_write_pos_ = 0;
    int   samples_since_last_hop_ = 0;

    // Chroma smoothing (IIR)
    float smooth_chroma_[12];
    float key_accumulator_[12];
    int   key_frame_count_ = 0;

    // Chord templates [48][12]
    float chord_templates_[NUM_CHORDS][12];

    // Key profiles (Krumhansl-Schmuckler)
    float key_profiles_[NUM_KEYS][12];

    // Temporal voting (stability): last 5 chord detections
    static constexpr int VOTE_WINDOW = 5;
    static constexpr int VOTE_THRESHOLD = 3;   // 3-of-5 wins
    int chord_history_[VOTE_WINDOW];
    int chord_history_pos_ = 0;
    int stable_chord_idx_ = -1;

    // Oboe stream
    void* oboe_stream_ = nullptr;

    std::atomic<bool> running_{false};
    ChordCallback callback_;
};

// Chord and key name lookup
const char* chordName(int index);
const char* keyName(int index);
