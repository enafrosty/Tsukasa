# Vendored third-party source

Upstream code committed directly into this repository. When updating an entry,
record the new commit here in the same change.

| Component | Upstream | Version / commit | Licence | Location |
|---|---|---|---|---|
| lwIP | savannah.nongnu.org/projects/lwip | 2.x | BSD-3 | `net/third_party/lwip/` |
| stb_truetype | github.com/nothings/stb | [G | public domain / MIT | `vanilla/include/stb_truetype.h` |
| stb_image | github.com/nothings/stb | [G | public domain / MIT | `vanilla/include/stb_image.h` |
| doomgeneric | [G | [G | GPL-2.0 | `tsukasa-ports/ports/doom/src/` |
| Lua | lua.org | 5.4.7 | MIT | `tsukasa-ports/ports/lua/src/` |
| TinyCC | github.com/TinyCC/tinycc | `0fb54300b56512754221d80adda85ddb9815bceb` (branch `mob`) | LGPL-2.1 | `tsukasa-ports/ports/tcc/src/tinycc/` |
| Clay | github.com/nicbarker/clay | `b25a31c1a152915cd7dd6796e6592273e5a10aac` (v0.14) | Zlib | `vanilla/layout/clay.h` |

Notes: TinyCC's upstream `.github/` was removed when vendoring. Its own
`.gitignore` is retained so its build output stays untracked.

Clay layout engine
  License: Zlib
  Upstream: https://github.com/nicbarker/clay
  Commit: b25a31c1a152915cd7dd6796e6592273e5a10aac
  Location: vanilla/layout/clay.h

