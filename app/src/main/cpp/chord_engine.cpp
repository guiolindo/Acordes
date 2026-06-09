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
    // Improved templates with two key ideas:
    // (1) Positive weight = expected pitch classes (root, 3rd, 5th, 7th).
    //     Root gets extra weight from its strong harmonic series.
    // (2) Negative weight = pitch classes that should be ABSENT for this chord.
    //     The major 3rd vs minor 3rd discriminator is what disambiguates
    //     A vs Am, C vs Cm, etc. Without negative weights the templates are
    //     too tolerant and chords sharing 2/3 notes flicker.

    auto setTriad = [&](int chord_idx, int root, int third, bool isMinor) {
        float* T = templates[chord_idx];
        memset(T, 0, 12 * sizeof(float));
        T[root]                = 1.50f;        // root + its octaves
        T[(root + third) % 12] = 1.00f;        // 3rd (M3=4 or m3=3)
        T[(root + 7) % 12]     = 0.90f;        // perfect 5th (also from root's 3rd harmonic)
        // Mild boost for 5th's harmonic contribution
        T[(root + 11) % 12]   += 0.08f;        // weak: leading tone in some overtones
        // Negative discriminators
        T[(root + (isMinor ? 4 : 3)) % 12] -= 0.50f;  // the OPPOSITE 3rd
        T[(root + 1) % 12]    -= 0.20f;        // b9 (not in basic triad)
        T[(root + 6) % 12]    -= 0.15f;        // tritone above root
    };

    auto setSeventh = [&](int chord_idx, int root, int third, int seventh, bool isMinor) {
        float* T = templates[chord_idx];
        memset(T, 0, 12 * sizeof(float));
        T[root]                  = 1.50f;
        T[(root + third) % 12]   = 1.00f;
        T[(root + 7) % 12]       = 0.90f;
        T[(root + seventh) % 12] = 0.70f;      // b7 in dom7/min7, M7 in maj7
        // Negative: opposite 3rd kills Am vs A confusion in 7ths too
        T[(root + (isMinor ? 4 : 3)) % 12] -= 0.45f;
        T[(root + 1) % 12]       -= 0.15f;
    };

    for (int root = 0; root < 12; root++) {
        setTriad  (      root, root, 4, false);          // Major
        setTriad  (12 +  root, root, 3, true);           // Minor
        setSeventh(24 +  root, root, 4, 10, false);      // Dom7
        setSeventh(36 +  root, root, 3, 10, true);       // Min7

        // L2-normalize each (so dot products are comparable). Negative
        // entries stay negative; we normalize by the norm of the vector.
        for (int t : {0, 12, 24, 36}) {
            float* T = templates[t + root];
            float sum = 0;
            for (int j = 0; j < 12; j++) sum += T[j] * T[j];
            float norm = sqrtf(sum);
            if (norm > 0) {
                for (int j = 0; j < 12; j++) T[j] /= norm;
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

    memset(ring_buffer_,        0, sizeof(ring_buffer_));
    memset(smooth_chroma_,      0, sizeof(smooth_chroma_));
    memset(key_accumulator_,    0, sizeof(key_accumulator_));
    memset(chroma_history_,     0, sizeof(chroma_history_));
    memset(chord_smooth_scores_,0, sizeof(chord_smooth_scores_));

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
        // Silence — decay the smoothed scores so the next sound starts fresh.
        for (int c = 0; c < NUM_CHORDS; c++) chord_smooth_scores_[c] *= 0.7f;
        chroma_hist_filled_ = 0;
        return;
    }

    // Apply window and copy to FFT input
    for (int i = 0; i < FFT_SIZE; i++) {
        fft_input_[i] = ring_buffer_[read_start + i] * fft_window_[i];
    }

    // Per-frame chroma
    float chroma[12];
    computeChroma(fft_input_, chroma);

    // Aggregate over CHROMA_HIST frames (~70 ms) — matching uses the averaged
    // chroma, which dramatically reduces noise compared to single-frame.
    memcpy(chroma_history_[chroma_hist_pos_], chroma, 12 * sizeof(float));
    chroma_hist_pos_ = (chroma_hist_pos_ + 1) % CHROMA_HIST;
    if (chroma_hist_filled_ < CHROMA_HIST) chroma_hist_filled_++;

    float agg_chroma[12] = {0};
    for (int i = 0; i < chroma_hist_filled_; i++) {
        for (int j = 0; j < 12; j++) agg_chroma[j] += chroma_history_[i][j];
    }
    for (int j = 0; j < 12; j++) agg_chroma[j] /= chroma_hist_filled_;

    // Re-normalize aggregated chroma
    {
        float s = 0;
        for (int j = 0; j < 12; j++) s += agg_chroma[j] * agg_chroma[j];
        float n = sqrtf(s);
        if (n > 1e-8f) for (int j = 0; j < 12; j++) agg_chroma[j] /= n;
    }

    // Update key accumulator from aggregated chroma (slower-changing input)
    updateKeyAccumulator(agg_chroma);

    // Detect key for harmonic-field bias
    float key_conf = 0;
    int key_idx = detectKey(key_conf);

    // Build in-key membership for the detected key (soft prior, never a filter)
    // Major key (idx 0-11) scale degrees: I, ii, iii, IV, V, vi, vii°
    //   = root + {0,2,4,5,7,9,11} as scale notes
    // Minor key (12-23): natural minor scale = root + {0,2,3,5,7,8,10}
    bool inkey_major_triad[12]   = {false};
    bool inkey_minor_triad[12]   = {false};
    if (key_idx >= 0 && key_conf > 0.55f) {
        bool isMajorKey = key_idx < 12;
        int  keyRoot    = key_idx % 12;
        if (isMajorKey) {
            // I, IV, V are major; ii, iii, vi are minor in a major key
            inkey_major_triad[(keyRoot + 0) % 12] = true; // I
            inkey_major_triad[(keyRoot + 5) % 12] = true; // IV
            inkey_major_triad[(keyRoot + 7) % 12] = true; // V
            inkey_minor_triad[(keyRoot + 2) % 12] = true; // ii
            inkey_minor_triad[(keyRoot + 4) % 12] = true; // iii
            inkey_minor_triad[(keyRoot + 9) % 12] = true; // vi
        } else {
            // i, iv, v are minor in natural minor; III, VI, VII are major
            inkey_minor_triad[(keyRoot + 0) % 12] = true; // i
            inkey_minor_triad[(keyRoot + 5) % 12] = true; // iv
            inkey_minor_triad[(keyRoot + 7) % 12] = true; // v (or V in harmonic minor)
            inkey_major_triad[(keyRoot + 3) % 12] = true; // III
            inkey_major_triad[(keyRoot + 8) % 12] = true; // VI
            inkey_major_triad[(keyRoot + 10)% 12] = true; // VII
        }
    }

    // Compute raw template scores for all 48 chords, with key bias.
    // Then IIR-smooth per-chord (stable across frames), then pick winner
    // and only switch the displayed chord if margin > MARGIN_THRESHOLD.
    float raw_scores[NUM_CHORDS];
    for (int c = 0; c < NUM_CHORDS; c++) {
        float dot = 0.0f;
        for (int i = 0; i < 12; i++) dot += agg_chroma[i] * chord_templates_[c][i];

        // Mild key bias (only on basic triads; 7ths are bias-neutral)
        int root = c % 12;
        if (c < 12) {                                  // major triads
            if (inkey_major_triad[root])      dot *= 1.05f;
            else if (key_conf > 0.55f)        dot *= 0.97f;
        } else if (c < 24) {                           // minor triads
            if (inkey_minor_triad[root])      dot *= 1.05f;
            else if (key_conf > 0.55f)        dot *= 0.97f;
        }

        raw_scores[c] = dot;
    }

    // IIR-smooth each chord score
    const float a = SCORE_SMOOTH_ALPHA;
    for (int c = 0; c < NUM_CHORDS; c++) {
        chord_smooth_scores_[c] = a * raw_scores[c] + (1.0f - a) * chord_smooth_scores_[c];
    }

    // Find best and runner-up among smoothed scores
    int   best     = 0;
    int   second   = 1;
    float bestVal  = chord_smooth_scores_[0];
    float secondVal= chord_smooth_scores_[1];
    if (secondVal > bestVal) {
        best = 1; second = 0;
        float tmp = bestVal; bestVal = secondVal; secondVal = tmp;
    }
    for (int c = 2; c < NUM_CHORDS; c++) {
        float v = chord_smooth_scores_[c];
        if (v > bestVal) {
            second = best;     secondVal = bestVal;
            best   = c;        bestVal   = v;
        } else if (v > secondVal) {
            second = c;        secondVal = v;
        }
    }

    // Margin-gated update: switch displayed chord only when the new candidate
    // really pulls ahead. If the current stable chord still wins, keep it.
    if (stable_chord_idx_ < 0) {
        // First time — adopt the best as soon as it crosses a minimum quality
        if (bestVal > 0.40f) stable_chord_idx_ = best;
    } else if (best == stable_chord_idx_) {
        // Already correct — nothing to do
    } else {
        float current_val = chord_smooth_scores_[stable_chord_idx_];
        float margin      = bestVal - current_val;
        if (margin > MARGIN_THRESHOLD) {
            stable_chord_idx_ = best;
        }
    }

    if (callback_ && stable_chord_idx_ >= 0) {
        ChordResult result;
        result.chordIndex  = stable_chord_idx_;
        result.keyIndex    = key_idx;
        result.confidence  = chord_smooth_scores_[stable_chord_idx_];
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

    // Log-compress magnitudes: log1p(power * scale) keeps small bins audible
    // and stops single loud transients (drum hit, vocal pop) from dominating.
    for (int k = 1; k <= FFT_SIZE / 2; k++) {
        int c = bin_to_chroma[k];
        if (c < 0) continue;
        float power = freq_buf[k].r * freq_buf[k].r + freq_buf[k].i * freq_buf[k].i;
        chroma[c] += log1pf(power * 1000.0f);
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
        float power = freq_buf[k].r * freq_buf[k].r + freq_buf[k].i * freq_buf[k].i;
        if (power > bass_max_mag) {
            bass_max_mag = power;
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
