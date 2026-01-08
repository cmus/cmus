# Playback Speed Control: Implementation Breakdown

This document provides a comprehensive technical breakdown of the pitch-invariant time-stretching implementation in cmus. It covers the underlying Waveform Similarity Overlap-Add (WSOLA) algorithm, the integration into the cmus audio pipeline, and the handling of real-time constraints.

---

## 1. Underlying Algorithm: WSOLA (Waveform Similarity Overlap-Add)

To change playback speed without altering pitch, we use **WSOLA**. Unlike simple resampling (which changes both speed and pitch by scaling frequencies), WSOLA works in the **time domain** by re-ordering and blending segments of the original audio.

### 1.1 Core Concept
The algorithm seeks to maintain the local periodicity (pitch) of the signal while changing its duration. It does this by:
1.  Taking a "synthesis window" of audio.
2.  Searching for the most "similar" next segment in the original stream at a distance determined by the target speed.
3.  Overlapping and adding these segments to create a continuous, pitch-stable output.

### 1.2 Mathematical Logic & Step Calculation
In `speed.c`, the process is governed by the following relationship:
- **`window_size` (W)**: Total length of a processed segment (40ms).
- **`overlap_size` (L)**: The region used for cross-fading (10ms).
- **`step` (S)**: The nominal distance we move in the *input* for every `(W - L)` samples we produce in the *output*.

**Formula**: `step = (int)((window_size - overlap_size) * speed)`

- If `speed = 2.0`, `step` is doubled, meaning we skip more input audio to produce output faster.
- If `speed = 0.5`, `step` is halved, meaning we consume input more slowly.

### 1.3 Alignment via SAD (Sum of Absolute Differences)
To avoid "glitches" or "phase cancellations," the algorithm doesn't just jump to the next `step`. It performs a **template match** within a `search_range` (15ms).

The function `calc_diff` calculates the similarity between the "overlap buffer" (the tail of the previous output) and potential candidates in the input stream:
```c
long long diff = 0;
for (i = 0; i < limit; i++) {
    int d = a[i] - b[i];
    diff += (d < 0) ? -d : d;
}
```
The algorithm selects the `best_offset` that minimizes this difference, ensuring the waveforms align as closely as possible before blending.

### 1.4 Linear Cross-Fading (The "Add" in Overlap-Add)
Once the best match is found, we blend the previous overlap with the new segment using a linear ramp to ensure a smooth transition:
```c
int32_t w2 = (i * 1024) / s->overlap_size; // Weight of new segment
int32_t w1 = 1024 - w2;                   // Weight of previous overlap
output = (v1 * w1 + v2 * w2) >> 10;
```

---

## 2. Integration into the cmus Pipeline

### 2.1 The Producer-Consumer Model
cmus uses a decoupled architecture:
1.  **Producer Thread (`producer_loop`)**: Reads raw data from input plugins (MP3, FLAC, etc.) and fills the `player_buffer` in chunks (typically 4KB).
2.  **Consumer Thread (`consumer_loop`)**: Reads from the `player_buffer`, applies software volume/replaygain, and writes to the output backend (PulseAudio, ALSA).

### 2.2 Point of Interception
The speed control is injected into the **Consumer Thread** (`player.c`), specifically between the buffer retrieval and the hardware write:

```text
[player_buffer] -> [speed_stretcher] -> [scale_samples (Volume)] -> [op_write (Hardware)]
```

This placement is critical because:
- It allows us to track `consumer_pos` (the track progress) accurately by recording how many samples were *consumed* from the original buffer, regardless of how many were *produced* by the stretcher.
- It ensures that volume and ReplayGain are applied to the *final* stretched audio, avoiding potential clipping or precision loss during the stretching process.

### 2.3 Internal Buffering & Accumulation
A significant challenge was the "Chunk Mismatch." 
- cmus chunks are often ~23ms long.
- WSOLA requires a minimum "Look-ahead" of `Step + Search + Window` (approx 65ms-100ms) to find a valid match.

To solve this, the `speed_stretcher` maintains an internal `in_buf` (200ms capacity).
1.  The `consumer_loop` feeds whatever it has into `in_buf`.
2.  `speed_stretcher_process` only runs the WSOLA loop if `in_buf_fill` exceeds the required threshold.
3.  Any unused samples remain in `in_buf` for the next call.

---

## 3. UI and Control Flow

### 3.1 Global State
The `playback_speed` is a global double in `player.c`. When changed via the `:speed` command:
1.  The value is clamped between 0.25x and 2.0x.
2.  The `status_changed` flag is set, triggering a UI refresh.

### 3.2 UI Integration (`ui_curses.c`)
The status line uses a conditional format: `%{?speed?speed: %{speed}x | }`. 
The `TF_SPEED` token is dynamically populated. If `speed == 1.0`, the token is cleared to keep the UI minimal.

---

## 4. Current Limitations & Technical Debt

- **Fixed Format**: Currently hardcoded for 16-bit Stereo. 
- **Latency**: The 200ms accumulation buffer introduces a slight delay in "Speed Change" responsiveness, as the existing buffer must be cleared or processed before the new speed is fully audible.
- **Complexity**: The linear ramp is a first-order approximation; higher-order (cosine) windows could further reduce spectral leakage (artifacts).