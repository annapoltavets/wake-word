# M_6_9 — ESP32-S3 FreeRTOS wake word

Voice control of an LED on the ESP32-S3: an I2S microphone is sampled continuously, a 1-second sliding window is classified, and the result switches the LED on, off, or toggles it.

> **Status:** the Edge Impulse model (`components/edge_impulse`, keyword `carlos`) is integrated: saying the keyword toggles the LED. The old loudness stub is still available via `WW_USE_ENERGY_STUB` in `src/wake_word.c`.

## Hardware

| Part | Pin |
|------|-----|
| I2S mic BCLK (INMP441 / SPH0645 or similar) | GPIO 4 |
| I2S mic WS | GPIO 5 |
| I2S mic DIN | GPIO 6 |
| LED | GPIO 15 |

- Board: `esp32-s3-devkitc-1` (16 MB flash, 8 MB octal PSRAM).
- INMP441: connect L/R to GND (left channel). Audio is 16 kHz, 32-bit slot, mono.

### Wiring diagram

```mermaid
flowchart LR
    subgraph MIC["I2S microphone (INMP441)"]
        M_VDD["VDD"]
        M_GND["GND"]
        M_SCK["SCK (BCLK)"]
        M_WS["WS"]
        M_SD["SD (DOUT)"]
        M_LR["L/R"]
    end

    subgraph ESP["ESP32-S3 DevKitC-1"]
        E_3V3["3V3"]
        E_GND["GND"]
        E_G4["GPIO 4"]
        E_G5["GPIO 5"]
        E_G6["GPIO 6"]
        E_G15["GPIO 15"]
    end

    subgraph LEDC["LED circuit"]
        R["Resistor ~220 Ω"]
        LED["LED"]
        L_GND["GND"]
    end

    E_3V3 --- M_VDD
    E_GND --- M_GND
    E_GND --- M_LR
    E_G4 -- "BCLK" --> M_SCK
    E_G5 -- "WS" --> M_WS
    M_SD -- "DIN" --> E_G6
    E_G15 --> R --> LED --> L_GND
```

### Algorithm

```mermaid
flowchart TD
    A([app_main]) --> B["led_init, audio_init, wake_word_init"]
    B --> C["Create tasks: led_task, audio_task, inference_task"]

    subgraph AT["audio_task (core 1, prio 6)"]
        D["i2s_channel_read: 512 samples, 32 bit"] --> E["Shift >> 16 and clamp to int16"]
        E --> F{"audio_queue full?"}
        F -- yes --> G["Log warning, drop frame"]
        F -- no --> H["xQueueSend frame"]
    end

    subgraph IT["inference_task (core 0, prio 5)"]
        I["xQueueReceive frame"] --> J["Slide 1 s window: memmove + append frame"]
        J --> K{"cooldown > 0?"}
        K -- yes --> L["cooldown--"] --> I
        K -- no --> M{"8 frames collected?"}
        M -- no --> I
        M -- yes --> N["wake_word_process(window)"]
        N --> O{"label != NONE and score >= 0.6?"}
        O -- no --> I
        O -- yes --> P["led_set_state(cmd): take mutex, change state, give mutex, notify led_task"]
        P --> Q["cooldown = 31 frames"] --> I
    end

    subgraph LT["led_task (core 1, prio 4)"]
        R1["ulTaskNotifyTake (wait)"] --> S["Take mutex, read led_level, give mutex"]
        S --> W["gpio_set_level GPIO 15"]
        W --> R1
    end

    H -. audio_queue .-> I
    P -. "mutex-protected led_level + notify" .-> R1
```

## Build and run

Requires [PlatformIO](https://platformio.org/).

```bash
pio run                       # build
pio run -t upload -t monitor  # flash and open serial monitor (115200)
pio run -t clean
```

## How it works

```
mic → audio_task → audio_queue → inference_task → [mutex: led_level] → led_task → LED
      (core 1)                    (core 0)                    (core 1)
```

| File | Role |
|------|------|
| `src/audio.c/.h` | I2S setup; reads 512-sample (32 ms) frames, converts 32→16 bit, pushes to `audio_queue` |
| `src/wake_word.c/.h` | Classifier interface (`wake_word_process`) returning `WW_NONE/ON/OFF/TOGGLE` plus a score; currently the energy stub |
| `src/main.c` | `inference_task`: maintains the 1 s window, runs inference every 256 ms, 1 s cooldown after a detection |
| `src/led.c/.h` | `led_set_state()` changes the shared LED state under a mutex and notifies `led_task`, which reads the state under the mutex and drives GPIO 15 |

Inference runs on core 0 so it does not interfere with I2S reads on core 1.

## Updating the model

1. Export the C++ library from Edge Impulse Studio and replace `edge-impulse-sdk/`, `model-parameters/`, `tflite-model/` in `components/edge_impulse/`.
2. If the labels changed, update `WW_KEYWORD_LABEL` (and the label→command mapping) in `src/wake_word.c`.
3. Make sure `WW_WINDOW_SAMPLES` (`wake_word.h`) matches the model's input (checked at startup in `wake_word_init()`), and tune `WW_THRESHOLD`.
