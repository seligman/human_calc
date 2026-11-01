/* PCRE2 10.49 as one translation unit: the configuration below (NON-AUTOTOOLS-BUILD allows
 * macros instead of a config.h; HAVE_CONFIG_H is defined nowhere), then the 21 vendored
 * sources, untouched (third_party/pcre2/VENDOR.md). 32-bit code units, no Unicode support,
 * no JIT. Compiled without warnings (-w, /W0): the code is not ours to fix. The engine never
 * relies on the compiled-in limits: py_re.c passes its own contexts with explicit limits.
 */
#define SUPPORT_PCRE2_32 1
#define LINK_SIZE 2
#define MATCH_LIMIT 10000000
#define MATCH_LIMIT_DEPTH MATCH_LIMIT
#define HEAP_LIMIT 65536
#define NEWLINE_DEFAULT 2
#define PARENS_NEST_LIMIT 250
#define MAX_NAME_SIZE 128
#define MAX_NAME_COUNT 10000
#define MAX_VARLOOKBEHIND 255
#define PCRE2_EXPORT

#if defined(HAVE_CONFIG_H) || defined(SUPPORT_UNICODE) || defined(SUPPORT_JIT)
#error "human_calc builds PCRE2 without config.h, Unicode support or JIT"
#endif

#include "hc_pcre2.h"

#include "pcre2/src/pcre2_auto_possess.c"
#include "pcre2/src/pcre2_chartables.c"
#include "pcre2/src/pcre2_chkdint.c"
#include "pcre2/src/pcre2_compile.c"
#include "pcre2/src/pcre2_compile_cgroup.c"
#include "pcre2/src/pcre2_compile_class.c"
#include "pcre2/src/pcre2_context.c"
#include "pcre2/src/pcre2_error.c"
#include "pcre2/src/pcre2_find_bracket.c"
#include "pcre2/src/pcre2_match.c"
#include "pcre2/src/pcre2_match_data.c"
#include "pcre2/src/pcre2_newline.c"
#include "pcre2/src/pcre2_pattern_info.c"
#include "pcre2/src/pcre2_script_run.c"
#include "pcre2/src/pcre2_string_utils.c"
#include "pcre2/src/pcre2_study.c"
#include "pcre2/src/pcre2_substring.c"
#include "pcre2/src/pcre2_tables.c"
#include "pcre2/src/pcre2_ucd.c"
#include "pcre2/src/pcre2_valid_utf.c"
#include "pcre2/src/pcre2_xclass.c"
