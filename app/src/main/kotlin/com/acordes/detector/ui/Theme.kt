package com.acordes.detector.ui

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

val BackgroundDark   = Color(0xFF0A0A0F)
val SurfaceDark      = Color(0xFF12121A)
val CardDark         = Color(0xFF1A1A28)
val AccentPrimary    = Color(0xFF7C6AF7)   // Violet
val AccentSecondary  = Color(0xFF4ECDC4)   // Teal
val TextPrimary      = Color(0xFFF0F0FF)
val TextSecondary    = Color(0xFF8888AA)
val HistoryItemBg    = Color(0xFF22223A)
val ConfidenceGreen  = Color(0xFF4CAF50)
val ConfidenceYellow = Color(0xFFFFEB3B)

private val DarkColors = darkColorScheme(
    primary       = AccentPrimary,
    secondary     = AccentSecondary,
    background    = BackgroundDark,
    surface       = SurfaceDark,
    onPrimary     = Color.White,
    onBackground  = TextPrimary,
    onSurface     = TextPrimary,
)

@Composable
fun AcordesTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = DarkColors,
        content = content
    )
}
