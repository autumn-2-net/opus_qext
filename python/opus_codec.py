"""Core encoding/decoding API wrapping opus_qext_api.dll via CFFI.

All functions use the same C code path as the opusenc/opusdec CLI tools,
including automatic resampling for non-native sample rates.
"""

import os
from . import _lib, ffi

# ── Constants ──

VBR = 0
CVBR = 1
HARD_CBR = 2

SIGNAL_AUTO = -1000
SIGNAL_MUSIC = 3002
SIGNAL_VOICE = 3001

FRAME_2_5_MS = 5001
FRAME_5_MS = 5002
FRAME_10_MS = 5003
FRAME_20_MS = 5004
FRAME_40_MS = 5005
FRAME_60_MS = 5006


class OpusError(Exception):
    """Raised when an opus_qext_api function returns an error."""
    def __init__(self, code):
        self.code = code
        msg = ffi.string(_lib.opus_qext_error_string(code)).decode("utf-8")
        super().__init__(f"opus_qext error {code}: {msg}")


def _check(ret):
    if ret != 0:
        raise OpusError(ret)


def _make_enc_params(bitrate=-1, enable_qext=True, complexity=10,
                     vbr_mode=VBR, signal_type=SIGNAL_AUTO,
                     frame_duration_ms=20, no_phase_inv=False,
                     downmix=0, max_ogg_delay=48000):
    """Create and populate OpusQextEncParams."""
    p = ffi.new("OpusQextEncParams *")
    _lib.opus_qext_enc_params_init(p)
    p.bitrate = bitrate
    p.enable_qext = 1 if enable_qext else 0
    p.complexity = complexity
    p.vbr_mode = vbr_mode
    p.signal_type = signal_type

    # Map ms to constant
    frame_map = {
        2.5: FRAME_2_5_MS, 5: FRAME_5_MS, 10: FRAME_10_MS,
        20: FRAME_20_MS, 40: FRAME_40_MS, 60: FRAME_60_MS,
    }
    p.frame_duration = frame_map.get(frame_duration_ms, FRAME_20_MS)
    p.no_phase_inv = 1 if no_phase_inv else 0
    p.downmix = downmix
    p.max_ogg_delay = max_ogg_delay
    return p


def _make_dec_params(output_rate=0, force_stereo=False, no_dither=False,
                     float_output=False, gain=0.0):
    """Create and populate OpusQextDecParams."""
    p = ffi.new("OpusQextDecParams *")
    _lib.opus_qext_dec_params_init(p)
    p.output_rate = output_rate
    p.force_stereo = 1 if force_stereo else 0
    p.no_dither = 1 if no_dither else 0
    p.fp = 1 if float_output else 0
    p.gain = gain
    return p


# ── File-to-file ──

def encode_file(in_path, out_path, **kwargs):
    """Encode an audio file (WAV/AIFF/raw) to .opus file.

    Supports any sample rate; auto-resampled to 48kHz internally.

    Args:
        in_path: Input audio file path.
        out_path: Output .opus file path.
        **kwargs: Encoding params (see _make_enc_params).
    """
    p = _make_enc_params(**kwargs)
    in_b = in_path.encode("utf-8") if isinstance(in_path, str) else in_path
    out_b = out_path.encode("utf-8") if isinstance(out_path, str) else out_path
    _check(_lib.opus_qext_encode_file(in_b, out_b, p))


def decode_file(in_path, out_path, **kwargs):
    """Decode .opus file to WAV file.

    Output sample rate defaults to original rate stored in opus header.

    Args:
        in_path: Input .opus file path.
        out_path: Output WAV file path.
        **kwargs: Decoding params (see _make_dec_params).
    """
    p = _make_dec_params(**kwargs)
    in_b = in_path.encode("utf-8") if isinstance(in_path, str) else in_path
    out_b = out_path.encode("utf-8") if isinstance(out_path, str) else out_path
    _check(_lib.opus_qext_decode_file(in_b, out_b, p))


# ── WAV bytes ↔ opus bytes ──

def encode_wav(wav_data, **kwargs):
    """Encode WAV bytes to opus/ogg bytes.

    Args:
        wav_data: bytes or path - Complete WAV file content or path to WAV file.
        **kwargs: Encoding params.

    Returns:
        bytes: Opus/Ogg encoded data.
    """
    if isinstance(wav_data, (str, os.PathLike)):
        with open(wav_data, "rb") as f:
            wav_data = f.read()

    p = _make_enc_params(**kwargs)
    result = ffi.new("OpusQextEncResult *")
    buf = ffi.from_buffer(wav_data)

    _check(_lib.opus_qext_encode_mem(ffi.cast("const unsigned char *", buf),
                                      len(wav_data), result, p))

    try:
        out = ffi.buffer(result.data, result.size)[:]
    finally:
        _lib.opus_qext_free(result.data)

    return bytes(out)


def decode_to_wav(opus_data, **kwargs):
    """Decode opus/ogg bytes to WAV bytes.

    Args:
        opus_data: bytes or path - Opus/Ogg file content or path to .opus file.
        **kwargs: Decoding params.

    Returns:
        bytes: Complete WAV file data.
    """
    if isinstance(opus_data, (str, os.PathLike)):
        with open(opus_data, "rb") as f:
            opus_data = f.read()

    p = _make_dec_params(**kwargs)
    result = ffi.new("OpusQextDecResult *")
    buf = ffi.from_buffer(opus_data)

    _check(_lib.opus_qext_decode_mem(ffi.cast("const unsigned char *", buf),
                                      len(opus_data), result, p))

    try:
        out = ffi.buffer(result.data, result.size)[:]
    finally:
        _lib.opus_qext_free(result.data)

    return bytes(out)


# ── Raw float PCM ↔ opus bytes ──

def encode_pcm(pcm_float, sample_rate, channels, **kwargs):
    """Encode raw float32 PCM to opus/ogg bytes.

    The C layer handles resampling from any sample_rate to 48kHz.

    Args:
        pcm_float: bytes-like - Interleaved float32 samples in [-1, +1].
        sample_rate: Input sample rate in Hz.
        channels: Number of channels.
        **kwargs: Encoding params.

    Returns:
        bytes: Opus/Ogg encoded data.
    """
    p = _make_enc_params(**kwargs)
    result = ffi.new("OpusQextEncResult *")

    buf = ffi.from_buffer(pcm_float)
    num_samples = len(pcm_float) // (4 * channels)  # float32 = 4 bytes

    _check(_lib.opus_qext_encode_pcm(
        ffi.cast("const float *", buf),
        num_samples, sample_rate, channels, result, p))

    try:
        out = ffi.buffer(result.data, result.size)[:]
    finally:
        _lib.opus_qext_free(result.data)

    return bytes(out)


def decode_to_pcm(opus_data, **kwargs):
    """Decode opus/ogg bytes to raw float32 PCM.

    Args:
        opus_data: bytes or path - Opus/Ogg data or file path.
        **kwargs: Decoding params (output_rate, force_stereo, gain).

    Returns:
        tuple: (pcm_bytes, sample_rate, channels)
            pcm_bytes: bytes of interleaved float32 samples.
            sample_rate: Output sample rate.
            channels: Number of output channels.
    """
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
        out = ffi.buffer(pcm_ptr[0], total_floats * 4)[:]
    finally:
        _lib.opus_qext_free(pcm_ptr[0])

    return bytes(out), sr[0], ch[0]
