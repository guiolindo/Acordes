package com.acordes.detector.ui

import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.*
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Mic
import androidx.compose.material.icons.filled.MicOff
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.scale
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.acordes.detector.ChordState
import kotlinx.coroutines.launch

@Composable
fun ChordScreen(
    state: ChordState,
    onToggleListen: () -> Unit
) {
    val listState = rememberLazyListState()
    val coroutineScope = rememberCoroutineScope()

    // Auto-scroll history to end when new chord arrives
    LaunchedEffect(state.history.size) {
        if (state.history.isNotEmpty()) {
            coroutineScope.launch {
                listState.animateScrollToItem(state.history.size - 1)
            }
        }
    }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(BackgroundDark)
    ) {
        Column(
            modifier = Modifier.fillMaxSize(),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            // ── Top bar: Key indicator ──────────────────────────────────────
            TopKeyBar(key = state.currentKey, isListening = state.isListening)

            Spacer(modifier = Modifier.weight(1f))

            // ── Chord history row ───────────────────────────────────────────
            ChordHistoryRow(
                history = state.history,
                currentChord = state.currentChord,
                listState = listState
            )

            Spacer(modifier = Modifier.height(32.dp))

            // ── Current chord (big center display) ─────────────────────────
            CurrentChordDisplay(
                chord = state.currentChord,
                confidence = state.confidence,
                rms = state.rms,
                isListening = state.isListening
            )

            Spacer(modifier = Modifier.weight(1.2f))

            // ── RMS / level bar ─────────────────────────────────────────────
            LevelBar(rms = state.rms, isListening = state.isListening)

            Spacer(modifier = Modifier.height(24.dp))

            // ── Mic toggle button ───────────────────────────────────────────
            MicButton(isListening = state.isListening, onClick = onToggleListen)

            Spacer(modifier = Modifier.height(48.dp))
        }
    }
}

@Composable
private fun TopKeyBar(key: String, isListening: Boolean) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .statusBarsPadding()
            .padding(horizontal = 24.dp, vertical = 12.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(
            text = "Acordes",
            color = TextSecondary,
            fontSize = 14.sp,
            fontWeight = FontWeight.Medium,
            letterSpacing = 2.sp
        )
        if (key != "--") {
            Surface(
                shape = RoundedCornerShape(20.dp),
                color = AccentPrimary.copy(alpha = 0.15f),
                modifier = Modifier.border(
                    1.dp, AccentPrimary.copy(alpha = 0.4f), RoundedCornerShape(20.dp)
                )
            ) {
                Row(
                    modifier = Modifier.padding(horizontal = 14.dp, vertical = 6.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    Text("♫", color = AccentSecondary, fontSize = 12.sp)
                    Text(
                        text = key,
                        color = AccentSecondary,
                        fontSize = 13.sp,
                        fontWeight = FontWeight.SemiBold
                    )
                }
            }
        }
    }
}

@Composable
private fun ChordHistoryRow(
    history: List<String>,
    currentChord: String,
    listState: androidx.compose.foundation.lazy.LazyListState
) {
    if (history.isEmpty()) {
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(48.dp),
            contentAlignment = Alignment.Center
        ) {
            Text(
                text = "Toque um acorde para começar",
                color = TextSecondary.copy(alpha = 0.5f),
                fontSize = 13.sp
            )
        }
        return
    }

    LazyRow(
        state = listState,
        modifier = Modifier
            .fillMaxWidth()
            .height(52.dp),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        contentPadding = PaddingValues(horizontal = 24.dp)
    ) {
        items(history) { chord ->
            val isCurrent = chord == history.last() && chord == currentChord
            HistoryChip(chord = chord, isCurrent = isCurrent)
        }
    }
}

@Composable
private fun HistoryChip(chord: String, isCurrent: Boolean) {
    val bgColor by animateColorAsState(
        targetValue = if (isCurrent) AccentPrimary.copy(alpha = 0.25f) else HistoryItemBg,
        animationSpec = tween(200), label = "chip_bg"
    )
    val borderColor by animateColorAsState(
        targetValue = if (isCurrent) AccentPrimary else Color.Transparent,
        animationSpec = tween(200), label = "chip_border"
    )
    val textColor by animateColorAsState(
        targetValue = if (isCurrent) TextPrimary else TextSecondary,
        animationSpec = tween(200), label = "chip_text"
    )

    Box(
        modifier = Modifier
            .clip(RoundedCornerShape(12.dp))
            .background(bgColor)
            .border(1.dp, borderColor, RoundedCornerShape(12.dp))
            .padding(horizontal = 14.dp, vertical = 8.dp),
        contentAlignment = Alignment.Center
    ) {
        Text(
            text = chord,
            color = textColor,
            fontSize = 16.sp,
            fontWeight = if (isCurrent) FontWeight.Bold else FontWeight.Normal
        )
    }
}

@Composable
private fun CurrentChordDisplay(
    chord: String,
    confidence: Float,
    rms: Float,
    isListening: Boolean
) {
    val scale by animateFloatAsState(
        targetValue = if (chord != "--" && isListening) 1f else 0.85f,
        animationSpec = spring(dampingRatio = 0.5f, stiffness = 300f),
        label = "chord_scale"
    )

    // Split chord: root + quality (e.g. "Am" → "A" + "m", "C#7" → "C#" + "7")
    val (root, quality) = splitChord(chord)

    // Glow color based on chord type
    val glowColor = when {
        quality.contains("m") && !quality.contains("7") -> Color(0xFF4ECDC4) // minor → teal
        quality.contains("7") -> Color(0xFFFFB74D)                           // 7th → orange
        chord == "--" -> Color.Transparent
        else -> AccentPrimary                                                 // major → violet
    }

    Box(
        modifier = Modifier.scale(scale),
        contentAlignment = Alignment.Center
    ) {
        // Glow background
        if (chord != "--" && isListening) {
            Box(
                modifier = Modifier
                    .size(220.dp)
                    .background(
                        Brush.radialGradient(
                            colors = listOf(
                                glowColor.copy(alpha = 0.18f * confidence),
                                Color.Transparent
                            )
                        ),
                        CircleShape
                    )
            )
        }

        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Row(
                verticalAlignment = Alignment.Top,
                horizontalArrangement = Arrangement.Center
            ) {
                Text(
                    text = root,
                    color = if (chord == "--") TextSecondary.copy(alpha = 0.3f) else TextPrimary,
                    fontSize = 96.sp,
                    fontWeight = FontWeight.Black,
                    lineHeight = 96.sp
                )
                if (quality.isNotEmpty()) {
                    Text(
                        text = quality,
                        color = glowColor,
                        fontSize = 40.sp,
                        fontWeight = FontWeight.Bold,
                        modifier = Modifier.padding(top = 18.dp, start = 4.dp)
                    )
                }
            }

            // Confidence dots
            if (isListening && chord != "--") {
                ConfidenceDots(confidence = confidence)
            }
        }
    }
}

@Composable
private fun ConfidenceDots(confidence: Float) {
    val filled = (confidence * 5).toInt().coerceIn(0, 5)
    Row(
        horizontalArrangement = Arrangement.spacedBy(6.dp),
        modifier = Modifier.padding(top = 8.dp)
    ) {
        repeat(5) { i ->
            Box(
                modifier = Modifier
                    .size(8.dp)
                    .clip(CircleShape)
                    .background(
                        if (i < filled) AccentSecondary else TextSecondary.copy(alpha = 0.2f)
                    )
            )
        }
    }
}

@Composable
private fun LevelBar(rms: Float, isListening: Boolean) {
    val targetWidth = if (isListening) (rms * 800f).coerceIn(0f, 1f) else 0f
    val animWidth by animateFloatAsState(
        targetValue = targetWidth,
        animationSpec = tween(durationMillis = 80, easing = LinearEasing),
        label = "rms_bar"
    )

    Box(
        modifier = Modifier
            .fillMaxWidth(0.7f)
            .height(4.dp)
            .clip(RoundedCornerShape(2.dp))
            .background(SurfaceDark),
        contentAlignment = Alignment.CenterStart
    ) {
        Box(
            modifier = Modifier
                .fillMaxWidth(animWidth)
                .fillMaxHeight()
                .background(
                    Brush.horizontalGradient(
                        listOf(AccentSecondary, AccentPrimary)
                    )
                )
        )
    }
}

@Composable
private fun MicButton(isListening: Boolean, onClick: () -> Unit) {
    val bgColor by animateColorAsState(
        targetValue = if (isListening) AccentPrimary else SurfaceDark,
        animationSpec = tween(300), label = "mic_bg"
    )
    val iconColor by animateColorAsState(
        targetValue = if (isListening) Color.White else TextSecondary,
        animationSpec = tween(300), label = "mic_icon"
    )

    FloatingActionButton(
        onClick = onClick,
        containerColor = bgColor,
        contentColor = iconColor,
        shape = CircleShape,
        modifier = Modifier.size(72.dp),
        elevation = FloatingActionButtonDefaults.elevation(
            defaultElevation = if (isListening) 12.dp else 4.dp
        )
    ) {
        Icon(
            imageVector = if (isListening) Icons.Filled.Mic else Icons.Filled.MicOff,
            contentDescription = if (isListening) "Parar" else "Iniciar",
            modifier = Modifier.size(32.dp)
        )
    }
}

private fun splitChord(chord: String): Pair<String, String> {
    if (chord == "--") return Pair("--", "")
    // Handle: C, C#, Cm, C#m, C7, C#7, Cm7, C#m7
    return when {
        chord.length >= 2 && chord[1] == '#' -> {
            val root = chord.substring(0, 2)
            val quality = chord.substring(2)
            Pair(root, quality)
        }
        chord.length >= 1 -> {
            val root = chord.substring(0, 1)
            val quality = chord.substring(1)
            Pair(root, quality)
        }
        else -> Pair(chord, "")
    }
}
