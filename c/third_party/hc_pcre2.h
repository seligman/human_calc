/* PCRE2 configuration for its users (src/rt/py_re.c, src/hc_shared.c) and for the library
 * build (hc_pcre2.c): 32-bit code units (offsets are code points, like Python), static
 * linking (without PCRE2_STATIC, Windows declares every function dllimport).
 */
#ifndef HC_HC_PCRE2_H
#define HC_HC_PCRE2_H

#include "hc_namespace.h"

#if defined(PCRE2_CODE_UNIT_WIDTH) && PCRE2_CODE_UNIT_WIDTH != 32
#error "human_calc uses PCRE2 with 32-bit code units only"
#endif
#ifndef PCRE2_CODE_UNIT_WIDTH
#define PCRE2_CODE_UNIT_WIDTH 32
#endif
#ifndef PCRE2_STATIC
#define PCRE2_STATIC
#endif

#include "pcre2/src/pcre2.h"

#endif
