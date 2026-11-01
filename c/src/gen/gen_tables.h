/* Tables generated from the reference Python by c/tools/gen_tables.py.
 *
 * This header is hand-written: it is the contract between the generator (which writes
 * gen_unicode.c, gen_modifier.c, gen_word.c and gen_regex.c next to it) and the engine. Data
 * sizes are exported as const variables, so a Python table edit only regenerates the .c
 * files. Only the two enums below (ModType, ReId) name Python things one by one; the
 * generator checks them and stops with a message if Python grew a new unit category or regex
 * call site, which then needs a one-line edit here.
 *
 * Conventions: strings are UTF-32 arrays with a length (comparable with rt_str directly), or
 * ASCII C strings where the data is ASCII by nature. Doubles are stored as IEEE-754 bit
 * patterns (gen_num_double) so no compiler ever parses a decimal literal. All arrays are
 * const: the library has no mutable global state.
 */
#ifndef HC_GEN_TABLES_H
#define HC_GEN_TABLES_H

#include "rt/rt.h"

/* Provenance, e.g. "3.12" (the reference major.minor) and "15.0.0". */
extern const char GEN_PYTHON_VERSION[];
extern const char GEN_UNICODE_VERSION[];

/* ==== Unicode (unicodedata of the reference Python) ===================================== */

typedef struct GenUniRange {
    uint32_t first;
    uint32_t last;    /* inclusive */
    uint8_t value;
} GenUniRange;

/* General category classes (py_uni_class values), sorted, non-overlapping; code points not
 * covered are PY_UNI_OTHER. */
extern const GenUniRange GEN_UNI_CLASS[];
extern const size_t GEN_UNI_CLASS_COUNT;

typedef struct GenUniMap {
    uint32_t cp;
    uint32_t to;
} GenUniMap;

/* Code points whose str.lower() is a single different code point, sorted by cp. U+0130 (two
 * code points) is not listed and is handled in code; U+03A3 maps to U+03C3 here and the
 * Final_Sigma rule is applied in code. */
extern const GenUniMap GEN_UNI_LOWER[];
extern const size_t GEN_UNI_LOWER_COUNT;

/* Case flags for the Final_Sigma rule, sorted ranges: bit 0 = case-ignorable, bit 1 = cased
 * (only meaningful when bit 0 is clear: CPython skips case-ignorable code points without
 * looking at cased). Derived by probing the reference Python's str.lower(). */
#define GEN_UNI_CASE_IGNORABLE 1u
#define GEN_UNI_CASED 2u
extern const GenUniRange GEN_UNI_CASE[];
extern const size_t GEN_UNI_CASE_COUNT;

/* The code points of str.isspace() (29 in Unicode 15.0), sorted. */
extern const uint32_t GEN_UNI_SPACE[];
extern const size_t GEN_UNI_SPACE_COUNT;

/* NFC data. Canonical combining classes (non-zero only), sorted ranges. */
extern const GenUniRange GEN_UNI_CCC[];
extern const size_t GEN_UNI_CCC_COUNT;

/* The code points NFC's quick check does not pass as they are (value 1), sorted ranges: a
 * non-zero combining class, or NFC_QC No (the code point alone changes) or Maybe (the second
 * half of a primary composite, Hangul V and T jamo). A string with none of them is its own
 * NFC (UAX #15 quick check; CPython's normalize returns it unchanged). None is below U+0300. */
extern const GenUniRange GEN_UNI_NFC_SLOW[];
extern const size_t GEN_UNI_NFC_SLOW_COUNT;

/* Full (recursively applied) canonical decompositions, Hangul syllables excluded (they are
 * algorithmic), sorted by cp; each points at len code points in GEN_UNI_DECOMP_DATA. */
typedef struct GenUniDecomp {
    uint32_t cp;
    uint16_t offset;
    uint8_t len;
} GenUniDecomp;
extern const GenUniDecomp GEN_UNI_DECOMP[];
extern const size_t GEN_UNI_DECOMP_COUNT;
extern const uint32_t GEN_UNI_DECOMP_DATA[];

/* Primary composites: (first, second) -> composite, sorted by (first, second). Composition
 * exclusions, singletons and non-starter decompositions are left out; Hangul is
 * algorithmic. */
typedef struct GenUniCompose {
    uint32_t first;
    uint32_t second;
    uint32_t composite;
} GenUniCompose;
extern const GenUniCompose GEN_UNI_COMPOSE[];
extern const size_t GEN_UNI_COMPOSE_COUNT;

/* ==== Units (calc/modifier.py: _data, _lookup, _attached, _spaces, _extra_mappings) ===== */

/* The unit categories, _data[x][0], in the order of the Python table literal. */
typedef enum ModType {
    MT_VOLUME = 0,
    MT_WEIGHT,
    MT_LENGTH,
    MT_BYTES,
    MT_TIME,
    MT_MONTH,
    MT_ENGLISH,
    MT_BASE,
    MT_TEMPERATURE,
    MT_CURRENCY,
    MT_LENGTH_TIME,   /* "length/time" (synthesized rates) */
    MT_BYTES_TIME,    /* "bytes/time" */
    MT__COUNT
} ModType;

/* The Python type names ("volume", ..., "length/time"). */
extern const char *const GEN_MODTYPE_NAME[MT__COUNT];
/* For a rate type "a/b": {type of a, type of b} (type_name.split("/")); {-1, -1} otherwise. */
extern const int8_t GEN_MODTYPE_SPLIT[MT__COUNT][2];

/* A Python number from a table: int or float. */
typedef struct GenNum {
    uint8_t is_int;
    int64_t i;          /* when is_int */
    uint64_t f_bits;    /* IEEE-754 bits of the float, when !is_int */
} GenNum;

typedef enum GenSizeKind {
    GS_NUM = 0,    /* an int or float factor */
    GS_CODE = 1,   /* a string: currency ISO code, base letter, temperature letter */
    GS_RATE = 2    /* a tuple (numerator factor, denominator factor) */
} GenSizeKind;

/* _data[key][1] */
typedef struct GenModSize {
    uint8_t kind;          /* GenSizeKind */
    GenNum num;            /* GS_NUM */
    const char *code;      /* GS_CODE, ASCII ("USD", "h", "f") */
    GenNum rate[2];        /* GS_RATE */
} GenModSize;

/* One key of _data / _lookup. */
typedef struct GenModKey {
    const uint32_t *key;
    uint16_t key_len;
    uint16_t canon;        /* row of _lookup[key] (the canonical display name) in GEN_MOD_KEYS */
    uint16_t order;        /* position of the key in _data's insertion order */
    uint8_t type;          /* ModType of _data[key][0] */
    uint8_t attached;      /* key in _attached */
    uint8_t spaced;        /* key in _spaces */
    GenModSize size;
} GenModKey;

/* Every key, sorted by code points (binary search). */
extern const GenModKey GEN_MOD_KEYS[];
extern const size_t GEN_MOD_KEY_COUNT;
/* GEN_MOD_KEYS rows in _data insertion order (decompose_types scans _data in order). The
 * Python table writes temperature and currency as set literals, whose order follows the
 * string hash seed; the generator runs with PYTHONHASHSEED=0 so this file is reproducible
 * (no engine result depends on that order). */
extern const uint16_t GEN_MOD_ORDER[];

/* _extra_mappings: key -> the row of the mapped _data key ("kph" -> "km/h"), sorted by key. */
typedef struct GenModExtra {
    const uint32_t *key;
    uint16_t key_len;
    uint16_t target;
} GenModExtra;
extern const GenModExtra GEN_MOD_EXTRA[];
extern const size_t GEN_MOD_EXTRA_COUNT;

/* Modifier.get_space_tokens(), in _lookup order: the multi-word key, its replacement (the key
 * with spaces as U+E000), and the translated pattern
 * (?<![A-Za-z]) + re.escape(key) + (?![A-Za-z]) with re.IGNORECASE (pieces as in GenPattern). */
typedef struct GenSpaceToken {
    const uint32_t *key;
    uint16_t key_len;
    const uint32_t *replace;
    uint16_t replace_len;
    const char *const *pattern;   /* NULL-terminated pieces */
} GenSpaceToken;
extern const GenSpaceToken GEN_SPACE_TOKENS[];
extern const size_t GEN_SPACE_TOKEN_COUNT;

/* ==== Words (calc/word.py Word.ALL, MULTIPLIER, GROUPS; calc/special_tokens.py MONTHS) === */

/* value = small * 10**pow10 (so no C literal needs more than 64 bits: undecillion is
 * {1, 36}); order = Word.ALL insertion order. */
typedef struct GenWord {
    const char *name;      /* ASCII, e.g. "twenty-one" */
    uint8_t small;
    uint8_t pow10;
    uint8_t multiplier;    /* in Word.MULTIPLIER */
    uint8_t group;         /* in Word.GROUPS */
} GenWord;
extern const GenWord GEN_WORDS[];
extern const size_t GEN_WORD_COUNT;

typedef struct GenMonth {
    const char *name;      /* "january", ..., "sept" in MONTHS insertion order */
    uint8_t month;
} GenMonth;
extern const GenMonth GEN_MONTHS[];
extern const size_t GEN_MONTH_COUNT;

/* ==== Regular expressions ============================================================= */

/* One id per re.* call site in the engine (the 44 space-token patterns of
 * Calc._parse_string #2 are in GEN_SPACE_TOKENS). The comment gives the Python function, the
 * position of the call among that function's re.* calls (#n, in source order) and the call;
 * the generator keys its captured patterns the same way (DESIGN.md 11.3), and
 * GEN_PATTERNS[id].site has the line. */
typedef enum ReId {
    /* calc/__init__.py Calc._normalize_unicode */
    RE_NORM_FULLWIDTH = 0,     /* #0 re.sub, callable (fullwidth -> ASCII) */
    RE_NORM_SPACES,            /* #1 re.sub " " */
    RE_NORM_WHITESPACE,        /* #2 re.sub r"\s+" -> " " */
    RE_NORM_SCI_MINUS,         /* #3 re.sub "-" */
    RE_NORM_HYPHENS,           /* #4 re.sub "-" */
    RE_NORM_MINUS,             /* #5 re.sub " - " */
    RE_NORM_PLUS,              /* #6 re.sub "+" */
    RE_NORM_INVERSE_TRIG,      /* #7 re.sub r" a\1 ", IGNORECASE (a callable in C) */
    RE_NORM_DEGREE,            /* #8 re.sub degree sign */
    RE_NORM_FRACTION_SLASH,    /* #9 re.sub, callable slashed, IGNORECASE */
    RE_NORM_VULGAR_FRACTION,   /* #10 re.sub, callable (_mixed) */
    RE_NORM_TIMES,             /* #11 re.sub " * " */
    RE_NORM_DIVIDE,            /* #12 re.sub " / " */
    RE_NORM_OPEN_BRACKET,      /* #13 re.sub "(" */
    RE_NORM_CLOSE_BRACKET,     /* #14 re.sub ")" */
    RE_NORM_LESS_EQUAL,        /* #15 re.sub "<=" */
    RE_NORM_GREATER_EQUAL,     /* #16 re.sub ">=" */
    RE_NORM_PI,                /* #17 re.sub " pi " */
    RE_NORM_DOUBLE_QUOTE,      /* #18 re.sub '"' */
    RE_NORM_SINGLE_QUOTE,      /* #19 re.sub "'" */
    /* calc/__init__.py Calc._parse_string */
    RE_PARSE_LABEL,            /* #0 re.match */
    RE_PARSE_LABEL_WORD,       /* #1 re.fullmatch */
    /* calc/special_tokens.py SpecialTokens */
    RE_ST_DIGIT_RUNS,          /* find_dates #0 re.findall */
    RE_ST_AM_PM,               /* find_dates #1 re.match, IGNORECASE */
    RE_ST_TEXT_DATE,           /* find_text_dates #0 re.sub, callable, IGNORECASE | ASCII */
    RE_ST_WORD_HYPHENS,        /* find_words #0 re.sub, callable join, IGNORECASE */
    RE_ST_WORD,                /* find_words #1 re.sub, callable, IGNORECASE */
    RE_ST_POWER_PARTS,         /* find_superscripts.<locals>.power #0 re.findall */
    RE_ST_SUPERSCRIPTS,        /* find_superscripts #0 re.sub, callable power */
    RE_ST_FUNCTION,            /* find_functions #0 re.sub, callable, IGNORECASE | ASCII */
    RE_ST_SCIENTIFIC,          /* find_scientific #0 re.sub, callable */
    RE_ST_NUMBER_RUNS,         /* find_numbers #0 re.findall */
    RE_ST_BASE_LITERAL,        /* find_numbers #1 re.compile(...).match */
    /* calc/value.py */
    RE_VALUE_NUMBER,           /* Value.as_value #0 re.search */
    /* calc/variable.py */
    RE_VARIABLE_NAME,          /* Variable.as_variable #0 re.search */
    RE__COUNT
} ReId;

/* A translated pattern. The text is ASCII PCRE2 syntax for a 32-bit, non-Unicode build
 * (non-ASCII as \x{...}), split into pieces of at most 2000 characters so no C string literal
 * exceeds portable compiler limits; hc_shared joins the pieces and widens them to code
 * points before compiling. */
typedef struct GenPattern {
    const char *const *pieces;   /* NULL-terminated */
    uint32_t flags;              /* PY_RE_CASELESS or 0 */
    uint8_t groups;              /* capturing groups (<= PY_RE_MAX_GROUPS, checked twice) */
    const char *site;            /* "calc/__init__.py:152 Calc._normalize_unicode #2" */
    const char *python;          /* the Python pattern source (ASCII-escaped), for reference */
} GenPattern;
extern const GenPattern GEN_PATTERNS[RE__COUNT];

/* ==== Function-local tables that generated patterns embed ============================== */

/* Captured from the running Python function's locals by the generator (DESIGN.md 11.1), in
 * dict insertion order, so a new entry in Python reaches both the regenerated pattern and the
 * C lookup. C looks them up linearly (at most 18 entries). */

/* calc/__init__.py Calc._normalize_unicode: superscripts and subscripts (cp -> ASCII digit). */
extern const GenUniMap GEN_NORM_SUPERSCRIPTS[];
extern const size_t GEN_NORM_SUPERSCRIPTS_COUNT;
extern const GenUniMap GEN_NORM_SUBSCRIPTS[];
extern const size_t GEN_NORM_SUBSCRIPTS_COUNT;

/* calc/__init__.py Calc._normalize_unicode: fractions (cp -> (numerator, denominator)). */
typedef struct GenFraction {
    uint32_t cp;
    uint8_t numerator;
    uint8_t denominator;
} GenFraction;
extern const GenFraction GEN_NORM_FRACTIONS[];
extern const size_t GEN_NORM_FRACTIONS_COUNT;

/* calc/special_tokens.py SpecialTokens.find_superscripts: superscripts (cp -> '0'..'9', '+',
 * '-'). */
extern const GenUniMap GEN_ST_SUPERSCRIPTS[];
extern const size_t GEN_ST_SUPERSCRIPTS_COUNT;

/* ==== helpers ========================================================================== */

static inline double gen_num_double(const GenNum *n)
{
    union {
        uint64_t u;
        double d;
    } v;
    if (n->is_int) {
        return (double)n->i;
    }
    v.u = n->f_bits;
    return v.d;
}

#endif
