package com.acordes.detector

import android.app.Application
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

    private var lastChord: String = ""
    private val historyMaxSize = 12

    override fun onChord(
        chordIndex: Int,
        keyIndex: Int,
        confidence: Float,
        rms: Float,
        chordName: String,
        keyName: String
    ) {
        // C++ engine already applies temporal voting; trust whatever it emits.
        _state.update { current ->
            val newHistory = if (chordName != lastChord) {
                lastChord = chordName
                val updated = current.history.toMutableList()
                updated.add(chordName)
                if (updated.size > historyMaxSize) updated.removeAt(0)
                updated.toList()
            } else {
                current.history
            }
            current.copy(
                currentChord = chordName,
                currentKey = keyName,
                confidence = confidence,
                rms = rms,
                history = newHistory
            )
        }
    }

    fun startListening() {
        ChordDetector.start(this)
        _state.update { it.copy(isListening = true) }
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
