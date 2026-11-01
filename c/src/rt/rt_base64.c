/* Base64 (rt_base64.h), with Python's base64.b64encode / non-validating b64decode behavior. */
#include "rt/rt_base64.h"

static const char B64_ALPHABET[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int b64_value(unsigned char c)
{
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    if (c == '+') {
        return 62;
    }
    if (c == '/') {
        return 63;
    }
    return -1;
}

/* C only */
py_exc rt_base64_encode(rt_arena *arena, const unsigned char *data, size_t len, const char **out,
                        size_t *out_len)
{
    size_t groups = len / 3 + (len % 3 != 0 ? 1 : 0);
    size_t n;
    size_t i;
    size_t k = 0;
    char *buf;

    if (groups > (SIZE_MAX - 1) / 4) {
        return PYX_MEMORY_ERROR;
    }
    n = groups * 4;
    buf = (char *)rt_arena_alloc(arena, n + 1);
    PY_ALLOC_OK(buf);
    for (i = 0; i + 3 <= len; i += 3) {
        uint32_t v = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8) | data[i + 2];
        buf[k++] = B64_ALPHABET[(v >> 18) & 63u];
        buf[k++] = B64_ALPHABET[(v >> 12) & 63u];
        buf[k++] = B64_ALPHABET[(v >> 6) & 63u];
        buf[k++] = B64_ALPHABET[v & 63u];
    }
    if (len - i == 1) {
        uint32_t v = (uint32_t)data[i] << 16;
        buf[k++] = B64_ALPHABET[(v >> 18) & 63u];
        buf[k++] = B64_ALPHABET[(v >> 12) & 63u];
        buf[k++] = '=';
        buf[k++] = '=';
    } else if (len - i == 2) {
        uint32_t v = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8);
        buf[k++] = B64_ALPHABET[(v >> 18) & 63u];
        buf[k++] = B64_ALPHABET[(v >> 12) & 63u];
        buf[k++] = B64_ALPHABET[(v >> 6) & 63u];
        buf[k++] = '=';
    }
    buf[k] = '\0';
    *out = buf;
    *out_len = k;
    return PYX_OK;
}

/* C only */
py_exc rt_base64_decode(rt_arena *arena, const char *text, size_t len, unsigned char **out,
                        size_t *out_len)
{
    const unsigned char *s = (const unsigned char *)text;
    unsigned char *buf;
    size_t n = 0;
    size_t i;
    unsigned int quad_pos = 0;
    unsigned int pads = 0;
    unsigned int acc = 0;

    for (i = 0; i < len; i++) {
        if (s[i] >= 0x80u) {
            /* b64decode of a str with a non-ASCII character raises ValueError. */
            return PYX_VALUE_ERROR;
        }
    }
    buf = (unsigned char *)rt_arena_alloc(arena, len / 4 * 3 + 3);
    PY_ALLOC_OK(buf);
    for (i = 0; i < len; i++) {
        unsigned char c = s[i];
        int v;
        if (c == '=') {
            if (quad_pos >= 2) {
                pads++;
                if (quad_pos + pads >= 4) {
                    /* Complete padding ends the data; the rest is ignored. */
                    *out = buf;
                    *out_len = n;
                    return PYX_OK;
                }
            }
            continue;
        }
        v = b64_value(c);
        if (v < 0) {
            continue;
        }
        pads = 0;
        switch (quad_pos) {
        case 0:
            acc = (unsigned int)v;
            quad_pos = 1;
            break;
        case 1:
            buf[n++] = (unsigned char)(((acc << 2) | ((unsigned int)v >> 4)) & 0xFFu);
            acc = (unsigned int)v & 0x0Fu;
            quad_pos = 2;
            break;
        case 2:
            buf[n++] = (unsigned char)(((acc << 4) | ((unsigned int)v >> 2)) & 0xFFu);
            acc = (unsigned int)v & 0x03u;
            quad_pos = 3;
            break;
        default:
            buf[n++] = (unsigned char)(((acc << 6) | (unsigned int)v) & 0xFFu);
            quad_pos = 0;
            break;
        }
    }
    if (quad_pos != 0) {
        return PYX_VALUE_ERROR;
    }
    *out = buf;
    *out_len = n;
    return PYX_OK;
}
