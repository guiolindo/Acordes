package com.acordes.detector

import android.app.Application
import android.os.SystemClock
import androidx.lifecycle.AndroidViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update

data class ChordState(
    val currentChord: String = "--",
    val currentKey: String = "--",
    val confidence: Float = 0f,
    val rms: Float = 0f,
    val history: List<String> = emptyList(),
    val isListening: Boolean = false
)

class ChordViewModel(app: Application) : AndroidViewModel(app), ChordListener {

    private val _state = MutableStateFlow(ChordState())
    val state: StateFlow<ChordState> = _state.asStateFlow()

    // Visible chord requires this much continuous time before the screen updates.
    // Stops every micro-flicker from the C++ engine reaching the UI.
    private val DISPLAY_HOLD_MS = 180L

    // Chord must remain displayed at least this long before being added to history.
    // Keeps the history bar meaningful (no transient mis-detections in it).
    private val HISTORY_MIN_HOLD_MS = 600L

    private var pendingChord: String = ""
    private var pendingSince: Long = 0L

    private var displayedChord: String = ""
    private var displayedSince: Long = 0L
    private var addedToHistory: Boolean = false

    override fun onChord(
        chordIndex: Int,
        keyIndex: Int,
        confidence: Float,
        rms: Float,
        chordName: String,
        keyName: String
    ) {
        val now = SystemClock.elapsedRealtime()

        // 1. Hold time before swapping the visible chord
        if (chordName != pendingChord) {
            pendingChord = chordName
            pendingSince = now
        }

        var didChangeDisplay = false
        if (pendingChord != displayedChord &&
            now - pendingSince >= DISPLAY_HOLD_MS
        ) {
            displayedChord = pendingChord
            displayedSince = now
            addedToHistory = false
            didChangeDisplay = true
        }

        // 2. History only gets confirmed chords that lasted long enough
        val shouldAddToHistory = !addedToHistory &&
            displayedChord.isNotEmpty() && displayedChord != "--" &&
            now - displayedSince >= HISTORY_MIN_HOLD_MS

        if (didChangeDisplay || shouldAddToHistory) {
            _state.update { current ->
                val newHistory = if (shouldAddToHistory) {
                    addedToHistory = true
                    val updated = current.history.toMutableList()
                    if (updated.isEmpty() || updated.last() != displayedChord) {
                        updated.add(displayedChord)
                        if (updated.size > 12) updated.removeAt(0)
                    }
                    updated.toList()
                } else {
                    current.history
                }
                current.copy(
                    currentChord = if (displayedChord.isEmpty()) "--" else displayedChord,
                    currentKey = keyName,
                    confidence = confidence,
                    rms = rms,
                    history = newHistory
                )
            }
        } else {
            // Always keep confidence/rms fresh for the level bar & dots
            _state.update { it.copy(confidence = confidence, rms = rms, currentKey = keyName) }
        }
    }

    fun startListening() {
        // Reset transient state so the next session starts clean
        pendingChord = ""
        displayedChord = ""
        addedToHistory = false
        _state.update { it.copy(isListening = true, currentChord = "--", history = emptyList()) }
        ChordDetector.start(this)
    }

    fun stopListening() {
        ChordDetector.stop()
        _state.update { it.copy(isListening = false) }
    }

    override fun onCleared() {
        ChordDetector.destroy()
        super.onCleared()
    }
}
