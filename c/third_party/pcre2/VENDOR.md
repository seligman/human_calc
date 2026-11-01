# PCRE2 (vendored)

- Version: 10.49 (2026-09-28, per `ChangeLog` and `PCRE2_DATE`).
- Source: the release tarball,
  https://github.com/PCRE2Project/pcre2/releases/download/pcre2-10.49/pcre2-10.49.tar.gz (top
  directory `pcre2-10.49`, with `configure` and the `.generic` files). The download step did not
  record the URL; it is inferred from the archive layout.
- SHA-256 of the tarball the files were copied from:
  `929f0b20e62879252a15886b06c89f1edef61a363cbd5826fb041080a5e557ae`
- License: `LICENCE.md`, BSD-3-Clause WITH PCRE2-exception. A binary redistribution reproduces
  the notice in its documentation (`THIRD_PARTY_NOTICES.md` carries it); the exception only
  relaxes that further. The JIT (separately BSD-2-Clause) is neither built nor vendored.
- Vendored: 2026-10-31, by unit U1 of the C port (DESIGN.md 4.1).

## Files

`LICENCE.md` and, in `src/`, 21 sources and the 6 headers they include (`gcc -MM`):

    pcre2_auto_possess.c pcre2_chartables.c pcre2_chkdint.c pcre2_compile.c
    pcre2_compile_cgroup.c pcre2_compile_class.c pcre2_context.c pcre2_error.c
    pcre2_find_bracket.c pcre2_match.c pcre2_match_data.c pcre2_newline.c
    pcre2_pattern_info.c pcre2_script_run.c pcre2_string_utils.c pcre2_study.c
    pcre2_substring.c pcre2_tables.c pcre2_ucd.c pcre2_valid_utf.c pcre2_xclass.c
    pcre2.h pcre2_internal.h pcre2_intmodedep.h pcre2_compile.h pcre2_ucp.h pcre2_util.h

Two local renames, as NON-AUTOTOOLS-BUILD steps 2 and 3 prescribe; both copies are
byte-identical to their upstream files:

    src/pcre2.h             <- src/pcre2.h.generic
    src/pcre2_chartables.c  <- src/pcre2_chartables.c.dist

Every other file is an untouched copy. Upstream's library list (NON-AUTOTOOLS-BUILD step 4) also
has `pcre2_config.c`, `pcre2_convert.c`, `pcre2_dfa_match.c`, `pcre2_extuni.c`,
`pcre2_jit_compile.c`, `pcre2_maketables.c`, `pcre2_match_next.c`, `pcre2_ord2utf.c`,
`pcre2_serialize.c` and `pcre2_substitute.c`: not used (no JIT; `pcre2_maketables.c` is the only
code that calls locale-dependent ctype functions; the rest serve features the engine does not
call). `pcre2_valid_utf.c` is needed although UTF is never enabled: at -O0 (Debug builds, MSVC
`/Od`) `pcre2_compile` and `pcre2_match` keep their calls to `_pcre2_valid_utf_32`, which an
optimizing build removes as dead code (DESIGN.md 4.1 lists 20 sources; this is the 21st).

The sources are compiled as one translation unit by `third_party/hc_pcre2.c`, never on their own.

## Configuration

`third_party/hc_pcre2.c` defines, before the sources (no `config.h`; `HAVE_CONFIG_H` is defined
nowhere): `SUPPORT_PCRE2_32 1`, `LINK_SIZE 2`, `MATCH_LIMIT 10000000`, `MATCH_LIMIT_DEPTH
MATCH_LIMIT`, `HEAP_LIMIT 65536` (KiB), `NEWLINE_DEFAULT 2` (LF), `PARENS_NEST_LIMIT 250`,
`MAX_NAME_SIZE 128`, `MAX_NAME_COUNT 10000`, `MAX_VARLOOKBEHIND 255`, `PCRE2_EXPORT` (empty).
`SUPPORT_UNICODE` and `SUPPORT_JIT` stay undefined. `third_party/hc_pcre2.h` (included by the
wrapper and by the library's users) defines `PCRE2_CODE_UNIT_WIDTH 32` and `PCRE2_STATIC` and
includes `pcre2.h`. The engine passes its own compile and match contexts with explicit limits;
PCRE2's three default contexts (`_pcre2_default_*_context_32`, writable data) are never used.
Imports: `malloc`, `free`, `memcmp`, `memcpy`, `memmove`, `memset`, `strchr`.

## Updating

Download the new release, check `LICENCE.md`, copy the same files (redo the two renames),
review `src/config.h.generic` for new or changed settings, then `make check-third-party test`
(an undefined reference names a source to add) and `tools/cross_check.sh`; update this file and
the checksums below.

## SHA-256 of the vendored files

```
197d8a73ffee0d6b09adba2f9c677b5f5aede24edf89258a68e48248d010d811  LICENCE.md
53855156e91473a9fd905bff2c54bbe395ad479cf6a8d313a4502f85e121bcf4  src/pcre2_auto_possess.c
a0ceb94d96a2b438524d0cb85168e1ab5323c871865ddf9d524d45eb1ed131d1  src/pcre2_chartables.c
ffc49737b8303e240671ae15662cfe3f33db649bfe303607fc7d12a0f85abac8  src/pcre2_chkdint.c
01f7e90e6ccea23097911b35e33a1b6ea567887216d2695a24fcd27fbcf35223  src/pcre2_compile.c
48999c337a5ae7f36ceb4c5d8efb39b1b6784d265d3468ab8878f2b72a7ced6e  src/pcre2_compile_cgroup.c
601d14f8238a54d20528cfaf16489eaf6e9c4ae45e27c582796fb56280cd3519  src/pcre2_compile_class.c
a8a64450a3efea195b3cb26a91b5088bcb42f48dd751cac78688d4dda2b45c99  src/pcre2_compile.h
fb47ff3678401ea5d6247c24a2ccca2311bb35fddf66cf7ffa0db48de4654978  src/pcre2_context.c
b564ff9a785fa5d9d0592945dea35a668e34760f61d818daf03dea86d22bf18c  src/pcre2_error.c
9fde442db9bea0a7b406ff29e65c274ae0286536f14075a62502aef69f3cf0d8  src/pcre2_find_bracket.c
19756397bb6c402d43f74ba827c55acd26c21d078eead9ed5d49f582e4c506d8  src/pcre2.h
558dca4b23799024a69c25ed0a8c6f6823315c2352a07895d4bdf92d753dbc58  src/pcre2_internal.h
e64e88bb04168c9b2f10ed32320a7290e050e2253f09ee9f60757c8bf375f8a3  src/pcre2_intmodedep.h
aa91540f2c8e6dd203d956b0b8b5e2aea1a206be78d110a037b7105ef3fc62a9  src/pcre2_match.c
c9b6b970ed1e8a6d52635f6476274ff28682c55614b100799e482ecbcb1f1857  src/pcre2_match_data.c
bbfc7e6f1d95a2986948b7d0be5c2355d1560198d403f48fa72e0c82c7c9c920  src/pcre2_newline.c
d63e56279b9e3b9309329cc738003fac3f89c49676d26db7fc647123ce06e50d  src/pcre2_pattern_info.c
caf29e2cdc67035be5c131f2434ebe2072e08c8e6f15ca0f3e28e9795d4512f6  src/pcre2_script_run.c
5ade214c7170383b97874ead541bafe1c962e9b7f61ac10e8d87e404be76968f  src/pcre2_string_utils.c
3bfcfcce37f729252df9e23b5419a93d8542b6dfc5f47ac990d72e74ca9a2e3f  src/pcre2_study.c
76ef81b82e3d48572da556ee063dd7602d916aa62ea8be4d1d42229936669bf4  src/pcre2_substring.c
ab96e9e615b5b43bfd601b0d3d3be67a317c362706167c024f365e8af489e65d  src/pcre2_tables.c
cfa8bcb3ad316e09ebfdf3e4b07d708a9d4e53795cc7c0bf04ce5156d7695adc  src/pcre2_ucd.c
7029256f6212e729458759a925a624fe5d94a2be1f6425b4ae3816795bbf1f02  src/pcre2_ucp.h
ae763c7cc12f8b770aeb632de7ed0e8ec77163baf9316f6493be599121ec58c9  src/pcre2_util.h
fc7271e737bc7828b27bf52ade1feb0e1d29e40a9ef737d102ddb0a8091ccc3f  src/pcre2_valid_utf.c
1e8cfe268ec0a322bd6c2dc1e66fdbf515d742760b17584c43eaa7efdcb490b6  src/pcre2_xclass.c
```
