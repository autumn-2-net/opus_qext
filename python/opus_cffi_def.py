"""CFFI definitions for opus_qext_api.dll

Declares all C structures and functions from opus_qext_api.h for ABI-mode loading.
"""

import os
import sys
import cffi

ffi = cffi.FFI()

# C declarations matching opus_qext_api.h
ffi.cdef("""
/* Error codes */
#define OPUS_QEXT_OK             0
#define OPUS_QEXT_ERR_ALLOC     -1
#define OPUS_QEXT_ERR_OPEN      -2
#define OPUS_QEXT_ERR_FORMAT    -3
#define OPUS_QEXT_ERR_ENCODE    -4
#define OPUS_QEXT_ERR_DECODE    -5
#define OPUS_QEXT_ERR_WRITE     -6
#define OPUS_QEXT_ERR_ARGS      -7
#define OPUS_QEXT_ERR_INTERNAL  -8

/* VBR modes */
#define OPUS_QEXT_VBR       0
#define OPUS_QEXT_CVBR      1
#define OPUS_QEXT_HARD_CBR  2

/* Signal types */
#define OPUS_QEXT_SIGNAL_AUTO   -1000
#define OPUS_QEXT_SIGNAL_MUSIC  3002
#define OPUS_QEXT_SIGNAL_VOICE  3001

/* Frame durations */
#define OPUS_QEXT_FRAME_2_5_MS   5001
#define OPUS_QEXT_FRAME_5_MS     5002
#define OPUS_QEXT_FRAME_10_MS    5003
#define OPUS_QEXT_FRAME_20_MS    5004
#define OPUS_QEXT_FRAME_40_MS    5005
#define OPUS_QEXT_FRAME_60_MS    5006

/* Encoding parameters */
typedef struct {
    int32_t bitrate;
    int     complexity;
    int     enable_qext;
    int     vbr_mode;
    int     signal_type;
    int     frame_duration;
    int     no_phase_inv;
    int     downmix;
    int     max_ogg_delay;
} OpusQextEncParams;

/* Decoding parameters */
typedef struct {
    int   output_rate;
    int   force_stereo;
    int   no_dither;
    int   fp;
    float gain;
} OpusQextDecParams;

/* Encoding result */
typedef struct {
    unsigned char *data;
    size_t         size;
    int32_t        input_rate;
    int            channels;
    int64_t        samples_encoded;
} OpusQextEncResult;

/* Decoding result */
typedef struct {
    unsigned char *data;
    size_t         size;
    int            sample_rate;
    int            channels;
    int            bits_per_sample;
    int64_t        total_samples;
} OpusQextDecResult;

void opus_qext_enc_params_init(OpusQextEncParams *p);
void opus_qext_dec_params_init(OpusQextDecParams *p);

int opus_qext_encode_file(const char *in_path, const char *out_path,
                          const OpusQextEncParams *params);
int opus_qext_decode_file(const char *in_path, const char *out_path,
                          const OpusQextDecParams *params);

int opus_qext_encode_mem(const unsigned char *wav_data, size_t wav_len,
                         OpusQextEncResult *result,
                         const OpusQextEncParams *params);
int opus_qext_decode_mem(const unsigned char *opus_data, size_t opus_len,
                         OpusQextDecResult *result,
                         const OpusQextDecParams *params);

int opus_qext_encode_pcm(const float *pcm, size_t num_samples,
                         int sample_rate, int channels,
                         OpusQextEncResult *result,
                         const OpusQextEncParams *params);

int opus_qext_decode_pcm(const unsigned char *opus_data, size_t opus_len,
                         float **pcm, size_t *num_samples,
                         int *sample_rate, int *channels,
                         const OpusQextDecParams *params);

void opus_qext_free(void *ptr);
const char *opus_qext_error_string(int error);
const char *opus_qext_get_version(void);
""")


def _find_dll():
    """Search for opus_qext_api DLL in common locations."""
    if sys.platform == "win32":
        dll_name = "opus_qext_api.dll"
    elif sys.platform == "darwin":
        dll_name = "libopus_qext_api.dylib"
    else:
        dll_name = "libopus_qext_api.so"

    # Search paths (ordered by priority)
    search_dirs = [
        os.path.dirname(os.path.abspath(__file__)),  # Same dir as this module
        os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."),  # Parent dir
        os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build_test", "Release"),
        os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "Release"),
    ]

    for d in search_dirs:
        path = os.path.join(d, dll_name)
        if os.path.isfile(path):
            return os.path.abspath(path)

    raise OSError(
        f"Cannot find {dll_name}. Searched in:\n" +
        "\n".join(f"  {d}" for d in search_dirs) +
        "\nBuild the project first with build.bat or cmake."
    )


def load_lib():
    """Load and return the opus_qext_api shared library."""
    dll_path = _find_dll()
    # On Windows, need to add DLL directory to search path
    if sys.platform == "win32":
        dll_dir = os.path.dirname(dll_path)
        if hasattr(os, "add_dll_directory"):
            os.add_dll_directory(dll_dir)
    return ffi.dlopen(dll_path)
