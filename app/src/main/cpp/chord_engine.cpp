#include "chord_engine.h"
#include "kissfft/kiss_fft.h"
#include <oboe/Oboe.h>
#include <android/log.h>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <numeric>

#define LOG_TAG "ChordEngine"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ─── Chord names ─────────────────────────────────────────────────────────────
// 0-11: major, 12-23: minor, 24-35: dom7, 36-47: min7
static const char* CHORD_NAMES[] = {
    "C","C#","D","D#","E","F","F#","G","G#","A","A#","B",
    "Cm","C#m","Dm","D#m","Em","Fm","F#m","Gm","G#m","Am","A#m","Bm",
    "C7","C#7","D7","D#7","E7","F7","F#7","G7","G#7","A7","A#7","B7",
    "Cm7","C#m7","Dm7","D#m7","Em7","Fm7","F#m7","Gm7","G#m7","Am7","A#m7","Bm7"
};

static const char* KEY_NAMES[] = {
    "C maj","C# maj","D maj","D# maj","E maj","F maj",
    "F# maj","G maj","G# maj","A maj","A# maj","B maj",
    "C min","C# min","D min","D# min","E min","F min",
    "F# min","G min","G# min","A min","A# min","B min"
};

const char* chordName(int index) {
    if (index < 0 || index >= 48) return "?";
    return CHORD_NAMES[index];
}

const char* keyName(int index) {
    if (index < 0 || index >= 24) return "?";
    return KEY_NAMES[index];
}

// ─── Oboe callback wrapper ────────────────────────────────────────────────────
class AudioCallback : public oboe::AudioStreamDataCallback {
public:
    explicit AudioCallback(ChordEngine* engine) : engine_(engine) {}

    oboe::DataCallbackResult onAudioReady(
        oboe::AudioStream* stream,
        void* audioData,
        int32_t numFrames) override
    {
        engine_->processAudio(static_cast<float*>(audioData), numFrames);
        return oboe::DataCallbackResult::Continue;
    }
private:
    ChordEngine* engine_;
};

static std::unique_ptr<AudioCallback> g_callback;

// ─── Chord templates ──────────────────────────────────────────────────────────
// Notes: C=0, C#=1, D=2, D#=3, E=4, F=5, F#=6, G=7, G#=8, A=9, A#=10, B=11
// Major triad:  root, root+4, root+7
// Minor triad:  root, root+3, root+7
// Dom7:         root, root+4, root+7, root+10
// Min7:         root, root+3, root+7, root+10
static void buildChordTemplates(float templates[48][12]) {
    // Weights: root=1.0, third=0.8, fifth=0.6, seventh=0.5
    // Also add harmonic weighting: +12 contributes 0.4, +7 (fifth) adds overtone
    static const int MAJOR_INTERVALS[] = {0, 4, 7, -1};
    static const int MINOR_INTERVALS[] = {0, 3, 7, -1};
    static const int DOM7_INTERVALS[]  = {0, 4, 7, 10};
    static const int MIN7_INTERVALS[]  = {0, 3, 7, 10};

    static const float WEIGHTS_TRI[] = {1.0f, 0.8f, 0.6f};
    static const float WEIGHTS_7TH[] = {1.0f, 0.8f, 0.6f, 0.5f};

    for (int root = 0; root < 12; root++) {
        // Major (0-11)
        memset(templates[root], 0, 12 * sizeof(float));
        for (int i = 0; i < 3; i++) {
            int note = (root + MAJOR_INTERVALS[i]) % 12;
            templates[root][note] += WEIGHTS_TRI[i];
        }
        // Minor (12-23)
        memset(templates[12 + root], 0, 12 * sizeof(float));
        for (int i = 0; i < 3; i++) {
            int note = (root + MINOR_INTERVALS[i]) % 12;
            templates[12 + root][note] += WEIGHTS_TRI[i];
        }
        // Dom7 (24-35)
        memset(templates[24 + root], 0, 12 * sizeof(float));
        for (int i = 0; i < 4; i++) {
            int note = (root + DOM7_INTERVALS[i]) % 12;
            templates[24 + root][note] += WEIGHTS_7TH[i];
        }
        // Min7 (36-47)
        memset(templates[36 + root], 0, 12 * sizeof(float));
        for (int i = 0; i < 4; i++) {
            int note = (root + MIN7_INTERVALS[i]) % 12;
            templates[36 + root][note] += WEIGHTS_7TH[i];
        }

        // Normalize each template
        for (int t : {0, 12, 24, 36}) {
            float sum = 0;
            for (int j = 0; j < 12; j++) sum += templates[t + root][j] * templates[t + root][j];
            float norm = sqrtf(sum);
            if (norm > 0) {
                for (int j = 0; j < 12; j++) templates[t + root][j] /= norm;
            }
        }
    }
}

// ─── Key profiles (Krumhansl-Schmuckler) ─────────────────────────────────────
static void buildKeyProfiles(float profiles[24][12]) {
    static const float MAJOR_PROFILE[] = {
        6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f,
        2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f
    };
    static const float MINOR_PROFILE[] = {
        6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f,
        2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f
    };
    for (int root = 0; root < 12; root++) {
        float sum_maj = 0, sum_min = 0;
        for (int j = 0; j < 12; j++) {
            profiles[root][j]      = MAJOR_PROFILE[(j - root + 12) % 12];
            profiles[12 + root][j] = MINOR_PROFILE[(j - root + 12) % 12];
            sum_maj += profiles[root][j] * profiles[root][j];
            sum_min += profiles[12 + root][j] * profiles[12 + root][j];
        }
        float nm = sqrtf(sum_maj), nn = sqrtf(sum_min);
        for (int j = 0; j < 12; j++) {
            profiles[root][j]      /= nm;
            profiles[12 + root][j] /= nn;
        }
    }
}

// ─── ChordEngine implementation ───────────────────────────────────────────────
ChordEngine::ChordEngine() {
    // Hanning window
    for (int i = 0; i < FFT_SIZE; i++) {
        fft_window_[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (FFT_SIZE - 1)));
    }

    memset(ring_buffer_,    0, sizeof(ring_buffer_));
    memset(smooth_chroma_,  0, sizeof(smooth_chroma_));
    memset(key_accumulator_, 0, sizeof(key_accumulator_));
    for (int i = 0; i < VOTE_WINDOW; i++) chord_history_[i] = -1;

    buildChordTemplates(chord_templates_);
    buildKeyProfiles(key_profiles_);

    // Allocate FFT (real FFT of size FFT_SIZE)
    fft_cfg_ = kiss_fftr_alloc(FFT_SIZE, 0, nullptr, nullptr);
}

ChordEngine::~ChordEngine() {
    stop();
    if (fft_cfg_) kiss_fftr_free((kiss_fftr_cfg)fft_cfg_);
}

bool ChordEngine::start(ChordCallback callback) {
    callback_ = callback;

    g_callback = std::make_unique<AudioCallback>(this);

    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Input);
    builder.setPerformanceMode(oboe::PerformanceMode::LowLatency);
    builder.setSharingMode(oboe::SharingMode::Exclusive);
    builder.setFormat(oboe::AudioFormat::Float);
    builder.setChannelCount(oboe::ChannelCount::Mono);
    builder.setSampleRate(SAMPLE_RATE);
    builder.setFramesPerDataCallback(HOP_SIZE);
    builder.setDataCallback(g_callback.get());
    builder.setInputPreset(oboe::InputPreset::Unprocessed);

    oboe::AudioStream* stream = nullptr;
    oboe::Result result = builder.openStream(&stream);
    if (result != oboe::Result::OK) {
        LOGE("Failed to open Oboe stream: %s", oboe::convertToText(result));
        return false;
    }

    oboe_stream_ = stream;
    result = stream->requestStart();
    if (result != oboe::Result::OK) {
        LOGE("Failed to start Oboe stream: %s", oboe::convertToText(result));
        stream->close();
        oboe_stream_ = nullptr;
        return false;
    }

    running_.store(true);
    LOGI("Oboe stream started: sampleRate=%d, framesPerCallback=%d",
         stream->getSampleRate(), stream->getFramesPerDataCallback());
    return true;
}

void ChordEngine::stop() {
    running_.store(false);
    if (oboe_stream_) {
        auto* stream = static_cast<oboe::AudioStream*>(oboe_stream_);
        stream->requestStop();
        stream->close();
        oboe_stream_ = nullptr;
    }
    g_callback.reset();
}

void ChordEngine::processAudio(const float* data, int numFrames) {
    // Write incoming samples into ring buffer
    for (int i = 0; i < numFrames; i++) {
        ring_buffer_[ring_write_pos_] = data[i];
        ring_buffer_[ring_write_pos_ + FFT_SIZE] = data[i]; // double buffer trick
        ring_write_pos_ = (ring_write_pos_ + 1) % FFT_SIZE;
    }

    samples_since_last_hop_ += numFrames;
    if (samples_since_last_hop_ < HOP_SIZE) return;
    samples_since_last_hop_ -= HOP_SIZE;

    // Compute RMS for silence detection
    float rms = 0;
    int read_start = (ring_write_pos_ - FFT_SIZE + FFT_SIZE) % FFT_SIZE;
    for (int i = 0; i < FFT_SIZE; i++) {
        float s = ring_buffer_[read_start + i];
        rms += s * s;
    }
    rms = sqrtf(rms / FFT_SIZE);

    if (rms < 0.003f) {
        // Silence — clear vote history so next sound reacts quickly
        for (int i = 0; i < VOTE_WINDOW; i++) chord_history_[i] = -1;
        return;
    }

    // Apply window and copy to FFT input
    for (int i = 0; i < FFT_SIZE; i++) {
        fft_input_[i] = ring_buffer_[read_start + i] * fft_window_[i];
    }

    // Compute chroma
    float chroma[12];
    computeChroma(fft_input_, chroma);

    // Update key accumulator
    updateKeyAccumulator(chroma);

    // Match chord (raw, per-frame)
    float chord_conf = 0;
    int chord_idx = matchChord(chroma, chord_conf);

    // Temporal voting: push current frame into ring buffer of recent picks
    chord_history_[chord_history_pos_] = chord_idx;
    chord_history_pos_ = (chord_history_pos_ + 1) % VOTE_WINDOW;

    // Count votes for each chord; pick one with at least VOTE_THRESHOLD agreement
    int counts[NUM_CHORDS] = {0};
    for (int i = 0; i < VOTE_WINDOW; i++) {
        int c = chord_history_[i];
        if (c >= 0) counts[c]++;
    }
    int best_votes = 0;
    int vote_winner = stable_chord_idx_;
    for (int c = 0; c < NUM_CHORDS; c++) {
        if (counts[c] > best_votes) {
            best_votes = counts[c];
            vote_winner = c;
        }
    }
    if (best_votes >= VOTE_THRESHOLD) {
        stable_chord_idx_ = vote_winner;
    }

    // Detect key
    float key_conf = 0;
    int key_idx = detectKey(key_conf);

    if (callback_ && stable_chord_idx_ >= 0) {
        ChordResult result;
        result.chordIndex  = stable_chord_idx_;
        result.keyIndex    = key_idx;
        result.confidence  = chord_conf;
        result.rms         = rms;
        callback_(result);
    }
}

void ChordEngine::computeChroma(const float* window, float* chroma) {
    static kiss_fft_cpx freq_buf[FFT_SIZE / 2 + 1];

    kiss_fftr((kiss_fftr_cfg)fft_cfg_, window, freq_buf);

    memset(chroma, 0, 12 * sizeof(float));

    // Map FFT bins to chroma
    // Bin k corresponds to frequency: f = k * SAMPLE_RATE / FFT_SIZE
    // Chroma class: c = round(12 * log2(f / f_C0)) mod 12
    // f_C0 = 16.35 Hz (C0)
    static const float f_C0 = 16.3516f;
    static const float inv_log2 = 1.0f / logf(2.0f);
    static const float log2_f_C0 = logf(f_C0) * inv_log2;

    // Pre-compute bin->chroma map (static, computed once)
    static int bin_to_chroma[FFT_SIZE / 2 + 1];
    static bool map_built = false;
    if (!map_built) {
        for (int k = 1; k <= FFT_SIZE / 2; k++) {
            float f = (float)k * SAMPLE_RATE / FFT_SIZE;
            if (f < 50.0f || f > 5000.0f) {
                bin_to_chroma[k] = -1;
            } else {
                float pitch_class = 12.0f * (logf(f) * inv_log2 - log2_f_C0);
                int c = ((int)roundf(pitch_class)) % 12;
                if (c < 0) c += 12;
                bin_to_chroma[k] = c;
            }
        }
        map_built = true;
    }

    for (int k = 1; k <= FFT_SIZE / 2; k++) {
        int c = bin_to_chroma[k];
        if (c < 0) continue;
        float mag = freq_buf[k].r * freq_buf[k].r + freq_buf[k].i * freq_buf[k].i;
        chroma[c] += mag;
    }

    // Bass-note bias: dominant pitch class in 55-220 Hz is usually the root
    // played by the bass guitar — boost it to disambiguate (e.g. C vs Am).
    static const int bass_bin_lo = (int)(55.0f  * FFT_SIZE / SAMPLE_RATE);
    static const int bass_bin_hi = (int)(220.0f * FFT_SIZE / SAMPLE_RATE);
    int   bass_pc      = -1;
    float bass_max_mag = 0.0f;
    for (int k = bass_bin_lo; k <= bass_bin_hi; k++) {
        int c = bin_to_chroma[k];
        if (c < 0) continue;
        float mag = freq_buf[k].r * freq_buf[k].r + freq_buf[k].i * freq_buf[k].i;
        if (mag > bass_max_mag) {
            bass_max_mag = mag;
            bass_pc = c;
        }
    }
    if (bass_pc >= 0) {
        chroma[bass_pc] *= 1.5f;
    }

    // Normalize chroma vector (L2)
    float sum = 0;
    for (int i = 0; i < 12; i++) sum += chroma[i] * chroma[i];
    float norm = sqrtf(sum);
    if (norm > 1e-8f) {
        for (int i = 0; i < 12; i++) chroma[i] /= norm;
    }

    // Smooth with previous chroma (IIR). Lower alpha = more stable, less reactive.
    const float alpha = 0.20f;
    for (int i = 0; i < 12; i++) {
        smooth_chroma_[i] = alpha * chroma[i] + (1.0f - alpha) * smooth_chroma_[i];
        chroma[i] = smooth_chroma_[i];
    }
}

int ChordEngine::matchChord(const float* chroma, float& confidence) {
    float best_score = -1.0f;
    int best_chord = 0;

    for (int c = 0; c < NUM_CHORDS; c++) {
        float dot = 0;
        for (int i = 0; i < 12; i++) {
            dot += chroma[i] * chord_templates_[c][i];
        }
        if (dot > best_score) {
            best_score = dot;
            best_chord = c;
        }
    }

    confidence = best_score;
    return best_chord;
}

void ChordEngine::updateKeyAccumulator(const float* chroma) {
    const float decay = 0.995f;
    for (int i = 0; i < 12; i++) {
        key_accumulator_[i] = decay * key_accumulator_[i] + chroma[i];
    }
    key_frame_count_++;
}

int ChordEngine::detectKey(float& confidence) {
    float best_score = -1.0f;
    int best_key = 0;

    // Normalize key accumulator
    float acc_norm[12];
    float sum = 0;
    for (int i = 0; i < 12; i++) sum += key_accumulator_[i] * key_accumulator_[i];
    float norm = sqrtf(sum);
    if (norm < 1e-8f) {
        confidence = 0;
        return 0;
    }
    for (int i = 0; i < 12; i++) acc_norm[i] = key_accumulator_[i] / norm;

    for (int k = 0; k < NUM_KEYS; k++) {
        float dot = 0;
        for (int i = 0; i < 12; i++) dot += acc_norm[i] * key_profiles_[k][i];
        if (dot > best_score) {
            best_score = dot;
            best_key = k;
        }
    }

    confidence = best_score;
    return best_key;
}
