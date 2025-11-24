# Opus All-in-One with QEXT Support

This is a unified build of Opus encoder/decoder tools with QEXT (Scalable Quality Extension) support for encoding frequencies above 20kHz.

## Features

- **QEXT Support**: Encode frequencies above 20kHz (up to 24kHz @ 48kHz sampling, or 48kHz @ 96kHz sampling)
- **Cross-platform**: Works on Windows, Linux, and macOS
- **Single CMake build**: All dependencies bundled, no external packages needed
- **Based on official Opus tools**: opusenc 0.2, opusdec 0.2

## Building

### Windows (Visual Studio)

```cmd
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Executables will be in: `build\Release\opusenc.exe` and `build\Release\opusdec.exe`

### Windows (MinGW)

```cmd
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
```

### Linux / macOS

```bash
cmake -S . -B build
cmake --build build -j$(nproc)
```

Executables will be in: `build/opusenc` and `build/opusdec`

## Usage

### Basic Encoding (without QEXT)

```bash
# Encode WAV to Opus at 256 kbps
opusenc --bitrate 256 input.wav output.opus

# Encode raw PCM (48kHz, stereo, 16-bit) to Opus
opusenc --raw --raw-rate 48000 --raw-chan 2 --raw-bits 16 --raw-endianness 0 --bitrate 256 input.pcm output.opus
```

### Encoding with QEXT (frequencies above 20kHz)

**Important**: QEXT works best at high bitrates (≥320 kbps for stereo @ 48kHz) and requires CELT-only mode.

```bash
# 48kHz input, stereo, 320 kbps with QEXT
opusenc --raw --raw-rate 48000 --raw-chan 2 --raw-bits 16 --raw-endianness 0 \
        --bitrate 320 --comp 10 --music --qext \
        input.pcm output_qext.opus

# 96kHz input (for even higher frequencies), stereo, 512 kbps with QEXT
opusenc --raw --raw-rate 96000 --raw-chan 2 --raw-bits 16 --raw-endianness 0 \
        --bitrate 512 --comp 10 --music --qext \
        input_96k.pcm output_qext_96k.opus
```

### Decoding

```bash
# Decode Opus to WAV
opusdec output.opus decoded.wav

# Decode QEXT-encoded Opus (automatically handles QEXT if present)
opusdec output_qext.opus decoded_qext.wav
```

## QEXT Notes

1. **Sampling Rate**: 
   - 48kHz: Can encode up to ~24kHz (above the standard 20kHz limit)
   - 96kHz: Can encode up to ~48kHz
   
2. **Bitrate Recommendations**:
   - 48kHz stereo: ≥320 kbps
   - 96kHz stereo: ≥512 kbps
   - Lower bitrates may cause quality degradation on non-QEXT decoders

3. **Compatibility**:
   - QEXT data is stored as an extension in the Ogg packet
   - Standard decoders will ignore QEXT and play the base audio (up to 20kHz)
   - QEXT-enabled decoders will restore frequencies above 20kHz

4. **Best Use Cases**:
   - High-quality music archival
   - Professional audio workflows
   - Scenarios where both encoder and decoder are under your control

## Converting Audio Files

### Using ffmpeg to prepare PCM input

```bash
# Convert any audio to 48kHz stereo 16-bit PCM
ffmpeg -i input.flac -f s16le -ac 2 -ar 48000 input.pcm

# Convert to 96kHz for QEXT
ffmpeg -i input.flac -f s16le -ac 2 -ar 96000 input_96k.pcm
```

### Converting PCM back to WAV

```bash
# 48kHz stereo
ffmpeg -f s16le -ar 48000 -ac 2 -i decoded.pcm output.wav

# 96kHz stereo
ffmpeg -f s16le -ar 96000 -ac 2 -i decoded_96k.pcm output_96k.wav
```

## Project Structure

```
opus_all/
├── CMakeLists.txt          # Main build configuration
├── README.md               # This file
├── libogg-1.3.6/           # Ogg container library
├── opus-main/              # Opus codec library (with QEXT enabled)
├── opusfile-0.12/          # Opus file reading library
├── libopusenc-0.2/         # Opus encoding library
└── opus-tools-0.2/         # opusenc/opusdec tools (patched for QEXT)
```

## Technical Details

- **QEXT**: Enabled via `-DENABLE_QEXT` compile flag
- **Frequency bands**: Standard Opus uses 21 bands up to 20kHz; QEXT adds 14 additional bands
- **Extension ID**: 124 (stored in Ogg packet padding)
- **Packet size**: Up to 3825 bytes (vs standard 1275 bytes)

## Troubleshooting

### Build Issues

**Error: opus-main/CMakeLists.txt not found**
- Ensure the opus source directory is named `opus-main` and contains a CMakeLists.txt

**Linking errors on Linux**
- Make sure you have `libm` (math library) installed
- Try: `sudo apt-get install build-essential`

### Runtime Issues

**opusenc: command not found**
- Add the build directory to PATH, or use the full path to the executable

**QEXT not working**
- Verify ENABLE_QEXT is defined during compilation (check CMake output)
- Use high bitrates (≥320 kbps)
- Ensure input has actual content above 20kHz (check with spectrum analyzer)

## License

This project combines multiple open-source components:
- libogg: BSD-style license
- Opus: BSD license
- opus-tools: BSD license

See individual component directories for full license texts.

## Credits

- Opus codec: Xiph.Org Foundation
- QEXT extension: Part of the Opus development branch
- Build integration: Custom CMake configuration

## Support

For issues specific to this build:
- Check that all source directories are properly extracted
- Verify CMake version ≥ 3.15
- Ensure compiler supports C99

For Opus-specific questions:
- https://opus-codec.org/
- https://github.com/xiph/opus
