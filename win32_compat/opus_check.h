/* Opus check macros for libopusenc compatibility */
#ifndef OPUS_CHECK_H
#define OPUS_CHECK_H

/* libopusenc uses __opus_check_* macros which are not defined in standard opus headers
   We define them here as simple pass-through macros */

#define __opus_check_int(x) (x)
#define __opus_check_int_ptr(x) (x)
#define __opus_check_uint_ptr(x) (x)
#define __opus_check_void_ptr(x) (x)

#endif /* OPUS_CHECK_H */
