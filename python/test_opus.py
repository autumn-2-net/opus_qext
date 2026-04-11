"""Tests for opus_qext Python bindings.

Run from project root:
    python -m pytest python/test_opus.py -v
Or:
    python python/test_opus.py
"""

import os
import sys
import struct
import tempfile
import wave

import numpy as np

# Ensure the python package is importable
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from python import opus_codec, opus_numpy
from python.opus_codec import OpusError


def make_wav_bytes(sample_rate=48000, channels=2, duration_s=1.0,
                   freq=440.0, bits_per_sample=16):
    """Generate a WAV file as bytes with a sine wave."""
    num_samples = int(sample_rate * duration_s)

    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as tmp:
        tmp_path = tmp.name

    try:
        with wave.open(tmp_path, "w") as wf:
            wf.setnchannels(channels)
            wf.setsampwidth(bits_per_sample // 8)
            wf.setframerate(sample_rate)

            for i in range(num_samples):
                t = i / sample_rate
                val = int(32767 * 0.5 * np.sin(2 * np.pi * freq * t))
                for _ in range(channels):
                    wf.writeframes(struct.pack("<h", val))

        with open(tmp_path, "rb") as f:
            return f.read()
    finally:
        os.unlink(tmp_path)


def make_pcm_float(sample_rate=48000, channels=2, duration_s=1.0, freq=440.0):
    """Generate interleaved float32 PCM data as ndarray."""
    num_samples = int(sample_rate * duration_s)
    t = np.arange(num_samples, dtype=np.float32) / sample_rate
    mono = 0.5 * np.sin(2 * np.pi * freq * t).astype(np.float32)

    if channels == 1:
        return mono.reshape(-1, 1)
    else:
        return np.column_stack([mono] * channels)


# ── Test: Version ──

def test_version():
    from python import __version__
    print(f"Version: {__version__}")
    assert "opus_qext_api" in __version__


# ── Test: File encode/decode roundtrip ──

def test_file_roundtrip():
    wav_bytes = make_wav_bytes(sample_rate=48000, channels=2, duration_s=0.5)

    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as tmp_in:
        tmp_in.write(wav_bytes)
        in_path = tmp_in.name

    out_opus = in_path + ".opus"
    out_wav = in_path + ".decoded.wav"

    try:
        opus_codec.encode_file(in_path, out_opus, bitrate=128000)
        assert os.path.isfile(out_opus)
        assert os.path.getsize(out_opus) > 0
        print(f"  Encoded: {os.path.getsize(out_opus)} bytes")

        opus_codec.decode_file(out_opus, out_wav)
        assert os.path.isfile(out_wav)
        assert os.path.getsize(out_wav) > 0
        print(f"  Decoded: {os.path.getsize(out_wav)} bytes")
    finally:
        for p in [in_path, out_opus, out_wav]:
            if os.path.exists(p):
                os.unlink(p)

    print("  PASS: file roundtrip")


# ── Test: WAV bytes roundtrip ──

def test_wav_bytes_roundtrip():
    wav_bytes = make_wav_bytes(sample_rate=48000, channels=2, duration_s=0.5)

    opus_bytes = opus_codec.encode_wav(wav_bytes, bitrate=128000)
    assert len(opus_bytes) > 0
    print(f"  Encoded: {len(opus_bytes)} bytes")

    decoded_wav = opus_codec.decode_to_wav(opus_bytes)
    assert len(decoded_wav) > 44  # WAV header is 44 bytes
    assert decoded_wav[:4] == b"RIFF"
    assert decoded_wav[8:12] == b"WAVE"
    print(f"  Decoded WAV: {len(decoded_wav)} bytes")
    print("  PASS: WAV bytes roundtrip")


# ── Test: 44.1kHz auto-resample encoding ──

def test_44100_resample():
    wav_bytes = make_wav_bytes(sample_rate=44100, channels=2, duration_s=0.5)

    opus_bytes = opus_codec.encode_wav(wav_bytes, bitrate=128000, enable_qext=True)
    assert len(opus_bytes) > 0
    print(f"  44.1kHz encoded: {len(opus_bytes)} bytes (with QEXT)")
    print("  PASS: 44.1kHz auto-resample")


# ── Test: QEXT on/off ──

def test_qext_toggle():
    wav_bytes = make_wav_bytes(sample_rate=48000, channels=2, duration_s=0.5)

    opus_qext = opus_codec.encode_wav(wav_bytes, bitrate=256000, enable_qext=True)
    opus_no_qext = opus_codec.encode_wav(wav_bytes, bitrate=256000, enable_qext=False)

    assert len(opus_qext) > 0
    assert len(opus_no_qext) > 0
    print(f"  QEXT on: {len(opus_qext)} bytes")
    print(f"  QEXT off: {len(opus_no_qext)} bytes")
    print("  PASS: QEXT toggle")


# ── Test: NumPy roundtrip ──

def test_numpy_roundtrip():
    arr = make_pcm_float(sample_rate=48000, channels=2, duration_s=0.5)

    opus_bytes = opus_numpy.encode_ndarray(arr, 48000, channels=2, bitrate=128000)
    assert len(opus_bytes) > 0
    print(f"  NumPy encoded: {len(opus_bytes)} bytes")

    decoded, sr, ch = opus_numpy.decode_to_ndarray(opus_bytes)
    assert sr == 48000
    assert ch == 2
    assert decoded.shape[1] == 2
    assert decoded.shape[0] > 0
    print(f"  NumPy decoded: {decoded.shape}, sr={sr}, ch={ch}")
    print("  PASS: NumPy roundtrip")


# ── Test: NumPy dtype variants ──

def test_numpy_dtypes():
    arr_f32 = make_pcm_float(sample_rate=48000, channels=1, duration_s=0.3)

    opus_bytes = opus_numpy.encode_ndarray(arr_f32, 48000, bitrate=64000)

    for dt in [np.float32, np.float64, np.int16]:
        decoded, sr, ch = opus_numpy.decode_to_ndarray(opus_bytes, dtype=dt)
        assert decoded.dtype == dt
        print(f"  dtype={dt.__name__}: shape={decoded.shape}")

    # int16 input encoding
    arr_i16 = (arr_f32 * 32767).astype(np.int16)
    opus_i16 = opus_numpy.encode_ndarray(arr_i16, 48000, bitrate=64000)
    assert len(opus_i16) > 0
    print("  PASS: NumPy dtype variants")


# ── Test: High bitrate ──

def test_high_bitrate():
    wav_bytes = make_wav_bytes(sample_rate=48000, channels=2, duration_s=0.5)

    for bps in [512000, 1024000, 2048000]:
        opus_bytes = opus_codec.encode_wav(wav_bytes, bitrate=bps, enable_qext=True)
        assert len(opus_bytes) > 0
        print(f"  {bps // 1000}kbps: {len(opus_bytes)} bytes")

    print("  PASS: High bitrate encoding")


# ── Test: PCM encode/decode ──

def test_pcm_roundtrip():
    arr = make_pcm_float(sample_rate=48000, channels=2, duration_s=0.5)
    pcm_bytes = arr.astype(np.float32).tobytes()

    opus_bytes = opus_codec.encode_pcm(pcm_bytes, 48000, 2, bitrate=128000)
    assert len(opus_bytes) > 0

    decoded_pcm, sr, ch = opus_codec.decode_to_pcm(opus_bytes)
    assert sr == 48000
    assert ch == 2
    assert len(decoded_pcm) > 0
    print(f"  PCM roundtrip: {len(pcm_bytes)} -> {len(opus_bytes)} -> {len(decoded_pcm)} bytes")
    print("  PASS: PCM roundtrip")


# ── Test: Torch (optional) ──

def test_torch():
    try:
        import torch
        from python.opus_torch import encode_tensor, decode_to_tensor
    except ImportError:
        print("  SKIP: torch not available")
        return

    t = torch.randn(2, 24000)  # 2ch, 0.5s at 48kHz
    t = t.clamp(-1.0, 1.0)

    opus_bytes = encode_tensor(t, 48000, bitrate=128000)
    assert len(opus_bytes) > 0

    decoded, sr, ch = decode_to_tensor(opus_bytes)
    assert sr == 48000
    assert ch == 2
    assert decoded.shape[0] == 2
    print(f"  Torch: {t.shape} -> {len(opus_bytes)} bytes -> {decoded.shape}")
    print("  PASS: Torch roundtrip")


# ── Run all tests ──

def run_all():
    tests = [
        ("Version", test_version),
        ("File roundtrip", test_file_roundtrip),
        ("WAV bytes roundtrip", test_wav_bytes_roundtrip),
        ("44.1kHz auto-resample", test_44100_resample),
        ("QEXT toggle", test_qext_toggle),
        ("NumPy roundtrip", test_numpy_roundtrip),
        ("NumPy dtype variants", test_numpy_dtypes),
        ("High bitrate (512k/1024k/2048k)", test_high_bitrate),
        ("PCM roundtrip", test_pcm_roundtrip),
        ("Torch (optional)", test_torch),
    ]

    passed = 0
    failed = 0
    skipped = 0

    for name, fn in tests:
        print(f"\n[TEST] {name}")
        try:
            fn()
            passed += 1
        except Exception as e:
            if "SKIP" in str(e):
                skipped += 1
            else:
                failed += 1
                print(f"  FAIL: {e}")
                import traceback
                traceback.print_exc()

    print(f"\n{'='*50}")
    print(f"Results: {passed} passed, {failed} failed, {skipped} skipped")
    print(f"{'='*50}")
    return failed == 0


if __name__ == "__main__":
    success = run_all()
    sys.exit(0 if success else 1)
