# Human Calc C port: the Linux development build (DESIGN.md 12.3). GNU make.
#
#   make                  BUILD/libhuman_calc.a, BUILD/libhuman_calc.so, BUILD/hc, BUILD/hc_test
#   make test             quick tier: vectors, every tests/{rt,engine}/test_*.c and
#                         tests/test_*.c, the runner, test-diff (when Python is available);
#                         port-status as a warning
#   make test-diff        tests/corpus/regress.txt through tools/hc_diff.py against
#                         BUILD/libhuman_calc.so, and tests/cli_env_diff.py on BUILD/hc: fails on
#                         any difference from the Python engine
#   make test-units       the unit tests only (no runner)
#   make test-full        full tier vectors (BUILD/vectors-full), unit tests, the runner
#   make test-NAME        one test, e.g. make test-str for tests/rt/test_str.c
#   make vectors          tests/vectors/*.py -> BUILD/vectors/*.jsonl (vectors-full: full tier)
#   make asan / ubsan     sanitizer builds (with -fsigned-char) in BUILD/asan, BUILD/ubsan:
#                         unit tests, the runner, the fuzz driver over the corpus
#   make ptr-check        ASan's pointer-compare / pointer-subtract checks (BUILD/ptr): the fuzz
#                         driver over the long-line corpus
#   make test-signed-char the unit tests and the runner built with -fsigned-char
#   make check-headers    each header alone with the own-code flags (twice: include guards),
#                         human_calc.h as C++11, rt.h refusing a contracting GNU-mode build
#   make check-syntax     each existing .c alone (-fsyntax-only)
#   make stubs            tools/gen_stubs.py -> BUILD/libhc_stubs.a, plus the link check
#   make stub-report      the stub functions linked into BUILD/libhuman_calc.so
#   make check-third-party  no writable data in libtommath, the wrapper units link with
#                         -Wl,--no-undefined, their imports
#   make check-imports    the library imports no banned libc symbol (DESIGN.md 12.5)
#   make namespace / namespace-check   tools/gen_namespace.py (writes src/hc_namespace.h!) /
#                         every global symbol of the library starts with hc_
#   make gen / gen-check  tools/gen_tables.py, and its --check
#   make port-status      tools/port_status.py
#   make fuzz N=20000     tests/diff_fuzz.py against BUILD/libhuman_calc.so
#   make cross            tools/cross_check.sh (zig in Docker)
#   make format-check     clang-format --dry-run (when clang-format is available)
#   make clean            removes BUILD (only when BUILD is build or under build/)
#
# Variables:
#   BUILD       output directory (default build). Concurrent work uses one each:
#               make BUILD=build/u5 test
#   HC_SRCS     the engine and runtime sources to build (default: every src/*.c, src/rt/*.c,
#               src/gen/*.c); the stub archive supplies whatever is left out
#   HC_EXCLUDE  sources to leave out of HC_SRCS (e.g. a file another unit is editing)
#   CC, CXX, PYTHON, NM, AR, OPT (default -O2 -g), EXTRA_CPPFLAGS (before the include
#               paths), EXTRA_CFLAGS, EXTRA_LDFLAGS, TEST_ARGS, RUNNER_ARGS
#
# Keep parallel builds modest (-j4 at most): the machine shares memory with other work.

BUILD ?= build
PYTHON ?= python3
NM ?= nm
CLANG_FORMAT ?= clang-format
OPT ?= -O2 -g
EXTRA_CPPFLAGS ?=
EXTRA_CFLAGS ?=
EXTRA_LDFLAGS ?=
TEST_ARGS ?=
N ?= 20000

ROOT_DIR := $(abspath ..)
ifneq ($(filter . ./ .. ../% src src/% include% tests% tools% third_party% docs% ci% python%,$(BUILD)),)
$(error BUILD=$(BUILD) would write into the source tree)
endif
comma := ,

HC_SRCS ?= $(sort $(wildcard src/*.c src/rt/*.c src/gen/*.c))
HC_EXCLUDE ?=
LIB_SRCS := $(filter-out $(HC_EXCLUDE),$(HC_SRCS))
TP_SRCS := third_party/hc_tommath.c third_party/hc_pcre2.c third_party/hc_ryu.c
LIBM_SRCS := src/rt/py_math.c src/rt/py_float.c src/rt/py_float_text.c
HEADERS := $(sort $(wildcard include/*.h src/*.h src/rt/*.h src/gen/*.h)) \
           third_party/hc_tommath.h third_party/hc_pcre2.h

TEST_SUPPORT_SRCS := $(wildcard tests/hc_test.c tests/tolerance.c)
UNIT_TEST_SRCS := $(filter-out tests/test_runner.c,\
                  $(sort $(wildcard tests/rt/test_*.c tests/engine/test_*.c tests/test_*.c)))
RUNNER_SRCS := $(wildcard tests/test_runner.c)
CLI_SRCS := $(wildcard tools/hc_cli.c)
FUZZ_SRCS := $(wildcard tests/fuzz_driver.c)
VECTOR_SCRIPTS := $(filter-out tests/vectors/vecutil.py,$(sort $(wildcard tests/vectors/*.py)))

CPPFLAGS := $(EXTRA_CPPFLAGS) -Iinclude -Isrc -Ithird_party -Ithird_party/libtommath -Ithird_party/pcre2/src \
            -Ithird_party/ryu
CSTD := -std=c11 -ffp-contract=off
WARN := -Wall -Wextra -pedantic -Werror -Wconversion -Wshadow -Wstrict-prototypes \
        -Wmissing-prototypes -Wvla -Wpointer-arith -Wcast-qual
# One object set serves the static and the shared library: position independent, hidden
# symbols, hc_* exported (HC_BUILD_DLL only marks the public API visible on ELF).
LIBFLAGS := -fPIC -fvisibility=hidden -DHC_BUILD_DLL
TESTFLAGS := -D_CRT_SECURE_NO_WARNINGS -Itests
CFLAGS_ALL = $(CSTD) $(OPT) $(WARN) $(FILE_FLAGS) $(EXTRA_CFLAGS)
LDLIBS := -lm -pthread

OBJ := $(BUILD)/obj
obj = $(patsubst %.c,$(OBJ)/%.o,$(1))
LIB_OBJS := $(call obj,$(LIB_SRCS))
TP_OBJS := $(call obj,$(TP_SRCS))
LIBM_OBJS := $(call obj,$(LIBM_SRCS))
TEST_SUPPORT_OBJS := $(call obj,$(TEST_SUPPORT_SRCS))
UNIT_TESTS := $(patsubst %.c,$(BUILD)/%,$(UNIT_TEST_SRCS))
STATIC_LIB := $(BUILD)/libhuman_calc.a
SHARED_LIB := $(BUILD)/libhuman_calc.so
STUB_DIR := $(BUILD)/stubs
STUB_LIB := $(BUILD)/libhc_stubs.a
STUB_SRCS = $(sort $(wildcard $(STUB_DIR)/*.c))
STUB_OBJS = $(patsubst $(STUB_DIR)/%.c,$(OBJ)/stubs/%.o,$(STUB_SRCS))
RUNNER := $(if $(RUNNER_SRCS),$(BUILD)/hc_test)
CLI := $(if $(CLI_SRCS),$(BUILD)/hc)
FUZZ_DRIVER := $(if $(FUZZ_SRCS),$(BUILD)/fuzz_driver)
HAVE_PYTHON := $(shell command -v $(PYTHON) 2>/dev/null)

BANNED_IMPORTS := ^(setlocale|localeconv|newlocale|uselocale|nl_langinfo|strto(d|f|ld)|atof|getenv|secure_getenv|.*printf.*|.*scanf.*|is(alnum|alpha|blank|cntrl|digit|graph|lower|print|punct|space|upper|xdigit)|to(lower|upper)|isw.*|tow.*|__ctype_.*|__.*_l)$$

.PHONY: all lib test test-units test-diff test-full vectors vectors-full asan ubsan ptr-check \
        test-signed-char \
        sanitize-run check-headers check-syntax stubs stub-report check-third-party \
        check-imports check-arg-order namespace namespace-check gen gen-check port-status fuzz cross \
        format-check clean FORCE

all: $(STATIC_LIB) $(SHARED_LIB) $(CLI) $(RUNNER)

lib: $(STATIC_LIB)

# ---- compiling -----------------------------------------------------------------------------

# Objects are rebuilt when the compiler or the flags change (BUILD/flags.txt).
FLAGS_STAMP := $(BUILD)/flags.txt
FLAGS_NOW := $(CC) $(CPPFLAGS) $(LIBFLAGS) $(CSTD) $(OPT) $(WARN) $(EXTRA_CFLAGS)
ifneq ($(MAKECMDGOALS),clean)
$(shell mkdir -p $(BUILD) && { echo '$(FLAGS_NOW)' | cmp -s - $(FLAGS_STAMP) || echo '$(FLAGS_NOW)' > $(FLAGS_STAMP); })
endif

$(OBJ)/%.o: %.c $(FLAGS_STAMP)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(LIBFLAGS) $(CFLAGS_ALL) -MMD -MP -c $< -o $@

$(TP_OBJS): WARN := -w
$(LIBM_OBJS): FILE_FLAGS := -fno-builtin
$(call obj,$(UNIT_TEST_SRCS) $(TEST_SUPPORT_SRCS) $(RUNNER_SRCS) $(CLI_SRCS) $(FUZZ_SRCS)): \
    FILE_FLAGS := $(TESTFLAGS)

$(OBJ)/stubs/%.o: $(STUB_DIR)/%.c $(FLAGS_STAMP)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(LIBFLAGS) $(CFLAGS_ALL) -MMD -MP -c $< -o $@

-include $(shell find $(OBJ) -name '*.d' 2>/dev/null)

# ---- libraries -----------------------------------------------------------------------------

$(STATIC_LIB): $(LIB_OBJS) $(TP_OBJS)
	@rm -f $@
	$(AR) rcs $@ $^

# The shared library takes the stub archive last, so it links (and loads under ctypes) while
# functions are missing; stub-report lists them. Every public function is pulled in (-u), so
# the library always exports the whole API, stubbed or real. With every function written, no
# stub is linked.
API_SED := s/^HC_API .*HC_CALL \(hc_[a-z_0-9]*\)(.*/\1/p
HC_API_SYMBOLS := $(shell sed -n '$(API_SED)' include/human_calc.h)
$(SHARED_LIB): $(LIB_OBJS) $(TP_OBJS) $(STUB_LIB)
	$(CC) -shared -Wl,--no-undefined -Wl,-z,noexecstack $(EXTRA_CFLAGS) $(EXTRA_LDFLAGS) \
	    $(addprefix -Wl$(comma)-u$(comma),$(HC_API_SYMBOLS)) \
	    -o $@ $(LIB_OBJS) $(TP_OBJS) $(STUB_LIB) $(LDLIBS)
	@n=$$(strings -a $@ | grep -c '^human_calc stub: ' || true); \
	if [ "$$n" != 0 ]; then echo "note: $@ contains $$n stub functions (make stub-report)"; fi

stub-report: $(SHARED_LIB)
	@strings -a $(SHARED_LIB) | sed -n 's/^human_calc stub: //p' | sort

# ---- stubs (DESIGN.md 14.2) ----------------------------------------------------------------

# Regenerated on every use (cheap; only changed files are rewritten), then built by a
# sub-make that sees the current file list. The archive changes only when a stub did.
$(STUB_LIB): FORCE
	@$(PYTHON) tools/gen_stubs.py --out $(STUB_DIR) --quiet
	@$(MAKE) --no-print-directory BUILD=$(BUILD) stub-archive

.PHONY: stub-archive
stub-archive: $(STUB_OBJS) $(STUB_DIR)/stubs.list
	@if [ ! -f $(STUB_LIB) ] || [ -n "$$(find $(STUB_OBJS) $(STUB_DIR)/stubs.list -newer $(STUB_LIB) | head -1)" ]; then \
	    rm -f $(STUB_LIB) && $(AR) rcs $(STUB_LIB) $(STUB_OBJS) && echo "$(STUB_LIB): $(words $(STUB_OBJS)) stubs"; fi

$(STUB_DIR)/check/link_check.c: $(STUB_LIB) ;

$(BUILD)/stub_link_check: $(STUB_LIB) $(STUB_DIR)/check/link_check.c
	$(CC) $(CPPFLAGS) $(CFLAGS_ALL) -o $@ $(STUB_DIR)/check/link_check.c $(STUB_LIB) $(LDLIBS)

stubs: $(BUILD)/stub_link_check
	@$(BUILD)/stub_link_check

# ---- programs and tests --------------------------------------------------------------------

LINK_PROGRAM = $(CC) $(EXTRA_CFLAGS) $(EXTRA_LDFLAGS) -o $@ $^ $(LDLIBS)

$(RUNNER): $(call obj,$(RUNNER_SRCS)) $(TEST_SUPPORT_OBJS) $(LIB_OBJS) $(TP_OBJS) $(STUB_LIB)
	$(LINK_PROGRAM)

$(CLI): $(call obj,$(CLI_SRCS)) $(LIB_OBJS) $(TP_OBJS) $(STUB_LIB)
	$(LINK_PROGRAM)

$(FUZZ_DRIVER): $(call obj,$(FUZZ_SRCS)) $(LIB_OBJS) $(TP_OBJS) $(STUB_LIB)
	$(LINK_PROGRAM)

$(BUILD)/tests/%: $(OBJ)/tests/%.o $(TEST_SUPPORT_OBJS) $(LIB_OBJS) $(TP_OBJS) $(STUB_LIB)
	@mkdir -p $(@D)
	$(LINK_PROGRAM)

# Runs test executables: exit 0 passes, 77 is SKIP (no vector file), anything else fails.
define run_tests
@pass=0; fail=0; skip=0; failed=""; \
for t in $(1); do \
    echo "== $$t"; \
    $$t --vectors $(2) $(TEST_ARGS); rc=$$?; \
    if [ $$rc -eq 0 ]; then pass=$$((pass+1)); \
    elif [ $$rc -eq 77 ]; then skip=$$((skip+1)); echo "SKIP $$t"; \
    else fail=$$((fail+1)); failed="$$failed $$t"; echo "FAIL $$t (exit $$rc)"; fi; \
done; \
echo "unit tests: $$pass passed, $$skip skipped, $$fail failed$$failed"; \
[ $$fail -eq 0 ]
endef

RUNNER_ARGS ?=
run_runner = $(if $(RUNNER),$(RUNNER) --root $(ROOT_DIR) $(RUNNER_ARGS),@echo "no tests/test_runner.c yet: runner skipped")

test: $(if $(HAVE_PYTHON),vectors) $(UNIT_TESTS) $(RUNNER) $(if $(HAVE_PYTHON),$(SHARED_LIB) $(CLI))
	-@$(if $(wildcard tools/port_status.py),$(PYTHON) tools/port_status.py || echo "warning: port-status reported differences",true)
	$(call run_tests,$(UNIT_TESTS),$(BUILD)/vectors)
	$(run_runner)
	@$(if $(HAVE_PYTHON),$(MAKE) --no-print-directory BUILD=$(BUILD) test-diff,echo "no $(PYTHON): test-diff skipped")

# Differential regression tests against the Python engine (DESIGN.md 13.5): every divergence
# found so far, as hc_diff.py input, and the CLI's HC_DATE / HC_ZONE_OFFSET parsing.
test-diff: $(SHARED_LIB) $(CLI)
	HC_LIB=$(abspath $(SHARED_LIB)) $(PYTHON) -B tools/hc_diff.py --only-diff < tests/corpus/regress.txt
	@echo "test-diff: tests/corpus/regress.txt has no differences"
	$(if $(CLI),$(PYTHON) -B tests/cli_env_diff.py --hc $(CLI))

test-units: $(if $(HAVE_PYTHON),vectors) $(UNIT_TESTS)
	$(call run_tests,$(UNIT_TESTS),$(BUILD)/vectors)

test-full: $(if $(HAVE_PYTHON),vectors-full) $(UNIT_TESTS) $(RUNNER)
	$(call run_tests,$(UNIT_TESTS),$(BUILD)/vectors-full)
	$(run_runner)

# test-NAME runs tests/*/test_NAME.c ("%" may not appear in a pattern rule's prerequisites,
# hence the helper).
test_exe = $(patsubst %.c,$(BUILD)/%,$(filter %/test_$(1).c,$(UNIT_TEST_SRCS)))
.SECONDEXPANSION:
test-%: $(if $(HAVE_PYTHON),vectors) $$(call test_exe,$$*)
	@$(if $(call test_exe,$*),true,echo "no test named test_$*.c" && false)
	$(call run_tests,$(call test_exe,$*),$(BUILD)/vectors)

# Each tests/vectors/<area>.py is run as: <area>.py --tier quick|full --out DIR, and writes
# DIR/<area>.jsonl (DESIGN.md 13.3).
# regex.py and unicode.py also read the test inputs and the corpus, and regex.py imports
# tools/gen_tables.py.
VECTOR_DEPS := $(wildcard tests/vectors/vecutil.py ../calc/*.py ../test_cases.txt ../README.md \
               tests/corpus/corpus.py tests/corpus/*.jsonl tools/gen_tables.py)
$(BUILD)/vectors/%.jsonl: tests/vectors/%.py $(VECTOR_DEPS)
	@mkdir -p $(@D)
	$(PYTHON) $< --tier quick --out $(@D)
$(BUILD)/vectors-full/%.jsonl: tests/vectors/%.py $(VECTOR_DEPS)
	@mkdir -p $(@D)
	$(PYTHON) $< --tier full --out $(@D)

vectors: $(patsubst tests/vectors/%.py,$(BUILD)/vectors/%.jsonl,$(VECTOR_SCRIPTS))
vectors-full: $(patsubst tests/vectors/%.py,$(BUILD)/vectors-full/%.jsonl,$(VECTOR_SCRIPTS))

# ---- sanitizers and signed char (DESIGN.md 13.6) -------------------------------------------

SAN_ASAN := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
            -fsigned-char -DHC_DEBUG_ASSERTS
SAN_UBSAN := -fsanitize=undefined -fno-sanitize-recover=all -fsigned-char -DHC_DEBUG_ASSERTS
SAN_PTR := -fsanitize=address,pointer-compare,pointer-subtract,undefined -fno-sanitize-recover=all \
           -fno-omit-frame-pointer -fsigned-char -DHC_DEBUG_ASSERTS
# regress.txt is plain text lines, which fuzz_driver also reads (its "!reset" / "!rates" / "##"
# lines run as ordinary inputs); the rates files run a few lines each, refused ones included.
SAN_CORPUS = $(sort $(wildcard tests/corpus/fuzz/* tests/corpus/long*)) $(wildcard tests/corpus/regress.txt)
SAN_RATES = $(sort $(wildcard tests/corpus/rates/*.json))

asan: $(if $(HAVE_PYTHON),vectors)
	$(MAKE) --no-print-directory BUILD=$(BUILD)/asan OPT="-O1 -g" EXTRA_CFLAGS="$(SAN_ASAN)" sanitize-run
ubsan: $(if $(HAVE_PYTHON),vectors)
	$(MAKE) --no-print-directory BUILD=$(BUILD)/ubsan OPT="-O1 -g" EXTRA_CFLAGS="$(SAN_UBSAN)" sanitize-run
# Relational comparison or subtraction of pointers into different objects (C11 6.5.6p8,
# 6.5.8p5), PCRE2's frame vector check included (py_re.c gives it slack); slow (minutes).
ptr-check:
	$(MAKE) --no-print-directory BUILD=$(BUILD)/ptr OPT="-O1 -g" EXTRA_CFLAGS="$(SAN_PTR)" $(BUILD)/ptr/fuzz_driver
	ASAN_OPTIONS=detect_invalid_pointer_pairs=2 $(BUILD)/ptr/fuzz_driver --quiet --no-restore < tests/corpus/long.jsonl
test-signed-char: $(if $(HAVE_PYTHON),vectors)
	$(MAKE) --no-print-directory BUILD=$(BUILD)/signed-char EXTRA_CFLAGS=-fsigned-char sanitize-run

sanitize-run: $(UNIT_TESTS) $(RUNNER) $(FUZZ_DRIVER) $(BUILD)/stub_link_check
	$(call run_tests,$(UNIT_TESTS),$(dir $(BUILD))vectors)
	$(run_runner)
	@$(if $(FUZZ_DRIVER),for f in $(SAN_CORPUS); do echo "== fuzz_driver < $$f"; $(FUZZ_DRIVER) < $$f > /dev/null || exit 1; done,echo "no tests/fuzz_driver.c yet: fuzz corpus skipped")
	@$(if $(FUZZ_DRIVER),for f in $(SAN_RATES); do echo "== fuzz_driver --rates $$f"; printf '%s\n' '$$1 in EUR' '5 EUR + $$2' sum | $(FUZZ_DRIVER) --quiet --rates $$f || exit 1; done,true)

# ---- checks --------------------------------------------------------------------------------

# Each header alone, included twice (include guards), with the own-code flags; human_calc.h
# also as C++11; rt.h must refuse a GCC GNU-mode build that may contract FMAs; and every
# library source compiles in the GNU mode rt.h allows (no clash with GNU builtins like finite).
check-headers:
	@mkdir -p $(BUILD)/check_headers
	@set -e; for h in $(HEADERS); do \
	    inc=$${h#include/}; inc=$${inc#src/}; inc=$${inc#third_party/}; \
	    f=$(BUILD)/check_headers/$$(echo $$h | tr '/.' '__').c; \
	    printf '#include "%s"\n#include "%s"\ntypedef int hc_check_headers_tu;\n' $$inc $$inc > $$f; \
	    $(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(EXTRA_CFLAGS) -fsyntax-only $$f || { echo "FAIL $$h"; exit 1; }; \
	done; echo "check-headers: $(words $(HEADERS)) headers compile alone"
	@printf '#include "human_calc.h"\n#include "human_calc.h"\n' > $(BUILD)/check_headers/human_calc.cpp
	$(CXX) -Iinclude -std=c++11 -Wall -Wextra -pedantic -Werror -fsyntax-only $(BUILD)/check_headers/human_calc.cpp
	@printf '#include "rt/rt.h"\n' > $(BUILD)/check_headers/gnu_mode.c
	@if $(CC) $(CPPFLAGS) -std=gnu11 -fsyntax-only $(BUILD)/check_headers/gnu_mode.c 2>/dev/null; then \
	    if $(CC) --version | grep -qi clang; then echo "check-headers: (clang: no GNU-mode guard needed)"; \
	    else echo "FAIL: rt.h accepted -std=gnu11 without HC_FP_CONTRACT_OFF"; exit 1; fi; \
	else echo "check-headers: rt.h refuses -std=gnu11 without HC_FP_CONTRACT_OFF"; fi
	@$(CC) $(CPPFLAGS) -std=gnu11 -ffp-contract=off -DHC_FP_CONTRACT_OFF -fsyntax-only $(BUILD)/check_headers/gnu_mode.c
	@set -e; for f in $(LIB_SRCS); do \
	    $(CC) $(CPPFLAGS) -std=gnu11 -ffp-contract=off -DHC_FP_CONTRACT_OFF $(WARN) $(EXTRA_CFLAGS) \
	        $(if $(filter $(LIBM_SRCS),$(f)),-fno-builtin) -fsyntax-only $$f || { echo "FAIL $$f (-std=gnu11)"; exit 1; }; \
	done; echo "check-headers: $(words $(LIB_SRCS)) library sources compile in GNU mode (-std=gnu11, HC_FP_CONTRACT_OFF)"

SYNTAX_SRCS = $(LIB_SRCS) $(UNIT_TEST_SRCS) $(TEST_SUPPORT_SRCS) $(RUNNER_SRCS) $(CLI_SRCS) $(FUZZ_SRCS)
syntax_flags = $(if $(filter src/%,$(1)),,$(TESTFLAGS)) $(if $(filter $(LIBM_SRCS),$(1)),-fno-builtin)
check-syntax:
	@$(foreach f,$(SYNTAX_SRCS),$(CC) $(CPPFLAGS) $(CSTD) $(WARN) $(call syntax_flags,$(f)) $(EXTRA_CFLAGS) -fsyntax-only $(f) || { echo "FAIL $(f)"; exit 1; };) \
	echo "check-syntax: $(words $(SYNTAX_SRCS)) files"

# libtommath has no writable data (MP_FIXED_CUTOFFS); the three wrapper units link into a
# shared library with -Wl,--no-undefined (libc alone); their imports are listed.
check-third-party: $(TP_OBJS)
	@sect=$$(objdump -t $(OBJ)/third_party/hc_tommath.o | awk '$$3 == "O" && $$4 !~ /^\.(rodata|data\.rel\.ro)/ {print $$4, $$NF}'); \
	if [ -n "$$sect" ]; then echo "FAIL: writable data in libtommath:"; echo "$$sect"; exit 1; fi; \
	echo "check-third-party: libtommath has no writable data"
	$(CC) -shared -Wl,--no-undefined -o $(BUILD)/libthird_party_check.so $(TP_OBJS)
	@mkdir -p $(BUILD)/third_party_O0
	@for f in $(TP_SRCS); do $(CC) $(CPPFLAGS) $(CSTD) -O0 -fPIC -w -c $$f -o $(BUILD)/third_party_O0/$$(basename $$f .c).o || exit 1; done
	$(CC) -shared -Wl,--no-undefined -o $(BUILD)/third_party_O0/libcheck.so $(BUILD)/third_party_O0/*.o
	@echo "check-third-party: the wrapper units link with -Wl,--no-undefined (OPT and -O0)"
	@for o in $(TP_OBJS); do echo "$$o imports: $$($(NM) -u $$o | awk '{print $$2}' | sort | tr '\n' ' ')"; done

# A variable passed as &x to one call and read by value in the same argument list: MSVC and gcc
# evaluate arguments in different orders
check-arg-order:
	$(PYTHON) tools/check_arg_order.py

check-imports: $(STATIC_LIB)
	@bad=$$($(NM) -u $(STATIC_LIB) | awk 'NF == 2 {print $$2}' | sed 's/@.*//' | sort -u | grep -E '$(BANNED_IMPORTS)' || true); \
	if [ -n "$$bad" ]; then echo "FAIL: banned imports:"; echo "$$bad"; \
	    for s in $$bad; do $(NM) -A -u $(STATIC_LIB) | grep -w "$$s"; done; exit 1; fi; \
	echo "check-imports: OK ($$($(NM) -u $(STATIC_LIB) | awk 'NF == 2 {print $$2}' | sort -u | wc -l) distinct undefined symbols, none banned)"

namespace: $(STATIC_LIB)
	$(PYTHON) tools/gen_namespace.py --nm $(NM) --out src/hc_namespace.h $(STATIC_LIB)
	@echo "src/hc_namespace.h rewritten: rebuild everything (make clean all) before namespace-check"

namespace-check: $(STATIC_LIB)
	$(PYTHON) tools/gen_namespace.py --nm $(NM) --check $(STATIC_LIB)

need = @test -f $(1) || { echo "$(1) does not exist yet"; exit 1; }

gen:
	$(call need,tools/gen_tables.py)
	$(PYTHON) tools/gen_tables.py
gen-check:
	$(call need,tools/gen_tables.py)
	$(PYTHON) tools/gen_tables.py --check

port-status:
	$(call need,tools/port_status.py)
	$(PYTHON) tools/port_status.py

fuzz: $(SHARED_LIB)
	$(call need,tests/diff_fuzz.py)
	HC_LIB=$(abspath $(SHARED_LIB)) $(PYTHON) tests/diff_fuzz.py -n $(N)

cross:
	tools/cross_check.sh $(BUILD)/cross

FORMAT_FILES = $(filter-out src/hc_namespace.h $(wildcard src/gen/gen_*.c),\
               $(sort $(wildcard include/*.h src/*.[ch] src/rt/*.[ch] src/gen/*.[ch] tests/*.[ch] \
               tests/*/*.[ch] tools/*.c third_party/hc_*.[ch])))
format-check:
	@if command -v $(CLANG_FORMAT) >/dev/null 2>&1; then \
	    $(CLANG_FORMAT) --dry-run --Werror $(FORMAT_FILES) && echo "format-check: $(words $(FORMAT_FILES)) files OK"; \
	else echo "format-check: $(CLANG_FORMAT) not found, skipped"; fi

clean:
	@case "$(BUILD)" in build|build/*) rm -rf $(BUILD);; *) echo "refusing to remove BUILD=$(BUILD)"; exit 1;; esac

FORCE:
