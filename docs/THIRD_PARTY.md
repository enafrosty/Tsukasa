# Vendored Third-Party Source

Upstream code committed directly into this repository. When updating an entry, record the upstream version or commit SHA here in the same change.

| Component | Upstream | Version / Commit | License | Location |
| :--- | :--- | :--- | :--- | :--- |
| lwIP | `savannah.nongnu.org/projects/lwip` | `2.x` | BSD-3-Clause | `net/third_party/lwip/` |
| lwext4 | `github.com/gkostka/lwext4` | `b2d8fe025940cf4091fc40334aedd64d56034cdc` | BSD-3-Clause | `fs/third_party/lwext4/` |
| stb_truetype | `github.com/nothings/stb` | `v1.26` | Public Domain / MIT | `vanilla/include/stb_truetype.h` |
| stb_image | `github.com/nothings/stb` | `v2.30` | Public Domain / MIT | `vanilla/include/stb_image.h` |
| microui | `github.com/rxi/microui` | `897763f04497064d8520267c7e5bf0d4b14dbe1f` | MIT | `vanilla/devtools/vendor/microui/` |
| doomgeneric | `github.com/ozkl/doomgeneric` | Snapshot port | GPL-2.0 | `tsukasa-ports/ports/doom/src/` |
| Lua | `lua.org` | `5.4.7` | MIT | `tsukasa-ports/ports/lua/src/` |
| TinyCC | `github.com/TinyCC/tinycc` | `0fb54300b56512754221d80adda85ddb9815bceb` (`mob`) | LGPL-2.1 | `tsukasa-ports/ports/tcc/src/tinycc/` |

## Notes

- **TinyCC:** Upstream `.github/` workflows were removed when vendoring; its `.gitignore` is retained so local build artifacts stay untracked.
- **Limine:** Bootloader binaries (`v8.x-binary`) are fetched on demand into `.limine/` (ignored by git) during `make limine-artifacts` / `make iso-x86_64`, rather than committed to source.
- **Process Lifecycle & Supervisor (`vanilla/supervisor/`):** Services are currently managed via `spawn_ex` and liveness checks. Once copy-on-write `fork()`, `execve()`, and `SIGCHLD` land, the supervisor will transition to standard `fork`/`exec` with `SIGCHLD` + `waitpid()`.
