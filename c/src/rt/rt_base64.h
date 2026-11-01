/* Base64 (standard alphabet, padded) for the saved-state string.
 *
 * Decoding mirrors Python's non-validating base64.b64decode: characters outside the alphabet
 * (spaces, newlines, "!") are discarded first; then a wrong amount of padding or a leftover
 * single character is an error (binascii.Error), which the state restore turns into an empty
 * state. */
#ifndef HC_RT_BASE64_H
#define HC_RT_BASE64_H

#include "rt/rt.h"
#include "rt/rt_arena.h"

/* NUL-terminated ASCII output (the NUL is not counted). */
RT_MUST_CHECK py_exc rt_base64_encode(rt_arena *arena, const unsigned char *data, size_t len,
                                      const char **out, size_t *out_len);

/* PYX_VALUE_ERROR for bad padding, and for any byte >= 0x80 (the state arrives as a str,
 * and b64decode refuses a str that is not ASCII). Data after complete padding is ignored, as
 * in Python ("QQ==QUJD" is b"A"). text bytes are read as unsigned char. */
RT_MUST_CHECK py_exc rt_base64_decode(rt_arena *arena, const char *text, size_t len,
                                      unsigned char **out, size_t *out_len);

#endif
