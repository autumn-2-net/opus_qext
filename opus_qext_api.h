/* opus_qext_api.h - High-level Opus encoding/decoding API with QEXT support
 *
 * Extracted from opusenc.c / opusdec.c frontend logic.
 * Both CLI tools and Python CFFI bindings use this shared library.
 *
 * Copyright (C) 2024 - Based on opus-tools by Xiph.Org Foundation
 */

#ifndef OPUS_QEXT_API_H
#define OPUS_QEXT_API_H

#include <stddef.h>
#include <opus_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* DLL export macro */
#ifndef OPUS_QEXT_EXPORT
# if defined(_WIN32) || defined(WIN32)
#  ifdef OPUS_QEXT_BUILD
#   define OPUS_QEXT_EXPORT __declspec(dllexport)
#  else
#   define OPUS_QEXT_EXPORT __declspec(dllimport)
#  endif
# elif defined(__GNUC__)
#  define OPUS_QEXT_EXPORT __attribute__((visibility("default")))
# else
#  define OPUS_QEXT_EXPORT
# endif
#endif

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

/* Signal types (matches opus defines) */
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
    opus_int32 bitrate;       /* -1 = auto, else bits per second */
    int        complexity;    /* 0-10, default 10 */
    int        enable_qext;   /* 0 or 1, default 1 */
    int        vbr_mode;      /* OPUS_QEXT_VBR / CVBR / HARD_CBR */
    int        signal_type;   /* OPUS_QEXT_SIGNAL_AUTO / MUSIC / VOICE */
    int        frame_duration;/* OPUS_QEXT_FRAME_*_MS, default 20ms */
    int        no_phase_inv;  /* 0 or 1, default 0 */
    int        downmix;       /* 0=auto, 1=mono, 2=stereo, -1=no */
    int        max_ogg_delay; /* in 48kHz samples, default 48000 */
} OpusQextEncParams;

/* Decoding parameters */
typedef struct {
    int   output_rate;    /* 0 = use original rate from opus header, else Hz */
    int   force_stereo;   /* 0 or 1 */
    int   no_dither;      /* 0 or 1 */
    int   fp;             /* 1 = float32 output, 0 = int16 output */
    float gain;           /* manual gain in dB, 0 = no change */
} OpusQextDecParams;

/* Encoding result (memory mode) */
typedef struct {
    unsigned char *data;           /* opus/ogg output bytes; free with opus_qext_free() */
    size_t         size;           /* size of data in bytes */
    opus_int32     input_rate;     /* detected input sample rate */
    int            channels;       /* number of channels */
    opus_int64     samples_encoded;/* total samples encoded (at 48kHz) */
} OpusQextEncResult;

/* Decoding result (memory mode) */
typedef struct {
    unsigned char *data;           /* output bytes (WAV or raw PCM); free with opus_qext_free() */
    size_t         size;           /* size of data in bytes */
    int            sample_rate;    /* output sample rate */
    int            channels;       /* number of channels */
    int            bits_per_sample;/* 16 (int16) or 32 (float32) */
    opus_int64     total_samples;  /* total samples per channel */
} OpusQextDecResult;

/* Initialize params to defaults */
OPUS_QEXT_EXPORT void opus_qext_enc_params_init(OpusQextEncParams *p);
OPUS_QEXT_EXPORT void opus_qext_dec_params_init(OpusQextDecParams *p);

/* === File-to-file === */

/** Encode audio file (WAV/AIFF/raw) to .opus file.
 *  Supports any sample rate (auto-resampled to 48kHz internally).
 *  Returns OPUS_QEXT_OK on success, negative error code on failure. */
OPUS_QEXT_EXPORT int opus_qext_encode_file(const char *in_path,
                                            const char *out_path,
                                            const OpusQextEncParams *params);

/** Decode .opus file to WAV file.
 *  Output sample rate defaults to original rate from opus header.
 *  Returns OPUS_QEXT_OK on success, negative error code on failure. */
OPUS_QEXT_EXPORT int opus_qext_decode_file(const char *in_path,
                                            const char *out_path,
                                            const OpusQextDecParams *params);

/* === Memory-to-memory (WAV bytes <-> opus bytes) === */

/** Encode WAV bytes to opus/ogg bytes in memory.
 *  wav_data: complete WAV file content (RIFF header + PCM data).
 *  result->data must be freed by caller via opus_qext_free(). */
OPUS_QEXT_EXPORT int opus_qext_encode_mem(const unsigned char *wav_data,
                                           size_t wav_len,
                                           OpusQextEncResult *result,
                                           const OpusQextEncParams *params);

/** Decode opus/ogg bytes to WAV bytes in memory.
 *  result->data contains complete WAV file; free via opus_qext_free(). */
OPUS_QEXT_EXPORT int opus_qext_decode_mem(const unsigned char *opus_data,
                                           size_t opus_len,
                                           OpusQextDecResult *result,
                                           const OpusQextDecParams *params);

/* === PCM-to-memory (raw float PCM <-> opus bytes) === */

/** Encode raw float PCM to opus/ogg bytes.
 *  pcm: interleaved float samples in [-1,+1] range.
 *  num_samples: total number of samples per channel.
 *  sample_rate: input sample rate (auto-resampled internally).
 *  result->data must be freed by caller via opus_qext_free(). */
OPUS_QEXT_EXPORT int opus_qext_encode_pcm(const float *pcm,
                                            size_t num_samples,
                                            int sample_rate,
                                            int channels,
                                            OpusQextEncResult *result,
                                            const OpusQextEncParams *params);

/** Decode opus/ogg bytes to raw float PCM.
 *  *pcm: allocated by the function, free via opus_qext_free().
 *  *num_samples: output total samples per channel.
 *  *sample_rate: output sample rate.
 *  *channels: output channel count. */
OPUS_QEXT_EXPORT int opus_qext_decode_pcm(const unsigned char *opus_data,
                                            size_t opus_len,
                                            float **pcm,
                                            size_t *num_samples,
                                            int *sample_rate,
                                            int *channels,
                                            const OpusQextDecParams *params);

/** Free memory allocated by opus_qext_* functions. */
OPUS_QEXT_EXPORT void opus_qext_free(void *ptr);

/** Get human-readable error string. */
OPUS_QEXT_EXPORT const char *opus_qext_error_string(int error);

/** Get version string. */
OPUS_QEXT_EXPORT const char *opus_qext_get_version(void);

#ifdef __cplusplus
}
#endif

#endif /* OPUS_QEXT_API_H */
