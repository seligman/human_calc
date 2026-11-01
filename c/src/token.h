/* calc/token.py: the token object model shared by every engine module.
 *
 * Python has a class per token kind (Token and its subclasses). C has ONE struct, Token, with
 * a class tag (cls) and the union of every subclass's attributes as plain fields; behavior is
 * dispatched through the const class table TOKEN_CLASSES (the "vtable"). isinstance() and
 * is_types() follow the Python hierarchy, including Op_In being an Op_Div and every Op_* being
 * an Operator.
 *
 * Python attribute -> C field:
 *   Token.value     -> value   (rt_str) for every class except Value and Superscript
 *   Value.value     -> data    (Obj: an int/float/complex py_num, or a DateValue)
 *   Superscript.value -> power (py_num, an int)
 *   Token.prev/next -> prev/next
 *   Value.modifier  -> modifier (a CLS_Modifier Token, shared by pointer as in Python)
 *   Value.exact_days/span -> has_exact_days+exact_days / has_span+span
 *   Modifier.orig_value -> orig_value; Paren.done -> done; Variable.psuedo -> psuedo
 *   ad hoc tok.spaced (default True) and Superscript.after_function (default False) are real
 *   fields; every constructor sets the Python getattr defaults and clone() never copies them.
 *
 * Every token lives in an arena (Calc.arena during a calc() call). Nothing is freed while a
 * line is worked out: removed nodes keep their stale links, Modifier tokens become value
 * modifiers, handlers return nodes that were inside the replaced run, all as in Python.
 */
#ifndef HC_TOKEN_H
#define HC_TOKEN_H

#include "rt/rt.h"
#include "rt/rt_arena.h"
#include "rt/py_num.h"
#include "rt/py_str.h"
#include "date_value.h"

typedef struct Calc Calc;
typedef struct Token Token;

/* token.py:13-19 Token.SPACE, UNPRINTABLE, SPECIAL, ALL */
#define TOKEN_SPACE 0xE000u
#define TOKEN_UNPRINTABLE 0xE001u
#define TOKEN_SPECIAL_0 0xE010u   /* Token.SPECIAL[d] == TOKEN_SPECIAL_0 + d, d in 0..9 */
/* x in Token.ALL */
bool Token_in_ALL(uint32_t cp);
/* x in Token.SPECIAL (one code point) */
bool Token_in_SPECIAL(uint32_t cp);

/* The Python classes. Order matters only for TOKEN_CLASSES. */
typedef enum TokenClass {
    CLS_Token = 0,       /* abstract base */
    CLS_Value,
    CLS_Operator,        /* abstract base of the Op_* classes */
    CLS_Op_Perc,
    CLS_Op_Shift,
    CLS_Op_And,
    CLS_Op_Or,
    CLS_Op_Xor,
    CLS_Op_Mod,
    CLS_Op_Mult,
    CLS_Op_Add,
    CLS_Op_Div,
    CLS_Op_In,           /* subclass of Op_Div */
    CLS_Op_Power,
    CLS_Op_Sub,
    CLS_Modifier,
    CLS_Paren,
    CLS_Convert,
    CLS_Assign,
    CLS_Variable,
    CLS_Constant,
    CLS_Function,
    CLS_String,
    CLS_Factorial,
    CLS_Superscript,
    CLS_Word,            /* static_only: never instantiated */
    CLS_ModifierToVar,   /* static_only: never instantiated */
    CLS__COUNT
} TokenClass;

/* The "other" argument of can_handle: every STEP_* constant of Operator, Word and
 * ModifierToVar, and None. */
typedef enum Step {
    STEP_NONE = 0,       /* None */
    STEP_VARIABLES,      /* ModifierToVar.STEP_VARIABLES "variables" */
    STEP_MERGE_MODS,     /* Operator.STEP_MERGE_MODS */
    STEP_EXPONENT,
    STEP_NEGATE,
    STEP_POWER_SIGN,
    STEP_MULTIPLY,
    STEP_ADD,
    STEP_SHIFT,
    STEP_AND,
    STEP_XOR,
    STEP_OR,
    STEP_NORMAL,         /* Word.STEP_NORMAL (unused in Python) */
    STEP_ALL             /* Word.STEP_ALL (unused in Python) */
} Step;

/* Variable.psuedo (sic) */
typedef enum Psuedo {
    PSUEDO_NONE = 0,
    PSUEDO_LAST,
    PSUEDO_AVERAGE,
    PSUEDO_SUM
} Psuedo;

/* Value.value */
struct Obj {
    bool is_date;     /* isinstance(value, DateValue) */
    py_num num;       /* when !is_date: int, float or complex */
    DateValue date;   /* when is_date */
};

/* Value.span: (forward_months, forward_days, months, back_days) from DateValue.diff_span */
struct Span {
    int64_t forward_months;
    double forward_days;
    int64_t months;
    double back_days;
};

struct Token {
    TokenClass cls;
    Token *prev;
    Token *next;

    rt_str value;          /* Token.value (str-valued classes) */
    Obj data;              /* Value.value */
    py_num power;          /* Superscript.value */

    bool spaced;           /* ad hoc (__init__.py:361); getattr default True */
    bool after_function;   /* ad hoc on Superscript (__init__.py:495); default False */

    Token *modifier;       /* Value.modifier */
    bool has_exact_days;   /* Value.exact_days is not None */
    double exact_days;
    bool has_span;         /* Value.span is not None */
    Span span;

    rt_str orig_value;     /* Modifier.orig_value */
    bool done;             /* Paren.done */
    Psuedo psuedo;         /* Variable.psuedo */
};

/* handle() returns (from_ins, to_ins, token): the run cur[from_ins] .. cur[to_ins] is replaced
 * by token (token NULL deletes the run; token may be a node from inside the run). */
typedef struct HandleResult {
    int from_ins;
    int to_ins;
    Token *token;
} HandleResult;

/* The per-class behavior table (Python's methods and class attributes). */
typedef bool (*TokenHandlesLhsFn)(const Token *self);
typedef bool (*TokenCanHandleFn)(Token *self, Calc *engine, Step other);
typedef py_exc (*TokenHandleFn)(Token *self, Calc *engine, HandleResult *out);
typedef py_exc (*TokenCloneFn)(rt_arena *arena, const Token *self, Token **out);
typedef py_exc (*TokenToStringFn)(rt_arena *arena, const Token *self, rt_str *out);

typedef struct TokenClassInfo {
    const char *name;               /* Python class name */
    TokenClass parent;              /* base class (CLS_Token for direct subclasses) */
    bool static_only;               /* Token.static_only (Word, ModifierToVar) */
    const char *desc;               /* get_desc(); NULL for the abstract classes */
    TokenHandlesLhsFn handles_lhs;  /* handles_lhs() */
    TokenCanHandleFn can_handle;    /* can_handle(); for static_only classes the token is the
                                       one being looked at (Word.can_handle(cur, engine, step)) */
    TokenHandleFn handle;
    TokenCloneFn clone;
    TokenToStringFn to_string;      /* to_string() */
} TokenClassInfo;

/* Defined in token.c from the functions in every module. Const: no mutable global state. */
extern const TokenClassInfo TOKEN_CLASSES[CLS__COUNT];

/* ---- Obj helpers ---------------------------------------------------------------------- */

Obj Obj_from_num(py_num n);
Obj Obj_from_date(DateValue d);
/* isinstance(x, (int, float)) */
bool Obj_is_number(const Obj *o);
/* The numeric payload; PYX_TYPE_ERROR for a DateValue (Python's implicit TypeError when a
 * date reaches arithmetic). */
RT_MUST_CHECK py_exc Obj_as_num(const Obj *o, py_num *out);
/* Python truthiness: numbers by value, a DateValue is always true. */
bool Obj_truthy(const Obj *o);
/* Deep copy (big ints) into another arena. */
RT_MUST_CHECK py_exc Obj_copy_to(rt_arena *dst, const Obj *o, Obj *out);

/* ---- Token (token.py) ----------------------------------------------------------------- */

/* Token.__init__ plus the subclass defaults: every field zero/None except spaced = true. */
RT_MUST_CHECK py_exc Token_new(rt_arena *arena, TokenClass cls, Token **out);

/* type(token) is cls or a subclass of it; false for NULL (isinstance(None, X)). */
bool Token_isinstance(const Token *token, TokenClass cls);
bool TokenClass_is_subclass(TokenClass cls, TokenClass base);

/* get_desc() */
const char *Token_get_desc(const Token *self);
/* handles_lhs() (dispatch) and the base implementation (False) */
bool Token_handles_lhs(const Token *self);
bool Token_handles_lhs_base(const Token *self);

/* is_types(*classes): self, self.next, ... are instances of the classes in order; false when
 * the chain runs out. Write it as TOKEN_IS_TYPES(t, CLS_Value, CLS_Operator, CLS_Value),
 * which counts the classes itself (no hand-kept count, no varargs). */
bool Token_is_types_n(const Token *self, const TokenClass *classes, size_t count);
#define TOKEN_IS_TYPES(self, ...)                                                            \
    Token_is_types_n((self), (const TokenClass[]){__VA_ARGS__},                              \
                     sizeof((const TokenClass[]){__VA_ARGS__}) / sizeof(TokenClass))

/* insert(new_value, from_value, to_value): replaces the run from_value..to_value with
 * new_value (NULL deletes it). *cur_out / *head_out get Python's return pair (new_value or a
 * neighbour, and the head; both NULL when the list became empty). Never frees anything. */
void Token_insert(Token *self, Token *new_value, Token *from_value, Token *to_value,
                  Token **cur_out, Token **head_out);

/* get_head() */
Token *Token_get_head(Token *self);
/* __getitem__(key): walk key links (negative = prev). NULL when the walk ends just past
 * either end (Python's None, e.g. self[2] with nothing after self.next), and also when it
 * would have to step beyond that (Python's AttributeError; every engine call site is guarded
 * against it). */
Token *Token_getitem(Token *self, int key);

/* `self.value == "x"` / `self.value in {...}` for any token: false when the value is not a
 * string (Value, Superscript), exactly as Python's == between a number/DateValue and a str.
 * Write the set form as TOKEN_VALUE_IN(t, "-", "+"). */
bool Token_value_is(const Token *self, const char *ascii);
bool Token_value_in(const Token *self, const char *const *set, size_t n);
#define TOKEN_VALUE_IN(self, ...)                                                            \
    Token_value_in((self), (const char *const[]){__VA_ARGS__},                               \
                   sizeof((const char *const[]){__VA_ARGS__}) / sizeof(const char *))

/* to_string() (dispatch), and the base Token.to_string: str(self.value) with every code point
 * of Token.ALL replaced by "_". */
RT_MUST_CHECK py_exc Token_to_string(rt_arena *arena, const Token *self, rt_str *out);
RT_MUST_CHECK py_exc Token_to_string_base(rt_arena *arena, const Token *self, rt_str *out);

/* __repr__: f"{get_desc()}[{to_string()}]" (Variable adds ":psuedo"). Debug only. */
RT_MUST_CHECK py_exc Token_repr(rt_arena *arena, const Token *self, rt_str *out);

/* list_to_string(): " ".join(x.to_string() for x in self.iter()) -- the engine's output. */
RT_MUST_CHECK py_exc Token_list_to_string(rt_arena *arena, const Token *self, rt_str *out);

/* clone(), can_handle() and handle() through the class table. */
RT_MUST_CHECK py_exc Token_clone(rt_arena *arena, const Token *self, Token **out);
bool Token_can_handle(Token *self, Calc *engine, Step other);
RT_MUST_CHECK py_exc Token_handle(Token *self, Calc *engine, HandleResult *out);

/* Python iterates with self.iter(); C uses: for (t = head; t != NULL; t = t->next). The next
 * link is read after the body, which pre-scan C in Calc._calc_nodes relies on. */

#endif
