# opus-qext

Python bindings for the Opus audio codec with QEXT quality extensions (based on Opus 1.4.6).

## Features

- **QEXT mode**: Extended quality encoding up to 2048 kbps per channel
- **Full bandwidth**: Preserves audio content up to 24 kHz (48 kHz sample rate)
- **Multiple APIs**: File I/O, WAV bytes, NumPy arrays, PyTorch tensors
- **Any sample rate**: Automatic internal resampling with high-quality Speex resampler

## Installation

```bash
pip install opus-qext
pip install opus-qext[numpy]   # with NumPy support
pip install opus-qext[torch]   # with PyTorch support
```

## Quick Start

```python
import opus_qext

# File-to-file
opus_qext.encode_file("input.wav", "output.opus", bitrate=256000, enable_qext=True)
opus_qext.decode_file("output.opus", "decoded.wav")

# WAV bytes
with open("input.wav", "rb") as f:
    opus_data = opus_qext.encode_wav(f.read(), bitrate=256000, enable_qext=True)
wav_data = opus_qext.decode_to_wav(opus_data)

# NumPy arrays
from opus_qext import opus_numpy
opus_bytes = opus_numpy.encode_ndarray(audio_array, sample_rate=48000, bitrate=256000, enable_qext=True)
audio, sr, ch = opus_numpy.decode_to_ndarray(opus_bytes)

# PyTorch tensors
from opus_qext import opus_torch
opus_bytes = opus_torch.encode_tensor(waveform, sample_rate=48000, bitrate=256000, enable_qext=True)
waveform, sr, ch = opus_torch.decode_to_tensor(opus_bytes)
```

## Platform Support

Pre-built wheels include the native `opus_qext_api` library for:
- Windows (x86_64)

## License

BSD-3-Clause
