# Opus QEXT Usage Examples

## Quick Start

### 1. Build the project

**Windows:**
```cmd
build.bat
```

**Linux/macOS:**
```bash
chmod +x build.sh
./build.sh
```

### 2. Basic encoding test

```bash
# Windows
build\Release\opusenc.exe --help

# Linux/macOS
./build/opusenc --help
```

## Complete Workflow Examples

### Example 1: Standard Opus Encoding (no QEXT)

```bash
# Step 1: Convert source audio to PCM
ffmpeg -i music.flac -f s16le -ac 2 -ar 48000 music.pcm

# Step 2: Encode to Opus at 256 kbps
opusenc --raw --raw-rate 48000 --raw-chan 2 --raw-bits 16 --raw-endianness 0 \
        --bitrate 256 --comp 10 \
        music.pcm music.opus

# Step 3: Decode back to WAV
opusdec music.opus music_decoded.wav

# Step 4: Compare quality
ffmpeg -i music_decoded.wav -i music.flac -filter_complex "[0:a][1:a]amerge,showspectrumpic=s=1920x1080" spectrum.png
```

### Example 2: QEXT Encoding (48kHz, frequencies up to 24kHz)

```bash
# Step 1: Prepare high-quality 48kHz PCM (ensure source has >20kHz content)
ffmpeg -i high_quality_source.flac -f s16le -ac 2 -ar 48000 hq_48k.pcm

# Step 2: Encode with QEXT at 320 kbps
opusenc --raw --raw-rate 48000 --raw-chan 2 --raw-bits 16 --raw-endianness 0 \
        --bitrate 320 --comp 10 --music --qext \
        hq_48k.pcm hq_qext.opus

# Step 3: Decode with QEXT support
opusdec hq_qext.opus hq_qext_decoded.wav

# Step 4: Verify high frequencies are preserved
ffmpeg -i hq_qext_decoded.wav -lavfi showspectrumpic=s=1920x1080:legend=1 spectrum_qext.png
```

### Example 3: QEXT Encoding (96kHz, frequencies up to 48kHz)

```bash
# Step 1: Prepare 96kHz PCM
ffmpeg -i ultra_hq_source.flac -f s16le -ac 2 -ar 96000 uhq_96k.pcm

# Step 2: Encode with QEXT at 512 kbps
opusenc --raw --raw-rate 96000 --raw-chan 2 --raw-bits 16 --raw-endianness 0 \
        --bitrate 512 --comp 10 --music --qext \
        uhq_96k.pcm uhq_qext_96k.opus

# Step 3: Decode
opusdec uhq_qext_96k.opus uhq_qext_96k_decoded.wav

# Step 4: Convert back to FLAC for archival
ffmpeg -i uhq_qext_96k_decoded.wav -c:a flac uhq_qext_96k_decoded.flac
```

### Example 4: Batch Processing

**Windows batch script (process_all.bat):**
```batch
@echo off
setlocal enabledelayedexpansion

set OPUSENC=build\Release\opusenc.exe
set BITRATE=320

for %%f in (*.flac) do (
    echo Processing: %%f
    
    REM Convert to PCM
    ffmpeg -i "%%f" -f s16le -ac 2 -ar 48000 -y "%%~nf.pcm"
    
    REM Encode with QEXT
    %OPUSENC% --raw --raw-rate 48000 --raw-chan 2 --raw-bits 16 --raw-endianness 0 ^
              --bitrate %BITRATE% --comp 10 --music --qext ^
              "%%~nf.pcm" "%%~nf.opus"
    
    REM Clean up PCM
    del "%%~nf.pcm"
    
    echo Done: %%~nf.opus
    echo.
)

echo All files processed!
pause
```

**Linux/macOS bash script (process_all.sh):**
```bash
#!/bin/bash

OPUSENC=./build/opusenc
BITRATE=320

for file in *.flac; do
    basename="${file%.flac}"
    echo "Processing: $file"
    
    # Convert to PCM
    ffmpeg -i "$file" -f s16le -ac 2 -ar 48000 -y "${basename}.pcm"
    
    # Encode with QEXT
    $OPUSENC --raw --raw-rate 48000 --raw-chan 2 --raw-bits 16 --raw-endianness 0 \
             --bitrate $BITRATE --comp 10 --music --qext \
             "${basename}.pcm" "${basename}.opus"
    
    # Clean up PCM
    rm "${basename}.pcm"
    
    echo "Done: ${basename}.opus"
    echo ""
done

echo "All files processed!"
```

## Comparing QEXT vs Standard

### Create comparison files

```bash
# Standard Opus (20kHz limit)
opusenc --raw --raw-rate 48000 --raw-chan 2 --raw-bits 16 --raw-endianness 0 \
        --bitrate 320 --comp 10 --music \
        input.pcm standard.opus

# QEXT Opus (24kHz with 48kHz sampling)
opusenc --raw --raw-rate 48000 --raw-chan 2 --raw-bits 16 --raw-endianness 0 \
        --bitrate 320 --comp 10 --music --qext \
        input.pcm qext.opus

# Decode both
opusdec standard.opus standard_decoded.wav
opusdec qext.opus qext_decoded.wav

# Generate spectrum comparison
ffmpeg -i standard_decoded.wav -lavfi showspectrumpic=s=1920x1080:legend=1 standard_spectrum.png
ffmpeg -i qext_decoded.wav -lavfi showspectrumpic=s=1920x1080:legend=1 qext_spectrum.png
```

### ABX blind test

```bash
# Create test files
opusdec standard.opus standard.wav
opusdec qext.opus qext.wav

# Use foobar2000 ABX plugin or similar tool to blind test
# You should hear difference in high-frequency content (cymbals, hi-hats, air)
```

## Verifying QEXT is Working

### Check encoder output

When encoding with `--qext`, you should see no errors. If QEXT is not compiled in, you'll see:
```
Warning: OPUS_SET_QEXT failed: Unimplemented
```

### Check file size

QEXT files are typically larger due to extra frequency bands:
```bash
# Compare file sizes
ls -lh standard.opus qext.opus

# QEXT file should be 10-20% larger at same bitrate
```

### Spectrum analysis

```bash
# Generate detailed spectrum
ffmpeg -i qext_decoded.wav -lavfi showspectrumpic=s=3840x2160:legend=1:color=intensity qext_spectrum_hires.png

# Look for content above 20kHz (should be visible with QEXT, absent without)
```

### Using sox for frequency analysis

```bash
# Install sox if needed: apt-get install sox / brew install sox

# Generate spectrogram
sox qext_decoded.wav -n spectrogram -o qext_spectrogram.png -x 3000 -y 513 -z 120

# Check frequency response
sox qext_decoded.wav -n stat -freq
```

## Troubleshooting

### No high-frequency content in output

**Possible causes:**
1. Source file doesn't have >20kHz content
2. QEXT not enabled during compilation
3. Bitrate too low (use ≥320 kbps)
4. Decoder doesn't support QEXT

**Solutions:**
```bash
# Verify source has high frequencies
ffmpeg -i source.flac -af "highpass=f=20000" -f null -

# Check if encoder was built with QEXT
strings opusenc | grep QEXT

# Use higher bitrate
opusenc ... --bitrate 384 ... --qext ...
```

### Decoder compatibility

Standard Opus decoders (without QEXT) will:
- Successfully decode the file
- Play audio up to 20kHz
- Ignore QEXT extension data
- May have slightly lower quality due to bitrate allocation

QEXT-enabled decoders will:
- Decode full frequency range
- Restore content above 20kHz
- Provide best quality

## Performance Tips

### Faster encoding (lower complexity)

```bash
# Use complexity 5 instead of 10 for faster encoding
opusenc ... --comp 5 --qext ...

# Trade-off: slightly lower quality, much faster
```

### Parallel batch processing

```bash
# Linux/macOS with GNU parallel
find . -name "*.flac" | parallel -j 4 'opusenc --raw --raw-rate 48000 --raw-chan 2 --raw-bits 16 --raw-endianness 0 --bitrate 320 --comp 10 --music --qext <(ffmpeg -i {} -f s16le -ac 2 -ar 48000 -) {.}.opus'
```

## Integration with Audio Players

### VLC
- Supports standard Opus
- QEXT extensions ignored (plays up to 20kHz)

### foobar2000
- Supports standard Opus
- Use QEXT-enabled decoder component if available

### mpv
- Supports standard Opus
- QEXT extensions ignored

### Custom player
- Link against this QEXT-enabled libopus
- Use opusfile API for decoding
- Full QEXT support

## Recommended Workflows

### Music Archival (Best Quality)
```bash
# 96kHz, 512 kbps, QEXT
ffmpeg -i source.flac -f s16le -ac 2 -ar 96000 temp.pcm
opusenc --raw --raw-rate 96000 --raw-chan 2 --raw-bits 16 --raw-endianness 0 \
        --bitrate 512 --comp 10 --music --qext \
        temp.pcm archive.opus
rm temp.pcm
```

### Streaming (Balanced)
```bash
# 48kHz, 256 kbps, no QEXT (better compatibility)
ffmpeg -i source.flac -f s16le -ac 2 -ar 48000 temp.pcm
opusenc --raw --raw-rate 48000 --raw-chan 2 --raw-bits 16 --raw-endianness 0 \
        --bitrate 256 --comp 10 --music \
        temp.pcm stream.opus
rm temp.pcm
```

### Professional Audio (Maximum Quality)
```bash
# 96kHz, 640 kbps, QEXT, complexity 10
ffmpeg -i source.wav -f s16le -ac 2 -ar 96000 temp.pcm
opusenc --raw --raw-rate 96000 --raw-chan 2 --raw-bits 16 --raw-endianness 0 \
        --bitrate 640 --comp 10 --music --qext \
        temp.pcm professional.opus
rm temp.pcm
```
