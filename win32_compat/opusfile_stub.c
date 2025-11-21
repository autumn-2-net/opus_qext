/* Stub for opusfile HTTP functions when HTTP support is disabled */
#include <opusfile.h>

/* Stub for op_open_url when HTTP support is not available */
OggOpusFile *op_open_url(const char *url, int *error, const OpusServerInfo *info) {
    /* Always fail - HTTP support not compiled in */
    if (error) *error = OP_ENOTFORMAT;
    (void)url;
    (void)info;
    return NULL;
}
