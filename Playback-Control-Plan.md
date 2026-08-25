# Playback Speed Control Implementation Plan

This document outlines the architectural and logic changes required to implement real-time playback speed control (0.25x to 2.0x) in cmus, using the `[` and `]` keys, while maintaining pitch.

---

## 1. Core Logic & Architecture

### Global State Management
- **Variable**: Define a global floating-point variable `double playback_speed` (default `1.0`) in `player.h/c`.
- **Constraints**: Enforce a hard range of `0.25` to `2.0`.
- **Granularity**: Use a constant increment value of `0.25`.
- **Thread Safety**: Access to `playback_speed` should be protected if necessary, although simple double reads/writes on most architectures are atomic enough for this non-critical UI feedback.

---

## 2. Algorithm: Pitch-Invariant Time-Stretching

### Primary Method: WSOLA (Waveform Similarity Overlap-Add)
- **Mechanism**: Operates in the time domain. It adjusts the playback rate by overlapping and adding segments of the audio.
- **Pitch Preservation**: It finds similar waveforms in the overlap region to avoid phase discontinuities (clicks) and maintains the original pitch by not changing the sample rate of the output.

---

## 3. Integration & Code Snippets

### A. Player Engine State (`player.h` & `player.c`)

**`player.h`**:
```c
extern double playback_speed;
void player_set_speed(double speed);
```

**`player.c`**:
```c
double playback_speed = 1.0;

void player_set_speed(double speed)
{
    if (speed < 0.25) speed = 0.25;
    if (speed > 2.0) speed = 2.0;
    playback_speed = speed;
    // Notify UI or update player state if needed
}
```

### B. Command Handling (`command_mode.c`)

Implement a new command `:speed` and register it.

```c
static void cmd_speed(int argc, char *argv[])
{
    double val;
    if (argc != 1) {
        error_msg("usage: speed [+|-]VALUE");
        return;
    }
    // Logic to parse absolute or relative (+/-) speed
    if (argv[0][0] == '+' || argv[0][0] == '-') {
        val = playback_speed + atof(argv[0]);
    } else {
        val = atof(argv[0]);
    }
    player_set_speed(val);
}

// In commands array:
{ "speed", cmd_speed, 1, 1, NULL, 0, 0 },
```

### C. Key Bindings (`keys.c` / `data/rc`)

Override the existing bindings for `[` and `]`.

```c
// Change from:
// { "common", "[", "vol -1%" },
// { "common", "]", "vol +1%" },
// To:
{ "common", "[", "speed -0.25" },
{ "common", "]", "speed +0.25" },
```

### D. Audio Processing Hook (`player.c`)

Modify `_producer_read` to pass data through a speed-change function.

```c
static void _producer_read(void)
{
    // ...
    char *wpos;
    int size = buffer_get_wpos(&wpos);
    
    if (playback_speed == 1.0) {
        nr_read = ip_read(ip, wpos, size);
    } else {
        // 1. Read into intermediate buffer
        // 2. Process with WSOLA (time-stretch)
        // 3. Write resulting PCM to 'wpos'
        // nr_read = wsola_process(ip, wpos, size, playback_speed);
    }
    // ...
}
```

### E. UI Feedback (`ui_curses.c` & `options.c`)

**`ui_curses.c`**: Add a new format option `TF_SPEED`.

```c
// In update_statusline() or similar:
fopt_set_str(&track_fopts[TF_SPEED], buf_speed);
```

**`options.c`**: Update the default `statusline_format`.

```c
[FMT_STATUSLINE] = { "format_statusline",
    // ...
    "%{?speed!=1.0?speed: %{speed}x | }"
    "%{?volume>=0?%{?lvolume!=rvolume?%{lvolume}%% %{rvolume}?%{volume}}%% | }"
    // ...
},
```

---

## 4. Visual Feedback Mechanism

- **Status Line**: The speed will be displayed in the status line (e.g., `speed: 1.25x`).
- **Dynamic Visibility**: Using cmus's format strings (`%{?speed!=1.0?...}`), the speed indicator will only appear when the speed is not the default `1.0x`, keeping the UI clean during normal playback.

---

## 5. Third-Party Libraries

- **Recommendation**: For a clean implementation, consider **`libsoxr`** (Varying-speed resampling) or a minimal standalone WSOLA C implementation. `libsoxr` is highly efficient and handles the complexity of high-quality time-stretching if included. If the project prefers no new dependencies, a small WSOLA implementation (approx 200-300 lines of C) should be integrated directly into a new `speed.c/h` utility.