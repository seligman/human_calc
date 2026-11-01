#include "token.h"

#include "assign.h"
#include "constant.h"
#include "convert.h"
#include "factorial.h"
#include "function.h"
#include "modifier.h"
#include "modifier_to_var.h"
#include "operator_.h"
#include "paren.h"
#include "string_.h"
#include "value.h"
#include "variable.h"
#include "word.h"

/* Token_list_to_string empties its scratch arena once it holds this many bytes. */
#define TOKEN_LIST_SCRATCH_MAX ((size_t)256 * 1024)

/* py: calc/token.py Token @b0a986dc */
bool Token_in_ALL(uint32_t cp)
{
    return cp == TOKEN_SPACE || cp == TOKEN_UNPRINTABLE || Token_in_SPECIAL(cp);
}

/* py: calc/token.py Token @b0a986dc */
bool Token_in_SPECIAL(uint32_t cp)
{
    return cp >= TOKEN_SPECIAL_0 && cp <= TOKEN_SPECIAL_0 + 9u;
}

/* The class table: name, parent, static_only, get_desc(), handles_lhs, can_handle, handle,
 * clone, to_string. NULL handlers are methods the Python class does not have. */
const TokenClassInfo TOKEN_CLASSES[CLS__COUNT] = {
    /* py: calc/token.py Token @b0a986dc */
    [CLS_Token] = {"Token", CLS_Token, false, NULL, Token_handles_lhs_base, NULL, NULL, NULL,
                   Token_to_string_base},
    /* py: calc/value.py Value.get_desc @0af3147b */
    [CLS_Value] = {"Value", CLS_Token, false, "val", Token_handles_lhs_base, NULL, NULL,
                   Value_clone, Value_to_string},
    /* py: calc/operator_.py Operator.get_desc @465e0917 */
    [CLS_Operator] = {"Operator", CLS_Token, false, "op", Token_handles_lhs_base,
                      Operator_can_handle, NULL, NULL, Token_to_string_base},
    [CLS_Op_Perc] = {"Op_Perc", CLS_Operator, false, "op", Token_handles_lhs_base,
                     Op_Perc_can_handle, Op_Perc_handle, Operator_clone, Token_to_string_base},
    [CLS_Op_Shift] = {"Op_Shift", CLS_Operator, false, "op", Operator_handles_lhs,
                      Operator_can_handle, Op_Shift_handle, Operator_clone, Token_to_string_base},
    [CLS_Op_And] = {"Op_And", CLS_Operator, false, "op", Operator_handles_lhs,
                    Operator_can_handle, Op_And_handle, Operator_clone, Token_to_string_base},
    [CLS_Op_Or] = {"Op_Or", CLS_Operator, false, "op", Operator_handles_lhs,
                   Operator_can_handle, Op_Or_handle, Operator_clone, Token_to_string_base},
    [CLS_Op_Xor] = {"Op_Xor", CLS_Operator, false, "op", Operator_handles_lhs,
                    Operator_can_handle, Op_Xor_handle, Operator_clone, Token_to_string_base},
    [CLS_Op_Mod] = {"Op_Mod", CLS_Operator, false, "op", Operator_handles_lhs,
                    Operator_can_handle, Op_Mod_handle, Operator_clone, Token_to_string_base},
    [CLS_Op_Mult] = {"Op_Mult", CLS_Operator, false, "op", Operator_handles_lhs,
                     Operator_can_handle, Op_Mult_handle, Operator_clone, Token_to_string_base},
    [CLS_Op_Add] = {"Op_Add", CLS_Operator, false, "op", Operator_handles_lhs,
                    Operator_can_handle, Op_Add_handle, Operator_clone, Token_to_string_base},
    [CLS_Op_Div] = {"Op_Div", CLS_Operator, false, "op", Operator_handles_lhs,
                    Operator_can_handle, Op_Div_handle, Operator_clone, Token_to_string_base},
    [CLS_Op_In] = {"Op_In", CLS_Op_Div, false, "op", Operator_handles_lhs, Operator_can_handle,
                   Op_In_handle, Operator_clone, Token_to_string_base},
    [CLS_Op_Power] = {"Op_Power", CLS_Operator, false, "op", Operator_handles_lhs,
                      Operator_can_handle, Op_Power_handle, Operator_clone, Token_to_string_base},
    [CLS_Op_Sub] = {"Op_Sub", CLS_Operator, false, "op", Operator_handles_lhs,
                    Operator_can_handle, Op_Sub_handle, Operator_clone, Token_to_string_base},
    /* py: calc/modifier.py Modifier.get_desc @da999ebb */
    [CLS_Modifier] = {"Modifier", CLS_Token, false, "modify", Token_handles_lhs_base,
                      Modifier_can_handle, Modifier_handle, Modifier_clone, Token_to_string_base},
    /* py: calc/paren.py Paren.get_desc @9d356e99 */
    [CLS_Paren] = {"Paren", CLS_Token, false, "paren", Token_handles_lhs_base, Paren_can_handle,
                   Paren_handle, Paren_clone, Token_to_string_base},
    /* py: calc/convert.py Convert.get_desc @f0b2909e */
    [CLS_Convert] = {"Convert", CLS_Token, false, "convert", Convert_handles_lhs,
                     Convert_can_handle, Convert_handle, Convert_clone, Token_to_string_base},
    /* py: calc/assign.py Assign.get_desc @54f26c77 */
    [CLS_Assign] = {"Assign", CLS_Token, false, "assign", Token_handles_lhs_base,
                    Assign_can_handle, Assign_handle, Assign_clone, Token_to_string_base},
    /* py: calc/variable.py Variable.get_desc @b9665779 */
    [CLS_Variable] = {"Variable", CLS_Token, false, "var", Token_handles_lhs_base,
                      Variable_can_handle, Variable_handle, Variable_clone, Token_to_string_base},
    /* py: calc/constant.py Constant.get_desc @81c339c6 */
    [CLS_Constant] = {"Constant", CLS_Token, false, "const", Token_handles_lhs_base,
                      Constant_can_handle, Constant_handle, Constant_clone, Constant_to_string},
    /* py: calc/function.py Function.get_desc @5f40046c */
    [CLS_Function] = {"Function", CLS_Token, false, "func", Token_handles_lhs_base,
                      Function_can_handle, Function_handle, Function_clone, Token_to_string_base},
    /* py: calc/string.py String.get_desc @aee861ef */
    [CLS_String] = {"String", CLS_Token, false, "str", Token_handles_lhs_base, String_can_handle,
                    String_handle, String_clone, Token_to_string_base},
    /* py: calc/factorial.py Factorial.get_desc @37136f64 */
    [CLS_Factorial] = {"Factorial", CLS_Token, false, "!", Token_handles_lhs_base,
                       Factorial_can_handle, Factorial_handle, Factorial_clone,
                       Token_to_string_base},
    /* py: calc/factorial.py Superscript.get_desc @c285de6a */
    [CLS_Superscript] = {"Superscript", CLS_Token, false, "power", Token_handles_lhs_base,
                         Superscript_can_handle, Superscript_handle, Superscript_clone,
                         Superscript_to_string},
    [CLS_Word] = {"Word", CLS_Token, true, NULL, Token_handles_lhs_base, Word_can_handle,
                  Word_handle, NULL, Token_to_string_base},
    /* py: calc/modifier_to_var.py ModifierToVar @8518dac4 */
    [CLS_ModifierToVar] = {"ModifierToVar", CLS_Token, true, NULL, Token_handles_lhs_base,
                           ModifierToVar_can_handle, ModifierToVar_handle, NULL,
                           Token_to_string_base},
};

/* ---- Obj helpers ---------------------------------------------------------------------- */

/* C only */
Obj Obj_from_num(py_num n)
{
    Obj o;
    memset(&o, 0, sizeof(o));
    o.is_date = false;
    o.num = n;
    return o;
}

/* C only */
Obj Obj_from_date(DateValue d)
{
    Obj o;
    memset(&o, 0, sizeof(o));
    o.is_date = true;
    o.date = d;
    return o;
}

/* C only */
bool Obj_is_number(const Obj *o)
{
    return !o->is_date && (py_num_is_int(o->num) || py_num_is_float(o->num));
}

/* C only */
py_exc Obj_as_num(const Obj *o, py_num *out)
{
    if (o->is_date) {
        return PYX_TYPE_ERROR;
    }
    *out = o->num;
    return PYX_OK;
}

/* C only */
bool Obj_truthy(const Obj *o)
{
    if (o->is_date) {
        return true;
    }
    return py_num_truthy(o->num);
}

/* C only */
py_exc Obj_copy_to(rt_arena *dst, const Obj *o, Obj *out)
{
    Obj r = *o;
    if (!o->is_date) {
        PY_TRY(py_num_copy_to(dst, o->num, &r.num));
    }
    *out = r;
    return PYX_OK;
}

/* ---- Token ------------------------------------------------------------------------------ */

/* py: calc/token.py Token.__init__ @1a3b4ae5 */
py_exc Token_new(rt_arena *arena, TokenClass cls, Token **out)
{
    Token *t = RT_NEW(arena, Token);
    PY_ALLOC_OK(t);
    t->cls = cls;
    t->value = RT_STR_EMPTY;
    t->prev = NULL;
    t->next = NULL;
    t->spaced = true;
    t->after_function = false;
    t->modifier = NULL;
    t->has_exact_days = false;
    t->has_span = false;
    t->orig_value = RT_STR_EMPTY;
    t->done = false;
    t->psuedo = PSUEDO_NONE;
    *out = t;
    return PYX_OK;
}

/* C only: issubclass(cls, base) over the parent column */
bool TokenClass_is_subclass(TokenClass cls, TokenClass base)
{
    int steps;
    for (steps = 0; steps <= (int)CLS__COUNT; steps++) {
        if ((unsigned)cls >= (unsigned)CLS__COUNT) {
            return false;
        }
        if (cls == base) {
            return true;
        }
        if (cls == CLS_Token) {
            return false;
        }
        cls = TOKEN_CLASSES[cls].parent;
    }
    return false;
}

/* C only: isinstance(token, cls) */
bool Token_isinstance(const Token *token, TokenClass cls)
{
    return token != NULL && TokenClass_is_subclass(token->cls, cls);
}

/* py: calc/token.py Token.get_desc @a580bf48 */
const char *Token_get_desc(const Token *self)
{
    const char *desc = NULL;
    if ((unsigned)self->cls < (unsigned)CLS__COUNT) {
        desc = TOKEN_CLASSES[self->cls].desc;
    }
    RT_DEBUG_ASSERT(desc != NULL);
    return desc != NULL ? desc : "";
}

/* C only: dispatch of handles_lhs() */
bool Token_handles_lhs(const Token *self)
{
    TokenHandlesLhsFn fn = TOKEN_CLASSES[self->cls].handles_lhs;
    if (fn == NULL) {
        return Token_handles_lhs_base(self);
    }
    return fn(self);
}

/* py: calc/token.py Token.handles_lhs @9f3920d5 */
bool Token_handles_lhs_base(const Token *self)
{
    RT_UNUSED(self);
    return false;
}

/* py: calc/token.py Token.is_types @b4569731 */
bool Token_is_types_n(const Token *self, const TokenClass *classes, size_t count)
{
    const Token *cur = self;
    size_t i;
    for (i = 0; i < count; i++) {
        if (cur == NULL) {
            return false;
        }
        if (!Token_isinstance(cur, classes[i])) {
            return false;
        }
        cur = cur->next;
    }
    return true;
}

/* py: calc/token.py Token.insert @90f71590 */
void Token_insert(Token *self, Token *new_value, Token *from_value, Token *to_value,
                  Token **cur_out, Token **head_out)
{
    if (from_value == NULL || to_value == NULL) {
        /* Python: AttributeError on None (every engine call site is in range) */
        RT_DEBUG_ASSERT(from_value != NULL && to_value != NULL);
        *cur_out = self;
        *head_out = self != NULL ? Token_get_head(self) : NULL;
        return;
    }
    if (new_value == NULL) {
        if (from_value->prev != NULL) {
            from_value->prev->next = to_value->next;
            new_value = from_value->prev;
        }
        if (to_value->next != NULL) {
            to_value->next->prev = from_value->prev;
            if (new_value == NULL) {
                new_value = to_value->next;
            }
        }
        if (new_value == NULL) {
            *cur_out = NULL;
            *head_out = NULL;
            return;
        }
    } else {
        new_value->prev = from_value->prev;
        new_value->next = to_value->next;
        if (new_value->prev != NULL) {
            new_value->prev->next = new_value;
        }
        if (new_value->next != NULL) {
            new_value->next->prev = new_value;
        }
    }
    *cur_out = new_value;
    *head_out = Token_get_head(new_value);
}

/* py: calc/token.py Token.get_head @267c4adf */
Token *Token_get_head(Token *self)
{
    Token *head = self;
    while (head->prev != NULL) {
        head = head->prev;
    }
    return head;
}

/* py: calc/token.py Token.__getitem__ @f2a16e91 */
Token *Token_getitem(Token *self, int key)
{
    Token *temp = self;
    while (key > 0) {
        if (temp == NULL) {
            return NULL;
        }
        temp = temp->next;
        key -= 1;
    }
    while (key < 0) {
        if (temp == NULL) {
            return NULL;
        }
        temp = temp->prev;
        key += 1;
    }
    return temp;
}

/* py: calc/token.py Token.iter @2c189d00 */
/* iter(): C walks the list with for (t = head; t != NULL; t = t->next) (token.h). */

/* C only: is self.value a str (every class but Value and Superscript) */
static bool Token_has_str_value(const Token *self)
{
    return self->cls != CLS_Value && self->cls != CLS_Superscript;
}

/* C only: self.value == "x" */
bool Token_value_is(const Token *self, const char *ascii)
{
    if (self == NULL || !Token_has_str_value(self)) {
        return false;
    }
    return rt_str_eq_ascii(self->value, ascii);
}

/* C only: self.value in {...} */
bool Token_value_in(const Token *self, const char *const *set, size_t n)
{
    if (self == NULL || !Token_has_str_value(self)) {
        return false;
    }
    return rt_str_in_ascii(self->value, set, n);
}

/* C only: dispatch of to_string() */
py_exc Token_to_string(rt_arena *arena, const Token *self, rt_str *out)
{
    TokenToStringFn fn = TOKEN_CLASSES[self->cls].to_string;
    if (fn == NULL) {
        return Token_to_string_base(arena, self, out);
    }
    return fn(arena, self, out);
}

/* py: calc/token.py Token.to_string @5053ce75 */
py_exc Token_to_string_base(rt_arena *arena, const Token *self, rt_str *out)
{
    rt_str value = self->value;
    rt_sb sb;
    size_t i;
    bool any = false;
    for (i = 0; i < value.len; i++) {
        if (Token_in_ALL(value.cp[i])) {
            any = true;
            break;
        }
    }
    if (!any) {
        *out = value;
        return PYX_OK;
    }
    rt_sb_init(&sb, arena);
    for (i = 0; i < value.len; i++) {
        rt_sb_add_cp(&sb, Token_in_ALL(value.cp[i]) ? (uint32_t)'_' : value.cp[i]);
    }
    return rt_sb_finish(&sb, out);
}

/* py: calc/token.py Token.__repr__ @1da31482 */
/* py: calc/variable.py Variable.__repr__ @92f7fda7 */
py_exc Token_repr(rt_arena *arena, const Token *self, rt_str *out)
{
    rt_str text;
    rt_sb sb;
    PY_TRY(Token_to_string(arena, self, &text));
    rt_sb_init(&sb, arena);
    rt_sb_add_ascii(&sb, Token_get_desc(self));
    rt_sb_add_cp(&sb, '[');
    rt_sb_add_str(&sb, text);
    if (self->cls == CLS_Variable && self->psuedo != PSUEDO_NONE) {
        rt_sb_add_cp(&sb, ':');
        rt_sb_add_ascii(&sb, self->psuedo == PSUEDO_LAST      ? "last"
                             : self->psuedo == PSUEDO_AVERAGE ? "average"
                                                              : "sum");
    }
    rt_sb_add_cp(&sb, ']');
    return rt_sb_finish(&sb, out);
}

/* py: calc/token.py Token.list_to_string @0915ac9c */
py_exc Token_list_to_string(rt_arena *arena, const Token *self, rt_str *out)
{
    /* Each to_string() works in scratch (formatting a number takes a few hundred bytes of
     * temporaries), emptied as it fills, so a list of 10,000 tokens does not keep them all in
     * the caller's arena until the line ends; only the joined text goes to arena. */
    const Token *x;
    rt_str text;
    rt_sb sb;
    rt_arena scratch;
    py_exc e = PYX_OK;
    rt_arena_init(&scratch);
    rt_arena_set_limit(&scratch, arena->limit);
    rt_sb_init(&sb, arena);
    for (x = self; e == PYX_OK && x != NULL; x = x->next) {
        e = Token_to_string(&scratch, x, &text);
        if (e == PYX_OK) {
            if (x != self) {
                rt_sb_add_cp(&sb, ' ');
            }
            rt_sb_add_str(&sb, text);
        }
        if (scratch.bytes > TOKEN_LIST_SCRATCH_MAX) {
            rt_arena_reset(&scratch);
        }
    }
    rt_arena_destroy(&scratch);
    PY_TRY(e);
    return rt_sb_finish(&sb, out);
}

/* C only: dispatch of clone() (AttributeError for a class without one, as in Python) */
py_exc Token_clone(rt_arena *arena, const Token *self, Token **out)
{
    TokenCloneFn fn = TOKEN_CLASSES[self->cls].clone;
    if (fn == NULL) {
        return PYX_ATTRIBUTE_ERROR;
    }
    return fn(arena, self, out);
}

/* C only: dispatch of can_handle() */
bool Token_can_handle(Token *self, Calc *engine, Step other)
{
    TokenCanHandleFn fn = TOKEN_CLASSES[self->cls].can_handle;
    if (fn == NULL) {
        return false;
    }
    return fn(self, engine, other);
}

/* C only: dispatch of handle() */
py_exc Token_handle(Token *self, Calc *engine, HandleResult *out)
{
    TokenHandleFn fn = TOKEN_CLASSES[self->cls].handle;
    if (fn == NULL) {
        return PYX_ATTRIBUTE_ERROR;
    }
    return fn(self, engine, out);
}
