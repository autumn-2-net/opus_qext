"""NumPy ndarray I/O for Opus encoding/decoding.

Encoding: ndarray -> float32 PCM -> opus_qext_encode_pcm() -> opus bytes
Decoding: opus_qext_decode_pcm() -> ndarray

All resampling happens in the C layer (matching CLI behavior).
"""

import struct
import numpy as np

from . import _lib, ffi
from .opus_codec import (
    _make_enc_params, _make_dec_params, _check, OpusError,
)


def encode_ndarray(arr, sample_rate, channels=None, **kwargs):
    """Encode a NumPy array to opus/ogg bytes.

    Args:
        arr: np.ndarray - shape (samples,) for mono or (samples, channels).
             Supported dtypes: float32, float64, int16.
             Float values should be in [-1, +1] range.
             int16 values are auto-converted to float32.
        sample_rate: Input sample rate in Hz.
        channels: Number of channels (auto-detected from arr.shape if None).
        **kwargs: Encoding params (bitrate, enable_qext, complexity, etc.)

    Returns:
        bytes: Opus/Ogg encoded data.
    """
    arr = np.asarray(arr)

    # Determine channels
    if arr.ndim == 1:
        ch = channels or 1
        arr = arr.reshape(-1, ch) if ch == 1 else arr.reshape(-1, ch)
    elif arr.ndim == 2:
        ch = channels or arr.shape[1]
        if arr.shape[1] != ch:
            raise ValueError(f"Array has {arr.shape[1]} columns but channels={ch}")
    else:
        raise ValueError(f"Expected 1D or 2D array, got {arr.ndim}D")

    num_samples = arr.shape[0]

    # Convert to float32 interleaved
    if arr.dtype == np.int16:
        arr = arr.astype(np.float32) / 32768.0
    elif arr.dtype == np.float64:
        arr = arr.astype(np.float32)
    elif arr.dtype != np.float32:
        arr = arr.astype(np.float32)

    # Ensure C-contiguous
    arr = np.ascontiguousarray(arr)

    p = _make_enc_params(**kwargs)
    result = ffi.new("OpusQextEncResult *")
    buf = ffi.from_buffer("float[]", arr)

    _check(_lib.opus_qext_encode_pcm(buf, num_samples, sample_rate, ch, result, p))

    try:
        out = ffi.buffer(result.data, result.size)[:]
    finally:
        _lib.opus_qext_free(result.data)

    return bytes(out)


def decode_to_ndarray(opus_data, dtype=np.float32, **kwargs):
    """Decode opus/ogg bytes to a NumPy array.

    Args:
        opus_data: bytes or str path - Opus/Ogg data or file path.
        dtype: Output dtype - np.float32, np.float64, or np.int16.
        **kwargs: Decoding params (output_rate, force_stereo, gain).

    Returns:
        tuple: (arr, sample_rate, channels)
            arr: np.ndarray shape (samples, channels) with requested dtype.
            sample_rate: Output sample rate in Hz.
            channels: Number of channels.
    """
    import os
    if isinstance(opus_data, (str, os.PathLike)):
        with open(opus_data, "rb") as f:
            opus_data = f.read()

    p = _make_dec_params(**kwargs)
    pcm_ptr = ffi.new("float **")
    num_samples = ffi.new("size_t *")
    sr = ffi.new("int *")
    ch = ffi.new("int *")

    buf = ffi.from_buffer(opus_data)
    _check(_lib.opus_qext_decode_pcm(
        ffi.cast("const unsigned char *", buf),
        len(opus_data), pcm_ptr, num_samples, sr, ch, p))

    try:
        total_floats = num_samples[0] * ch[0]
        # Create numpy array from C buffer
        raw = np.frombuffer(ffi.buffer(pcm_ptr[0], total_floats * 4), dtype=np.float32).copy()
    finally:
        _lib.opus_qext_free(pcm_ptr[0])

    arr = raw.reshape(-1, ch[0])

    # Convert dtype
    if dtype == np.int16:
        arr = np.clip(arr * 32768.0, -32768, 32767).astype(np.int16)
    elif dtype == np.float64:
        arr = arr.astype(np.float64)
    elif dtype != np.float32:
        arr = arr.astype(dtype)

    return arr, sr[0], ch[0]
