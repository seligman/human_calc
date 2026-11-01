# Ryu (vendored, d2s only)

- Version: master at commit 4c0618b0e44f7ef027ebae05d2cc7812048f7c8f (as recorded in the
  archive's pax header).
- Source: the GitHub archive of that commit,
  https://github.com/ulfjack/ryu/archive/4c0618b0e44f7ef027ebae05d2cc7812048f7c8f.tar.gz (top
  directory `ryu-4c0618b0e44f7ef027ebae05d2cc7812048f7c8f`). The download step did not record the
  URL; it is inferred from the archive layout and its pax header.
- SHA-256 of the tarball the files were copied from:
  `5c039f2957769c5f61a8699b12d8703043b3f47c3ac6e2124c5a04afb6819f29`
- License: Apache-2.0 OR BSL-1.0. Upstream's README: "All code outside of third_party/ is
  copyrighted by Ulf Adams and contributors, and may be used freely in accordance with the
  Apache 2.0 license. Alternatively, the files in the ryu/ directory may be used freely in
  accordance with the Boost 1.0 license." Every file here is from `ryu/`, and **this project
  uses them under the Boost Software License 1.0** (`LICENSE-Boost`), which requires the notice
  in source copies only, not in binaries. `LICENSE-Apache2` is kept because the file headers
  refer to both.
- Vendored: 2026-10-31, by unit U1 of the C port (DESIGN.md 4.1).

## Files

Untouched copies, no local renames: `LICENSE-Boost`, `LICENSE-Apache2`, and in `ryu/`:
`d2s.c`, `ryu.h`, `common.h`, `digit_table.h`, `d2s_intrinsics.h`, `d2s_full_table.h` (the
complete set `d2s.c` needs; it defines only `d2s`, `d2s_buffered` and `d2s_buffered_n`, and has
no writable data). Not used: `d2fixed` (329 KB of tables; exact fixed formatting is done with
bigints), `s2d` (refuses more than 17 digits), `f2s`, `generic_128`.

## Configuration

`third_party/hc_ryu.c` defines `RYU_ONLY_64_BIT_OPS` (no `__uint128_t`, no MSVC intrinsics: one
code path everywhere; the algorithm is exact either way) and includes `ryu/ryu/d2s.c`. The
include directory `third_party/ryu` must be on the path (the sources include `"ryu/ryu.h"`).
Imports: `malloc` (used only by `d2s`, which the engine does not call) and `__assert_fail` /
the platform's assert handler (the sources use `assert`).

## Updating

Download the new commit, check the README's license statement, copy the same files, run
`make test` (tests/rt/test_vendor.c checks a few values; the float vectors check millions) and
`tools/cross_check.sh`; update this file and the checksums below.

## SHA-256 of the vendored files

```
c71d239df91726fc519c6eb72d318ec65820627232b2f796219e87dcf35d0ab4  LICENSE-Apache2
c9bff75738922193e67fa726fa225535870d2aa1059f91452c411736284ad566  LICENSE-Boost
0bbd71d26da6193e678d0776cf418f43f287c73d6fd6725353df0aadf70f2a19  ryu/common.h
d24323c7eb77d63f1e52c415212b50060b776f0883525d907d04954fcd48cf64  ryu/d2s.c
2618f6e5fae6c4443899b184efe3d08295dd267dc9f1a994c983c7caca59ebe6  ryu/d2s_full_table.h
1d05702f2edacce428223d4b43dd3095c1bd1f3ad30128ce1761d84356dddc7d  ryu/d2s_intrinsics.h
8b782573abc0b8554d74163ae6c02f0beb5c30d1e63eaebdb0ddf2c98d817e01  ryu/digit_table.h
b7feab0ba1df5e9ef3d602f386592ad152491405e49b408fb21c0e0e9e6bfb16  ryu/ryu.h
```
