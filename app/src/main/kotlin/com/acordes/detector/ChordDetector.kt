package com.acordes.detector

interface ChordListener {
    fun onChord(
        chordIndex: Int,
        keyIndex: Int,
        confidence: Float,
        rms: Float,
        chordName: String,
        keyName: String
    )
}

object ChordDetector {

    init {
        System.loadLibrary("acordes")
        nativeInit()
    }

    fun start(listener: ChordListener) = nativeStart(listener)
    fun stop()                         = nativeStop()
    fun isRunning(): Boolean           = nativeIsRunning()
    fun destroy()                      = nativeDestroy()

    private external fun nativeInit()
    private external fun nativeStart(listener: ChordListener)
    private external fun nativeStop()
    private external fun nativeDestroy()
    private external fun nativeIsRunning(): Boolean
}
