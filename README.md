# ESP32-P4-NANO VBAN Audio Receive Demo

This project demonstrates how to receive VBAN audio streams on the ESP32-P4-NANO development board.  
It listens for VBAN UDP audio packets, decodes them, and plays the audio through the onboard codec and speaker.

## Features

- Listens for VBAN audio streams on a configurable UDP port (default: 6980)
- Supports mono 16-bit PCM audio at 48kHz (default, configurable)
- Plays received audio in real time via the onboard ES8311 codec and speaker
- DHCP for automatic IP assignment, with mDNS support for easy discovery

## How to Use

### Prerequisites

- ESP32-P4-NANO development board
- PC with ESP-IDF installed
- VBAN-compatible audio sender (e.g., Voicemeeter, VBAN Talkie, or custom sender)

### Build and Flash

*The following steps are for the CLI, but can also be done in the IDE.*

1. Clone this repository.
2. Move to the project directory.
3. Build, flash, and monitor using the following commands (replace `YOUR_SERIAL_PORT` with your actual serial port):
   ```bash
   idf.py build
   idf.py -p YOUR_SERIAL_PORT flash
   idf.py -p YOUR_SERIAL_PORT monitor
   ```
4. Reset the board if necessary.

### Verification

Send a VBAN audio stream from a PC or other device to the ESP32's IP address or mDNS hostname (`esp32-p4-nano.local`) on port 6980.

Ensure the VBAN packet format matches the expected configuration (mono, 16-bit, 48kHz, stream name `TestStream1`).

## Project Structure

```
├── CMakeLists.txt
├── main
│   ├── CMakeLists.txt
│   ├── main.c         // Main application: VBAN receive, audio playback
│   ├── circular_buffer.c/.h // Audio buffering
│   ├── p4nano_audio.c/.h    // Audio and codec initialization
│   ├── network.c/.h         // Ethernet initialization, DHCP, mDNS
│   ├── vban.c/.h            // VBAN protocol handling
└── README.md           // This document
```

## Configuration

Key parameters are defined in `main.c`:
```c
#define VBAN_LISTEN_PORT 6980
#define VBAN_EXPECTED_STREAM "TestStream1"
#define SAMPLE_RATE 48000
#define BIT_DEPTH 16
#define CHANNEL_COUNT 1
#define AUDIO_BUFFER_SIZE 32
```
You can change these values to match your VBAN sender configuration.

## License

This project is licensed under the Apache-2.0 License. See the `LICENSE` file for details.
