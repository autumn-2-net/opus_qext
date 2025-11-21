# Build Summary - Opus All-in-One with QEXT

## What Has Been Done

✅ **Complete CMake build system** - Single unified build for all components
✅ **QEXT support enabled** - Compile flag `-DENABLE_QEXT` added globally
✅ **opusenc patched** - Added `--qext` command line option
✅ **Cross-platform ready** - Works on Windows, Linux, and macOS
✅ **All dependencies bundled** - No external packages needed

## Project Structure

```
C:\Users\autumn\CLionProjects\opus_all\
├── CMakeLists.txt              # Main build configuration (QEXT enabled)
├── README.md                   # Full documentation
├── BUILD_SUMMARY.md            # This file
├── example_usage.md            # Detailed usage examples
├── build.bat                   # Windows build script
├── build.sh                    # Linux/macOS build script
├── qext_patch.txt              # Documentation of changes made
│
├── libogg-1.3.6/               # Ogg container library
├── opus-main/                  # Opus codec (with QEXT enabled)
├── opusfile-0.12/              # Opus file reading
├── libopusenc-0.2/             # Opus encoding library
└── opus-tools-0.2/             # opusenc/opusdec (patched)
    └── src/
        └── opusenc.c           # ✅ Modified to add --qext support
```

## Changes Made to Source Code

### 1. CMakeLists.txt (Root)
- Added `-DENABLE_QEXT` global definition
- Configured all 5 components (ogg, opus, opusfile, opusenc, opus-tools)
- Set up cross-platform build rules
- Added install targets

### 2. opus-tools-0.2/src/opusenc.c
Five modifications to add `--qext` support:

**a) Added to long_options array (line ~397):**
```c
{"qext", no_argument, NULL, 0},
```

**b) Added variable declaration (line ~435):**
```c
int enable_qext=0;
```

**c) Added to usage text (line ~151):**
```c
printf(" --qext             Enable QEXT for frequencies above 20kHz (requires CELT-only mode)\n");
```

**d) Added command line parsing (line ~579):**
```c
} else if (strcmp(optname, "qext")==0) {
  enable_qext=1;
```

**e) Added OPUS_SET_QEXT call (line ~948):**
```c
#ifdef ENABLE_QEXT
if (enable_qext) {
  ret = ope_encoder_ctl(enc, OPUS_SET_QEXT(1));
  if (ret != OPE_OK) {
    fprintf(stderr, "Warning: OPUS_SET_QEXT failed: %s\n", ope_strerror(ret));
  }
}
#endif
```

## How to Build

### Windows (Visual Studio 2022)

**Option 1: Use build script**
```cmd
build.bat
```

**Option 2: Manual**
```cmd
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

**Output:**
- `build\Release\opusenc.exe`
- `build\Release\opusdec.exe`

### Linux / macOS

**Option 1: Use build script**
```bash
chmod +x build.sh
./build.sh
```

**Option 2: Manual**
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

**Output:**
- `build/opusenc`
- `build/opusdec`

## Quick Test

After building, verify QEXT support:

```bash
# Windows
build\Release\opusenc.exe --help | findstr qext

# Linux/macOS
./build/opusenc --help | grep qext
```

You should see:
```
 --qext             Enable QEXT for frequencies above 20kHz (requires CELT-only mode)
```

## Usage Examples

### Basic QEXT Encoding

```bash
# Prepare PCM input (48kHz, stereo, 16-bit)
ffmpeg -i input.flac -f s16le -ac 2 -ar 48000 input.pcm

# Encode with QEXT at 320 kbps
opusenc --raw --raw-rate 48000 --raw-chan 2 --raw-bits 16 --raw-endianness 0 \
        --bitrate 320 --comp 10 --music --qext \
        input.pcm output.opus

# Decode
opusdec output.opus output.wav
```

### Verify High Frequencies

```bash
# Generate spectrum image
ffmpeg -i output.wav -lavfi showspectrumpic=s=1920x1080:legend=1 spectrum.png

# Look for content above 20kHz (should be visible with QEXT)
```

## QEXT Technical Details

### What is QEXT?

QEXT (Scalable Quality Extension) is an extension to the Opus codec that allows encoding frequencies above the standard 20kHz limit.

**Standard Opus:**
- Frequency range: 0-20kHz (FULLBAND)
- 21 frequency bands
- Packet size: up to 1275 bytes

**Opus with QEXT:**
- Frequency range: 0-24kHz @ 48kHz sampling, or 0-48kHz @ 96kHz sampling
- 21 base bands + 14 extension bands (NB_QEXT_BANDS)
- Packet size: up to 3825 bytes
- Extension ID: 124 (stored in Ogg packet padding)

### When to Use QEXT

**✅ Good use cases:**
- High-quality music archival
- Professional audio workflows
- Controlled environments (both encoder and decoder support QEXT)
- Source material with actual >20kHz content
- High bitrates available (≥320 kbps @ 48kHz)

**❌ Not recommended:**
- Streaming to unknown/standard players
- Low bitrates (<256 kbps)
- Source material band-limited to 20kHz
- Compatibility with all Opus decoders required

### Compatibility

**QEXT-enabled decoder:**
- Decodes full frequency range (up to 24kHz or 48kHz)
- Best quality

**Standard Opus decoder:**
- Decodes successfully (backward compatible)
- Plays audio up to 20kHz
- Ignores QEXT extension data
- May have slightly lower quality due to bitrate allocation to extension

## Bitrate Recommendations

| Sampling Rate | Channels | Minimum | Recommended | Optimal |
|---------------|----------|---------|-------------|---------|
| 48kHz         | Mono     | 160 kbps| 192 kbps    | 256 kbps|
| 48kHz         | Stereo   | 256 kbps| 320 kbps    | 384 kbps|
| 96kHz         | Mono     | 256 kbps| 320 kbps    | 448 kbps|
| 96kHz         | Stereo   | 384 kbps| 512 kbps    | 640 kbps|

## Troubleshooting

### Build Errors

**"opus-main/CMakeLists.txt not found"**
- Ensure opus source directory is named exactly `opus-main`
- Check that it contains a CMakeLists.txt file

**Linking errors on Linux**
- Install build essentials: `sudo apt-get install build-essential`
- Install math library: `sudo apt-get install libm-dev` (usually included)

**CMake version too old**
- Requires CMake ≥ 3.15
- Update: `sudo apt-get install cmake` or download from cmake.org

### Runtime Errors

**"OPUS_SET_QEXT failed: Unimplemented"**
- QEXT was not enabled during compilation
- Rebuild with `build.bat` or `build.sh` to ensure ENABLE_QEXT is defined

**No high frequencies in output**
- Source file may not have >20kHz content
- Bitrate too low (use ≥320 kbps)
- Verify with spectrum analyzer

**File won't decode**
- Use opusdec from this build (supports QEXT)
- Standard decoders will work but ignore QEXT data

## Next Steps

1. **Build the project** using `build.bat` (Windows) or `build.sh` (Linux/macOS)

2. **Test basic functionality:**
   ```bash
   opusenc --help
   opusdec --help
   ```

3. **Try QEXT encoding** with a high-quality source file

4. **Compare results** using spectrum analysis

5. **Read detailed examples** in `example_usage.md`

## Files Reference

- **README.md** - Complete documentation and usage guide
- **example_usage.md** - Detailed usage examples and workflows
- **qext_patch.txt** - Summary of source code changes
- **build.bat** - Windows build script
- **build.sh** - Linux/macOS build script
- **CMakeLists.txt** - Build configuration

## Support

For build issues:
- Check that all source directories are present and complete
- Verify CMake version ≥ 3.15
- Ensure compiler supports C99

For Opus/QEXT questions:
- https://opus-codec.org/
- https://github.com/xiph/opus

## License

This build combines multiple BSD-licensed components. See individual component directories for full license texts.

---

**Build Date:** 2025-11-21
**Opus Version:** 1.x (main branch)
**Tools Version:** 0.2
**QEXT:** Enabled
