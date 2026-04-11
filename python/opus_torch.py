"""PyTorch tensor I/O for Opus encoding/decoding.

Tensor convention follows torchaudio: shape (channels, samples).
Internally converts to/from numpy for the actual encoding/decoding.

PyTorch is an optional dependency - import errors are deferred to call time.
"""

import numpy as np
from .opus_numpy import encode_ndarray, decode_to_ndarray


def _check_torch():
    try:
        import torch
        return torch
    except ImportError:
        raise ImportError(
            "PyTorch is required for opus_torch functions. "
            "Install it with: pip install torch"
        )


def encode_tensor(tensor, sample_rate, **kwargs):
    """Encode a PyTorch tensor to opus/ogg bytes.

    Args:
        tensor: torch.Tensor - shape (channels, samples) or (samples,) for mono.
                Float tensors should be in [-1, +1] range.
        sample_rate: Input sample rate in Hz.
        **kwargs: Encoding params (bitrate, enable_qext, complexity, etc.)

    Returns:
        bytes: Opus/Ogg encoded data.
    """
    torch = _check_torch()

    if not isinstance(tensor, torch.Tensor):
        raise TypeError(f"Expected torch.Tensor, got {type(tensor)}")

    # Convert to numpy
    t = tensor.detach().cpu()
    if t.ndim == 1:
        # Mono: (samples,) -> (samples, 1)
        arr = t.numpy().astype(np.float32)
        channels = 1
    elif t.ndim == 2:
        # (channels, samples) -> (samples, channels)
        arr = t.T.numpy().astype(np.float32)
        channels = t.shape[0]
    else:
        raise ValueError(f"Expected 1D or 2D tensor, got {t.ndim}D")

    return encode_ndarray(arr, sample_rate, channels=channels, **kwargs)


def decode_to_tensor(opus_data, dtype=None, **kwargs):
    """Decode opus/ogg bytes to a PyTorch tensor.

    Args:
        opus_data: bytes or str path - Opus/Ogg data or file path.
        dtype: Output torch dtype (default: torch.float32).
        **kwargs: Decoding params (output_rate, force_stereo, gain).

    Returns:
        tuple: (tensor, sample_rate, channels)
            tensor: torch.Tensor shape (channels, samples).
            sample_rate: Output sample rate in Hz.
            channels: Number of channels.
    """
    torch = _check_torch()

    if dtype is None:
        dtype = torch.float32

    # Map torch dtype to numpy dtype for decoding
    np_dtype = np.float32
    if dtype == torch.float64:
        np_dtype = np.float64
    elif dtype == torch.int16:
        np_dtype = np.int16

    arr, sr, ch = decode_to_ndarray(opus_data, dtype=np_dtype, **kwargs)

    # (samples, channels) -> (channels, samples)
    tensor = torch.from_numpy(arr.T.copy())

    if tensor.dtype != dtype:
        tensor = tensor.to(dtype)

    return tensor, sr, ch
