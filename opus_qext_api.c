/* opus_qext_api.c - High-level Opus encoding/decoding API with QEXT support
 *
 * Core logic extracted from opusenc.c / opusdec.c frontend.
 * Provides file-to-file, memory-to-memory, and PCM-to-memory operations.
 *
 * Copyright (C) 2024 - Based on opus-tools by Xiph.Org Foundation
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#if (!defined WIN32 && !defined _WIN32) || defined(__MINGW32__)
# include <unistd.h>
#else
# include <process.h>
# define getpid _getpid
#endif

#if defined(WIN32) || defined(_WIN32)
# include <io.h>
# include <fcntl.h>
#endif

#include <opus.h>
#include <opus_multistream.h>
#include <opusenc.h>
#include <opusfile.h>

#include "opus_qext_api.h"
#include "opus-tools-0.2/src/encoder.h"
#include "opus-tools-0.2/src/diag_range.h"
#include "opus-tools-0.2/src/wav_io.h"
#include "opus-tools-0.2/src/speex_resampler.h"
#include "opus-tools-0.2/src/stack_alloc.h"

#define IMIN(a,b) ((a) < (b) ? (a) : (b))
#define IMAX(a,b) ((a) > (b) ? (a) : (b))

/* 120ms at 48000 */
#define MAX_FRAME_SIZE (960*6)

/* ========== Dynamic buffer for memory output ========== */

typedef struct {
    unsigned char *buf;
    size_t len;
    size_t cap;
} DynBuf;

static void dynbuf_init(DynBuf *d) {
    d->buf = NULL;
    d->len = 0;
    d->cap = 0;
}

static int dynbuf_write(DynBuf *d, const unsigned char *data, size_t len) {
    if (d->len + len > d->cap) {
        size_t newcap = d->cap ? d->cap * 2 : 65536;
        while (newcap < d->len + len) newcap *= 2;
        unsigned char *nb = (unsigned char*)realloc(d->buf, newcap);
        if (!nb) return -1;
        d->buf = nb;
        d->cap = newcap;
    }
    memcpy(d->buf + d->len, data, len);
    d->len += len;
    return 0;
}

static void dynbuf_free(DynBuf *d) {
    free(d->buf);
    d->buf = NULL;
    d->len = d->cap = 0;
}

/* ========== Encoder data (shared between file and memory modes) ========== */

typedef struct {
    OggOpusEnc *enc;
    /* File mode */
    FILE *fout;
    /* Memory mode */
    DynBuf *membuf;
    /* Stats */
    opus_int64 total_bytes;
    opus_int64 bytes_written;
    opus_int64 nb_encoded;
    opus_int64 pages_out;
    opus_int64 packets_out;
    opus_int32 peak_bytes;
    opus_int32 min_bytes;
    opus_int32 last_length;
    opus_int32 nb_streams;
    opus_int32 nb_coupled;
} EncData;

static int write_callback_file(void *user_data, const unsigned char *ptr, opus_int32 len) {
    EncData *data = (EncData*)user_data;
    data->bytes_written += len;
    data->pages_out++;
    return fwrite(ptr, 1, len, data->fout) != (size_t)len;
}

static int close_callback_file(void *user_data) {
    EncData *obj = (EncData*)user_data;
    if (obj->fout && obj->fout != stdout) return fclose(obj->fout) != 0;
    return 0;
}

static int write_callback_mem(void *user_data, const unsigned char *ptr, opus_int32 len) {
    EncData *data = (EncData*)user_data;
    data->bytes_written += len;
    data->pages_out++;
    return dynbuf_write(data->membuf, ptr, (size_t)len);
}

static int close_callback_mem(void *user_data) {
    (void)user_data;
    return 0;
}

static void packet_callback(void *user_data, const unsigned char *packet_ptr,
                             opus_int32 packet_len, opus_uint32 flags) {
    EncData *data = (EncData*)user_data;
    int nb_samples = opus_packet_get_nb_samples(packet_ptr, packet_len, 48000);
    if (nb_samples <= 0) return;
    data->total_bytes += packet_len;
    data->peak_bytes = IMAX(packet_len, data->peak_bytes);
    data->min_bytes = IMIN(packet_len, data->min_bytes);
    data->nb_encoded += nb_samples;
    data->packets_out++;
    data->last_length = packet_len;
    (void)flags;
}

static void encdata_init(EncData *data) {
    memset(data, 0, sizeof(*data));
    data->min_bytes = 256*1275*6;
    data->nb_streams = 1;
}

/* ========== Memory FILE adapter for WAV parsing (Windows-compatible) ========== */

typedef struct {
    const unsigned char *data;
    size_t len;
    size_t pos;
} MemReader;

#if defined(WIN32) || defined(_WIN32)
/* Windows: no fmemopen, use a temp file */
static FILE *mem_to_tmpfile(const unsigned char *data, size_t len) {
    FILE *f = tmpfile();
    if (!f) return NULL;
    if (fwrite(data, 1, len, f) != len) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    return f;
}
#else
/* POSIX: fmemopen */
static FILE *mem_to_tmpfile(const unsigned char *data, size_t len) {
    return fmemopen((void*)data, len, "rb");
}
#endif

/* ========== Core encode logic (extracted from opusenc.c main()) ========== */

static int encode_core(FILE *fin, const char *in_path,
                       const OpusEncCallbacks *callbacks, EncData *data,
                       const OpusQextEncParams *params,
                       opus_int32 *out_input_rate, int *out_channels,
                       opus_int64 *out_samples_encoded) {
    static const input_format raw_format = {NULL, 0, raw_open, wav_close, "raw", "RAW file reader"};
    int ret;
    oe_enc_opt inopt;
    const input_format *in_format;
    float *input;
    OggOpusEnc *enc;
    opus_int32 bitrate, rate, frame_size;
    int chan;
    opus_int32 opus_frame_param;
    opus_int32 lookahead = 0;
    int serialno;

    /* Defaults */
    bitrate = params->bitrate;
    frame_size = 960; /* 20ms at 48kHz */
    opus_frame_param = params->frame_duration;
    if (opus_frame_param == 0) opus_frame_param = OPUS_QEXT_FRAME_20_MS;

    /* Map our frame duration constants to opus */
    switch (opus_frame_param) {
        case OPUS_QEXT_FRAME_2_5_MS: opus_frame_param = OPUS_FRAMESIZE_2_5_MS; break;
        case OPUS_QEXT_FRAME_5_MS:   opus_frame_param = OPUS_FRAMESIZE_5_MS; break;
        case OPUS_QEXT_FRAME_10_MS:  opus_frame_param = OPUS_FRAMESIZE_10_MS; break;
        case OPUS_QEXT_FRAME_20_MS:  opus_frame_param = OPUS_FRAMESIZE_20_MS; break;
        case OPUS_QEXT_FRAME_40_MS:  opus_frame_param = OPUS_FRAMESIZE_40_MS; break;
        case OPUS_QEXT_FRAME_60_MS:  opus_frame_param = OPUS_FRAMESIZE_60_MS; break;
        default: opus_frame_param = OPUS_FRAMESIZE_20_MS; break;
    }
    frame_size = opus_frame_param <= OPUS_FRAMESIZE_40_MS
        ? 120 << (opus_frame_param - OPUS_FRAMESIZE_2_5_MS)
        : (opus_frame_param - OPUS_FRAMESIZE_20_MS + 1) * 960;

    /* Init inopt */
    memset(&inopt, 0, sizeof(inopt));
    inopt.channels = 2;
    inopt.rate = 48000;
    inopt.gain = 0;
    inopt.samplesize = 16;
    inopt.endianness = 0;
    inopt.rawmode = 0;
    inopt.ignorelength = 0;
    inopt.copy_comments = 1;
    inopt.copy_pictures = 1;

    srand((((unsigned)getpid()&65535)<<15)^(unsigned)time(NULL));
    serialno = rand();

    inopt.comments = ope_comments_create();
    if (!inopt.comments) return OPUS_QEXT_ERR_ALLOC;

    ret = ope_comments_add(inopt.comments, "ENCODER", "opus_qext_api");
    if (ret != OPE_OK) {
        ope_comments_destroy(inopt.comments);
        return OPUS_QEXT_ERR_INTERNAL;
    }

    /* Open input */
    if (inopt.rawmode) {
        in_format = &raw_format;
        in_format->open_func(fin, &inopt, NULL, 0);
    } else {
        in_format = open_audio_file(fin, &inopt);
    }
    if (!in_format) {
        ope_comments_destroy(inopt.comments);
        return OPUS_QEXT_ERR_FORMAT;
    }

    if (inopt.rate < 100 || inopt.rate > 768000) {
        in_format->close_func(inopt.readdata);
        ope_comments_destroy(inopt.comments);
        return OPUS_QEXT_ERR_FORMAT;
    }
    if (inopt.channels > 255 || inopt.channels < 1) {
        in_format->close_func(inopt.readdata);
        ope_comments_destroy(inopt.comments);
        return OPUS_QEXT_ERR_FORMAT;
    }

    /* Apply downmix */
    rate = inopt.rate;
    chan = inopt.channels;
    if (params->downmix > 0 && params->downmix < chan) {
        setup_downmix(&inopt, params->downmix);
        chan = params->downmix;
    } else if (params->downmix == 0 && chan > 2 && bitrate > 0 && bitrate < 16000 * chan) {
        int dm = chan > 8 ? 1 : 2;
        setup_downmix(&inopt, dm);
        chan = dm;
    }

    if (inopt.total_samples_per_channel && rate != 48000)
        inopt.total_samples_per_channel = (opus_int64)
            ((double)inopt.total_samples_per_channel * (48000.0 / (double)rate));

    /* Create encoder */
    enc = ope_encoder_create_callbacks(callbacks, data, inopt.comments, rate,
        chan, chan > 8 ? 255 : chan > 2, &ret);
    if (!enc) {
        in_format->close_func(inopt.readdata);
        ope_comments_destroy(inopt.comments);
        return OPUS_QEXT_ERR_ENCODE;
    }
    data->enc = enc;

    /* Set CTLs */
    ope_encoder_ctl(enc, OPUS_SET_EXPERT_FRAME_DURATION(opus_frame_param));
    ope_encoder_ctl(enc, OPE_SET_MUXING_DELAY(params->max_ogg_delay > 0 ? params->max_ogg_delay : 48000));
    ope_encoder_ctl(enc, OPE_SET_SERIALNO(serialno));
    ope_encoder_ctl(enc, OPE_SET_HEADER_GAIN(inopt.gain));
    ope_encoder_ctl(enc, OPE_SET_PACKET_CALLBACK(packet_callback, data));
    ope_encoder_ctl(enc, OPE_SET_COMMENT_PADDING(512));

    ope_encoder_ctl(enc, OPE_GET_NB_STREAMS(&data->nb_streams));
    ope_encoder_ctl(enc, OPE_GET_NB_COUPLED_STREAMS(&data->nb_coupled));

    /* QEXT + FORCE_MODE — must be set BEFORE bitrate so the raised cap takes effect */
#ifdef ENABLE_QEXT
    if (params->enable_qext) {
        ope_encoder_ctl(enc, OPUS_SET_QEXT(1));
        /* Force CELT-only mode to guarantee QEXT path activation */
        ope_encoder_ctl(enc, 11002 /* OPUS_SET_FORCE_MODE_REQUEST */, 1002 /* MODE_CELT_ONLY */);
    }
#endif

    /* Bitrate */
    if (bitrate < 0) {
        bitrate = ((64000 * data->nb_streams + 32000 * data->nb_coupled) *
                   (IMIN(48, IMAX(8, ((rate < 44100 ? rate : 48000) + 1000) / 1000)) + 16) + 32) >> 6;
    }
    if (bitrate > 2048000 * chan || bitrate < 500) {
        ope_encoder_destroy(enc);
        in_format->close_func(inopt.readdata);
        ope_comments_destroy(inopt.comments);
        return OPUS_QEXT_ERR_ARGS;
    }
    bitrate = IMIN(chan * 2048000, bitrate);

    ope_encoder_ctl(enc, OPUS_SET_BITRATE(bitrate));

    /* VBR */
    switch (params->vbr_mode) {
        case OPUS_QEXT_HARD_CBR:
            ope_encoder_ctl(enc, OPUS_SET_VBR(0));
            break;
        case OPUS_QEXT_CVBR:
            ope_encoder_ctl(enc, OPUS_SET_VBR(1));
            ope_encoder_ctl(enc, OPUS_SET_VBR_CONSTRAINT(1));
            break;
        default: /* VBR */
            ope_encoder_ctl(enc, OPUS_SET_VBR(1));
            ope_encoder_ctl(enc, OPUS_SET_VBR_CONSTRAINT(0));
            break;
    }

    /* Signal type */
    if (params->signal_type == OPUS_QEXT_SIGNAL_MUSIC)
        ope_encoder_ctl(enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));
    else if (params->signal_type == OPUS_QEXT_SIGNAL_VOICE)
        ope_encoder_ctl(enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    else
        ope_encoder_ctl(enc, OPUS_SET_SIGNAL(OPUS_AUTO));

    ope_encoder_ctl(enc, OPUS_SET_COMPLEXITY(params->complexity >= 0 ? params->complexity : 10));
    ope_encoder_ctl(enc, OPUS_SET_PACKET_LOSS_PERC(0));

#ifdef OPUS_SET_LSB_DEPTH
    ope_encoder_ctl(enc, OPUS_SET_LSB_DEPTH(IMAX(8, IMIN(24, inopt.samplesize))));
#endif

    if (params->no_phase_inv) {
#ifdef OPUS_SET_PHASE_INVERSION_DISABLED_REQUEST
        ope_encoder_ctl(enc, OPUS_SET_PHASE_INVERSION_DISABLED(1));
#endif
    }

    ope_encoder_ctl(enc, OPUS_GET_LOOKAHEAD(&lookahead));

    /* Allocate input buffer */
    input = (float*)malloc(sizeof(float) * frame_size * chan);
    if (!input) {
        ope_encoder_destroy(enc);
        in_format->close_func(inopt.readdata);
        ope_comments_destroy(inopt.comments);
        return OPUS_QEXT_ERR_ALLOC;
    }

    /* Main encoding loop */
    while (1) {
        opus_int32 nb_samples = inopt.read_samples(inopt.readdata, input, frame_size);
        ret = ope_encoder_write_float(enc, input, nb_samples);
        if (ret != OPE_OK || nb_samples < frame_size) break;
    }

    ret = ope_encoder_drain(enc);

    /* Output results */
    if (out_input_rate) *out_input_rate = rate;
    if (out_channels) *out_channels = chan;
    if (out_samples_encoded) *out_samples_encoded = data->nb_encoded;

    ope_encoder_destroy(enc);
    ope_comments_destroy(inopt.comments);
    free(input);
    if (params->downmix > 0) clear_downmix(&inopt);
    in_format->close_func(inopt.readdata);

    return (ret == OPE_OK) ? OPUS_QEXT_OK : OPUS_QEXT_ERR_ENCODE;
}

/* ========== Decoder helpers (extracted from opusdec.c) ========== */

#ifndef HAVE_LRINTF
# define float2int(x) ((int)(floor(.5+(x))))
#else
# define float2int(x) lrintf(x)
#endif

#ifndef HAVE_FMINF
# define fminf(_x,_y) ((_x)<(_y)?(_x):(_y))
#endif
#ifndef HAVE_FMAXF
# define fmaxf(_x,_y) ((_x)>(_y)?(_x):(_y))
#endif

static opus_int64 decode_audio_write(float *pcm, int channels, int frame_size,
    DynBuf *out, SpeexResamplerState *resampler, int rate,
    opus_int64 link_read, opus_int64 link_out, int fp) {
    opus_int64 sampout = 0;
    opus_int64 maxout;
    unsigned out_len;
    short *shortbuf;
    float *buf;
    float *output;

    shortbuf = (short*)alloca(sizeof(short) * MAX_FRAME_SIZE * channels);
    buf = (float*)alloca(sizeof(float) * MAX_FRAME_SIZE * channels);
    maxout = ((link_read / 48000) * rate + (link_read % 48000) * rate / 48000) - link_out;
    if (maxout < 0) maxout = 0;

    do {
        if (resampler) {
            unsigned in_len;
            output = buf;
            in_len = frame_size;
            out_len = 1024 < maxout ? 1024 : (unsigned)maxout;
            speex_resampler_process_interleaved_float(resampler,
                pcm, &in_len, buf, &out_len);
            pcm += channels * in_len;
            frame_size -= in_len;
        } else {
            output = pcm;
            out_len = frame_size < maxout ? (unsigned)frame_size : (unsigned)maxout;
            frame_size = 0;
        }

        if (!fp) {
            int i;
            for (i = 0; i < (int)out_len * channels; i++)
                shortbuf[i] = (short)float2int(fmaxf(-32768, fminf(output[i] * 32768.f, 32767)));
        }

        if (maxout > 0) {
            if (fp) {
                dynbuf_write(out, (const unsigned char*)output,
                    sizeof(float) * channels * out_len);
            } else {
                dynbuf_write(out, (const unsigned char*)shortbuf,
                    sizeof(short) * channels * out_len);
            }
            sampout += out_len;
            maxout -= out_len;
        }
    } while (frame_size > 0 && maxout > 0);
    return sampout;
}

static void decode_drain_resampler(DynBuf *out, SpeexResamplerState *resampler,
    int channels, int rate, opus_int64 link_read, opus_int64 link_out,
    opus_int64 *audio_size, int fp) {
    float *zeros;
    int drain;
    zeros = (float*)calloc(100 * channels, sizeof(float));
    drain = speex_resampler_get_input_latency(resampler);
    do {
        opus_int64 outsamp;
        int tmp = IMIN(drain, 100);
        outsamp = decode_audio_write(zeros, channels, tmp, out, resampler,
            rate, link_read, link_out, fp);
        link_out += outsamp;
        (*audio_size) += (fp ? sizeof(float) : sizeof(short)) * outsamp * channels;
        drain -= tmp;
    } while (drain > 0);
    free(zeros);
}

/* Core decode logic */
static int decode_core(OggOpusFile *st, DynBuf *pcm_out,
                       const OpusQextDecParams *params,
                       int *out_rate, int *out_channels,
                       opus_int64 *out_total_samples, int wav_mode) {
    const OpusHead *head;
    float *output;
    int rate, channels, fp, li, old_li = -1;
    int force_stereo;
    opus_int64 link_read = 0, link_out = 0;
    opus_int64 audio_size = 0;
    SpeexResamplerState *resampler = NULL;
    int wav_header_size = 0;

    fp = params->fp;
    force_stereo = params->force_stereo;

    if (params->gain != 0.f) {
        op_set_gain_offset(st, OP_HEADER_GAIN, float2int(params->gain * 256.f));
    }

    head = op_head(st, 0);

    /* Determine output rate.
       Default to the original input sample rate stored in the Opus header.
       The decode-side resampler uses Q10 + cutoff_override=1.0 which
       preserves content all the way to the output Nyquist frequency.
       If the header has no rate (0), fall back to 48kHz. */
    rate = params->output_rate;
    if (rate == 0) {
        rate = head->input_sample_rate;
        if (rate == 0) rate = 48000;
    }
    if (rate < 8000 || rate > 192000) rate = 48000;

    channels = force_stereo ? 2 : head->channel_count;

    /* Write WAV header placeholder (will be patched at end) */
    if (wav_mode) {
        /* Reserve space for WAV header - we'll fill it at end */
        unsigned char wav_hdr[44];
        int bps = fp ? 32 : 16;
        int byterate = rate * channels * (bps / 8);
        int blockalign = channels * (bps / 8);

        memcpy(wav_hdr, "RIFF", 4);
        memset(wav_hdr + 4, 0, 4); /* file size - 8, filled later */
        memcpy(wav_hdr + 8, "WAVE", 4);
        memcpy(wav_hdr + 12, "fmt ", 4);
        /* fmt chunk size */
        wav_hdr[16] = 16; wav_hdr[17] = 0; wav_hdr[18] = 0; wav_hdr[19] = 0;
        /* format: 1=PCM, 3=IEEE float */
        wav_hdr[20] = fp ? 3 : 1; wav_hdr[21] = 0;
        wav_hdr[22] = channels & 0xFF; wav_hdr[23] = 0;
        wav_hdr[24] = rate & 0xFF; wav_hdr[25] = (rate >> 8) & 0xFF;
        wav_hdr[26] = (rate >> 16) & 0xFF; wav_hdr[27] = (rate >> 24) & 0xFF;
        wav_hdr[28] = byterate & 0xFF; wav_hdr[29] = (byterate >> 8) & 0xFF;
        wav_hdr[30] = (byterate >> 16) & 0xFF; wav_hdr[31] = (byterate >> 24) & 0xFF;
        wav_hdr[32] = blockalign & 0xFF; wav_hdr[33] = 0;
        wav_hdr[34] = bps & 0xFF; wav_hdr[35] = 0;
        memcpy(wav_hdr + 36, "data", 4);
        memset(wav_hdr + 40, 0, 4); /* data size, filled later */

        wav_header_size = 44;
        dynbuf_write(pcm_out, wav_hdr, 44);
    }

    output = (float*)malloc(sizeof(float) * MAX_FRAME_SIZE * channels);
    if (!output) return OPUS_QEXT_ERR_ALLOC;

    /* Main decoding loop */
    while (1) {
        int nb_read;
        opus_int64 outsamp;

        if (force_stereo) {
            nb_read = op_read_float_stereo(st, output, MAX_FRAME_SIZE * channels);
            li = op_current_link(st);
        } else {
            nb_read = op_read_float(st, output, MAX_FRAME_SIZE * channels, &li);
        }

        if (nb_read < 0) {
            if (nb_read == OP_HOLE) continue;
            else break;
        }
        if (nb_read == 0) break;

        if (li != old_li) {
            if (resampler) {
                decode_drain_resampler(pcm_out, resampler, channels, rate,
                    link_read, link_out, &audio_size, fp);
                speex_resampler_destroy(resampler);
                resampler = NULL;
            }
            link_read = link_out = 0;
            head = op_head(st, li);
        }

        link_read += nb_read;

        if (rate != 48000 && !resampler) {
            int err;
            resampler = speex_resampler_init(channels, 48000, rate, 10, &err);
            if (err != 0) {
                free(output);
                return OPUS_QEXT_ERR_INTERNAL;
            }
            speex_resampler_skip_zeros(resampler);
            /* Override cutoff to 1.0 to preserve QEXT HF content
               through downsampling (same fix as encode side) */
            speex_resampler_set_cutoff(resampler, 1.0f);
        }

        outsamp = decode_audio_write(output, channels, nb_read, pcm_out,
            resampler, rate, link_read, link_out, fp);
        link_out += outsamp;
        audio_size += (fp ? sizeof(float) : sizeof(short)) * outsamp * channels;
        old_li = li;
    }

    if (resampler) {
        decode_drain_resampler(pcm_out, resampler, channels, rate,
            link_read, link_out, &audio_size, fp);
        speex_resampler_destroy(resampler);
    }

    /* Patch WAV header with actual sizes */
    if (wav_mode && pcm_out->buf && audio_size < 0x7FFFFFFF) {
        opus_int32 data_size = (opus_int32)audio_size;
        opus_int32 riff_size = data_size + 36;
        pcm_out->buf[4]  = riff_size & 0xFF;
        pcm_out->buf[5]  = (riff_size >> 8) & 0xFF;
        pcm_out->buf[6]  = (riff_size >> 16) & 0xFF;
        pcm_out->buf[7]  = (riff_size >> 24) & 0xFF;
        pcm_out->buf[40] = data_size & 0xFF;
        pcm_out->buf[41] = (data_size >> 8) & 0xFF;
        pcm_out->buf[42] = (data_size >> 16) & 0xFF;
        pcm_out->buf[43] = (data_size >> 24) & 0xFF;
    }

    if (out_rate) *out_rate = rate;
    if (out_channels) *out_channels = channels;
    if (out_total_samples) {
        int sample_size = fp ? sizeof(float) : sizeof(short);
        *out_total_samples = audio_size / (sample_size * channels);
    }

    free(output);
    return OPUS_QEXT_OK;
}

/* ========== Public API implementation ========== */

OPUS_QEXT_EXPORT void opus_qext_enc_params_init(OpusQextEncParams *p) {
    memset(p, 0, sizeof(*p));
    p->bitrate = -1; /* auto */
    p->complexity = 10;
    p->enable_qext = 1;
    p->vbr_mode = OPUS_QEXT_VBR;
    p->signal_type = OPUS_QEXT_SIGNAL_AUTO;
    p->frame_duration = OPUS_QEXT_FRAME_20_MS;
    p->no_phase_inv = 0;
    p->downmix = 0;
    p->max_ogg_delay = 48000;
}

OPUS_QEXT_EXPORT void opus_qext_dec_params_init(OpusQextDecParams *p) {
    memset(p, 0, sizeof(*p));
    p->output_rate = 0; /* use original */
    p->force_stereo = 0;
    p->no_dither = 0;
    p->fp = 0; /* int16 */
    p->gain = 0.f;
}

OPUS_QEXT_EXPORT int opus_qext_encode_file(const char *in_path,
                                            const char *out_path,
                                            const OpusQextEncParams *params) {
    OpusQextEncParams p;
    OpusEncCallbacks callbacks = {write_callback_file, close_callback_file};
    EncData data;
    FILE *fin;
    int ret;

    if (!in_path || !out_path) return OPUS_QEXT_ERR_ARGS;

    if (!params) { opus_qext_enc_params_init(&p); params = &p; }

    encdata_init(&data);

    /* Open input */
    fin = fopen(in_path, "rb");
    if (!fin) return OPUS_QEXT_ERR_OPEN;

    /* Open output */
    data.fout = fopen(out_path, "wb");
    if (!data.fout) { fclose(fin); return OPUS_QEXT_ERR_OPEN; }

    ret = encode_core(fin, in_path, &callbacks, &data, params, NULL, NULL, NULL);

    fclose(fin);
    /* data.fout is closed by close_callback_file via ope_encoder_destroy */
    return ret;
}

OPUS_QEXT_EXPORT int opus_qext_decode_file(const char *in_path,
                                            const char *out_path,
                                            const OpusQextDecParams *params) {
    OpusQextDecParams p;
    OggOpusFile *st;
    DynBuf pcm_out;
    int ret, rate, channels;
    opus_int64 total_samples;
    FILE *fout;

    if (!in_path || !out_path) return OPUS_QEXT_ERR_ARGS;
    if (!params) { opus_qext_dec_params_init(&p); params = &p; }

    st = op_open_file(in_path, &ret);
    if (!st) return OPUS_QEXT_ERR_OPEN;

    dynbuf_init(&pcm_out);
    ret = decode_core(st, &pcm_out, params, &rate, &channels, &total_samples, 1);
    op_free(st);

    if (ret != OPUS_QEXT_OK) { dynbuf_free(&pcm_out); return ret; }

    /* Write to file */
    fout = fopen(out_path, "wb");
    if (!fout) { dynbuf_free(&pcm_out); return OPUS_QEXT_ERR_OPEN; }
    if (fwrite(pcm_out.buf, 1, pcm_out.len, fout) != pcm_out.len) {
        fclose(fout);
        dynbuf_free(&pcm_out);
        return OPUS_QEXT_ERR_WRITE;
    }
    fclose(fout);
    dynbuf_free(&pcm_out);
    return OPUS_QEXT_OK;
}

OPUS_QEXT_EXPORT int opus_qext_encode_mem(const unsigned char *wav_data,
                                           size_t wav_len,
                                           OpusQextEncResult *result,
                                           const OpusQextEncParams *params) {
    OpusQextEncParams p;
    OpusEncCallbacks callbacks = {write_callback_mem, close_callback_mem};
    EncData data;
    DynBuf membuf;
    FILE *fin;
    int ret;
    opus_int32 input_rate;
    int channels;
    opus_int64 samples_encoded;

    if (!wav_data || !result) return OPUS_QEXT_ERR_ARGS;
    if (!params) { opus_qext_enc_params_init(&p); params = &p; }

    memset(result, 0, sizeof(*result));
    encdata_init(&data);
    dynbuf_init(&membuf);
    data.membuf = &membuf;

    /* Create FILE* from memory */
    fin = mem_to_tmpfile(wav_data, wav_len);
    if (!fin) return OPUS_QEXT_ERR_ALLOC;

    ret = encode_core(fin, NULL, &callbacks, &data, params,
                      &input_rate, &channels, &samples_encoded);
    fclose(fin);

    if (ret == OPUS_QEXT_OK) {
        result->data = membuf.buf;
        result->size = membuf.len;
        result->input_rate = input_rate;
        result->channels = channels;
        result->samples_encoded = samples_encoded;
        /* Don't free membuf - ownership transferred to result */
    } else {
        dynbuf_free(&membuf);
    }
    return ret;
}

OPUS_QEXT_EXPORT int opus_qext_decode_mem(const unsigned char *opus_data,
                                           size_t opus_len,
                                           OpusQextDecResult *result,
                                           const OpusQextDecParams *params) {
    OpusQextDecParams p;
    OggOpusFile *st;
    DynBuf pcm_out;
    int ret, rate, channels;
    opus_int64 total_samples;

    if (!opus_data || !result) return OPUS_QEXT_ERR_ARGS;
    if (!params) { opus_qext_dec_params_init(&p); params = &p; }

    memset(result, 0, sizeof(*result));

    st = op_open_memory(opus_data, opus_len, &ret);
    if (!st) return OPUS_QEXT_ERR_OPEN;

    dynbuf_init(&pcm_out);
    ret = decode_core(st, &pcm_out, params, &rate, &channels, &total_samples, 1);
    op_free(st);

    if (ret == OPUS_QEXT_OK) {
        result->data = pcm_out.buf;
        result->size = pcm_out.len;
        result->sample_rate = rate;
        result->channels = channels;
        result->bits_per_sample = params->fp ? 32 : 16;
        result->total_samples = total_samples;
    } else {
        dynbuf_free(&pcm_out);
    }
    return ret;
}

OPUS_QEXT_EXPORT int opus_qext_encode_pcm(const float *pcm,
                                            size_t num_samples,
                                            int sample_rate,
                                            int channels,
                                            OpusQextEncResult *result,
                                            const OpusQextEncParams *params) {
    OpusQextEncParams p;
    OpusEncCallbacks callbacks = {write_callback_mem, close_callback_mem};
    EncData data;
    DynBuf membuf;
    OggOpusEnc *enc;
    int ret;
    opus_int32 bitrate;
    opus_int32 opus_frame_param;

    if (!pcm || !result || channels < 1 || channels > 255 ||
        sample_rate < 100 || sample_rate > 768000 || num_samples == 0)
        return OPUS_QEXT_ERR_ARGS;

    if (!params) { opus_qext_enc_params_init(&p); params = &p; }

    memset(result, 0, sizeof(*result));
    encdata_init(&data);
    dynbuf_init(&membuf);
    data.membuf = &membuf;

    OggOpusComments *comments = ope_comments_create();
    if (!comments) return OPUS_QEXT_ERR_ALLOC;
    ope_comments_add(comments, "ENCODER", "opus_qext_api");

    srand((((unsigned)getpid()&65535)<<15)^(unsigned)time(NULL));

    enc = ope_encoder_create_callbacks(&callbacks, &data, comments, sample_rate,
        channels, channels > 8 ? 255 : channels > 2, &ret);
    if (!enc) {
        ope_comments_destroy(comments);
        return OPUS_QEXT_ERR_ENCODE;
    }
    data.enc = enc;

    /* Frame duration */
    opus_frame_param = params->frame_duration;
    if (opus_frame_param == 0) opus_frame_param = OPUS_QEXT_FRAME_20_MS;
    switch (opus_frame_param) {
        case OPUS_QEXT_FRAME_2_5_MS: opus_frame_param = OPUS_FRAMESIZE_2_5_MS; break;
        case OPUS_QEXT_FRAME_5_MS:   opus_frame_param = OPUS_FRAMESIZE_5_MS; break;
        case OPUS_QEXT_FRAME_10_MS:  opus_frame_param = OPUS_FRAMESIZE_10_MS; break;
        case OPUS_QEXT_FRAME_20_MS:  opus_frame_param = OPUS_FRAMESIZE_20_MS; break;
        case OPUS_QEXT_FRAME_40_MS:  opus_frame_param = OPUS_FRAMESIZE_40_MS; break;
        case OPUS_QEXT_FRAME_60_MS:  opus_frame_param = OPUS_FRAMESIZE_60_MS; break;
        default: opus_frame_param = OPUS_FRAMESIZE_20_MS; break;
    }

    ope_encoder_ctl(enc, OPUS_SET_EXPERT_FRAME_DURATION(opus_frame_param));
    ope_encoder_ctl(enc, OPE_SET_MUXING_DELAY(params->max_ogg_delay > 0 ? params->max_ogg_delay : 48000));
    ope_encoder_ctl(enc, OPE_SET_SERIALNO(rand()));
    ope_encoder_ctl(enc, OPE_SET_PACKET_CALLBACK(packet_callback, &data));
    ope_encoder_ctl(enc, OPE_SET_COMMENT_PADDING(512));

    ope_encoder_ctl(enc, OPE_GET_NB_STREAMS(&data.nb_streams));
    ope_encoder_ctl(enc, OPE_GET_NB_COUPLED_STREAMS(&data.nb_coupled));

    /* QEXT + FORCE_MODE — must be set BEFORE bitrate so the raised cap takes effect */
#ifdef ENABLE_QEXT
    if (params->enable_qext) {
        ope_encoder_ctl(enc, OPUS_SET_QEXT(1));
        ope_encoder_ctl(enc, 11002 /* OPUS_SET_FORCE_MODE_REQUEST */, 1002 /* MODE_CELT_ONLY */);
    }
#endif

    bitrate = params->bitrate;
    if (bitrate < 0) {
        bitrate = ((64000 * data.nb_streams + 32000 * data.nb_coupled) *
                   (IMIN(48, IMAX(8, ((sample_rate < 44100 ? sample_rate : 48000) + 1000) / 1000)) + 16) + 32) >> 6;
    }
    bitrate = IMIN(channels * 2048000, IMAX(500, bitrate));

    ope_encoder_ctl(enc, OPUS_SET_BITRATE(bitrate));

    switch (params->vbr_mode) {
        case OPUS_QEXT_HARD_CBR:
            ope_encoder_ctl(enc, OPUS_SET_VBR(0));
            break;
        case OPUS_QEXT_CVBR:
            ope_encoder_ctl(enc, OPUS_SET_VBR(1));
            ope_encoder_ctl(enc, OPUS_SET_VBR_CONSTRAINT(1));
            break;
        default:
            ope_encoder_ctl(enc, OPUS_SET_VBR(1));
            ope_encoder_ctl(enc, OPUS_SET_VBR_CONSTRAINT(0));
            break;
    }

    if (params->signal_type == OPUS_QEXT_SIGNAL_MUSIC)
        ope_encoder_ctl(enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));
    else if (params->signal_type == OPUS_QEXT_SIGNAL_VOICE)
        ope_encoder_ctl(enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));

    ope_encoder_ctl(enc, OPUS_SET_COMPLEXITY(params->complexity >= 0 ? params->complexity : 10));

    if (params->no_phase_inv) {
#ifdef OPUS_SET_PHASE_INVERSION_DISABLED_REQUEST
        ope_encoder_ctl(enc, OPUS_SET_PHASE_INVERSION_DISABLED(1));
#endif
    }

    /* Feed PCM */
    ret = ope_encoder_write_float(enc, pcm, (int)num_samples);
    if (ret == OPE_OK) ret = ope_encoder_drain(enc);

    ope_encoder_destroy(enc);
    ope_comments_destroy(comments);

    if (ret == OPE_OK) {
        result->data = membuf.buf;
        result->size = membuf.len;
        result->input_rate = sample_rate;
        result->channels = channels;
        result->samples_encoded = data.nb_encoded;
        return OPUS_QEXT_OK;
    } else {
        dynbuf_free(&membuf);
        return OPUS_QEXT_ERR_ENCODE;
    }
}

OPUS_QEXT_EXPORT int opus_qext_decode_pcm(const unsigned char *opus_data,
                                            size_t opus_len,
                                            float **out_pcm,
                                            size_t *out_num_samples,
                                            int *out_sample_rate,
                                            int *out_channels,
                                            const OpusQextDecParams *params) {
    OpusQextDecParams p;
    OggOpusFile *st;
    DynBuf pcm_out;
    int ret, rate, channels;
    opus_int64 total_samples;

    if (!opus_data || !out_pcm) return OPUS_QEXT_ERR_ARGS;

    if (!params) {
        opus_qext_dec_params_init(&p);
        p.fp = 1; /* force float for PCM mode */
        params = &p;
    }

    /* Force float output for PCM mode */
    OpusQextDecParams fp_params = *params;
    fp_params.fp = 1;

    st = op_open_memory(opus_data, opus_len, &ret);
    if (!st) return OPUS_QEXT_ERR_OPEN;

    dynbuf_init(&pcm_out);
    ret = decode_core(st, &pcm_out, &fp_params, &rate, &channels, &total_samples, 0);
    op_free(st);

    if (ret == OPUS_QEXT_OK) {
        *out_pcm = (float*)pcm_out.buf;
        if (out_num_samples) *out_num_samples = (size_t)total_samples;
        if (out_sample_rate) *out_sample_rate = rate;
        if (out_channels) *out_channels = channels;
    } else {
        dynbuf_free(&pcm_out);
    }
    return ret;
}

OPUS_QEXT_EXPORT void opus_qext_free(void *ptr) {
    free(ptr);
}

OPUS_QEXT_EXPORT const char *opus_qext_error_string(int error) {
    switch (error) {
        case OPUS_QEXT_OK:           return "Success";
        case OPUS_QEXT_ERR_ALLOC:    return "Memory allocation failed";
        case OPUS_QEXT_ERR_OPEN:     return "Failed to open file";
        case OPUS_QEXT_ERR_FORMAT:   return "Unsupported input format";
        case OPUS_QEXT_ERR_ENCODE:   return "Encoding error";
        case OPUS_QEXT_ERR_DECODE:   return "Decoding error";
        case OPUS_QEXT_ERR_WRITE:    return "Write error";
        case OPUS_QEXT_ERR_ARGS:     return "Invalid arguments";
        case OPUS_QEXT_ERR_INTERNAL: return "Internal error";
        default:                     return "Unknown error";
    }
}

OPUS_QEXT_EXPORT const char *opus_qext_get_version(void) {
    return "opus_qext_api 1.0 (opus-tools 0.2-qext)";
}
