"""opus_qext - Python bindings for Opus encoder/decoder with QEXT support.

High-level API for encoding/decoding Opus audio with QEXT quality extensions.
Uses the same C code path as the opusenc/opusdec CLI tools.

Modules:
    opus_codec  - Core encode/decode functions (file, WAV bytes, PCM)
    opus_numpy  - NumPy ndarray I/O
    opus_torch  - PyTorch tensor I/O (optional)
"""

from ._version import __version__ as __version__
from .opus_cffi_def import ffi, load_lib

# Load DLL once at import time
_lib = load_lib()

from .opus_codec import (
    encode_file,
    decode_file,
    encode_wav,
    decode_to_wav,
    encode_pcm,
    decode_to_pcm,
)

__all__ = [
    "encode_file",
    "decode_file",
    "encode_wav",
    "decode_to_wav",
    "encode_pcm",
    "decode_to_pcm",
    "__version__",
]
