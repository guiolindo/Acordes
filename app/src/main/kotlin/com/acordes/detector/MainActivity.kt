package com.acordes.detector

import android.Manifest
import android.content.pm.PackageManager
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.Surface
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.core.content.ContextCompat
import com.acordes.detector.ui.AcordesTheme
import com.acordes.detector.ui.BackgroundDark
import com.acordes.detector.ui.ChordScreen

class MainActivity : ComponentActivity() {

    private val viewModel: ChordViewModel by viewModels()

    private val requestMicPermission = registerForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { granted ->
        if (granted) viewModel.startListening()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        setContent {
            AcordesTheme {
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = BackgroundDark
                ) {
                    val state by viewModel.state.collectAsState()
                    ChordScreen(
                        state = state,
                        onToggleListen = ::toggleListening
                    )
                }
            }
        }
    }

    private fun toggleListening() {
        if (viewModel.state.value.isListening) {
            viewModel.stopListening()
        } else {
            when {
                ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO)
                    == PackageManager.PERMISSION_GRANTED -> viewModel.startListening()
                else -> requestMicPermission.launch(Manifest.permission.RECORD_AUDIO)
            }
        }
    }

    override fun onStop() {
        super.onStop()
        viewModel.stopListening()
    }
}
