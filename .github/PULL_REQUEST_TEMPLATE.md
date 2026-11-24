## Summary
<!-- Describe what changed and why. -->

## Changes
- 

## Verification
Report exact commands run from a clean build and real observed output:

- [ ] `make -j$(nproc) iso-x86_64` (LLVM)
- [ ] `make -j$(nproc) TOOLCHAIN=gcc iso-x86_64` (GCC)
- [ ] `bash scripts/ci/boot-test.sh tsukasa.iso boot.log` (`BOOT TEST PASSED`)
- [ ] `make check` (`0` failed, `0` skipped)
- [ ] `make visual-test` / `make -C vanilla host` (if GUI/Vanilla changed)
- [ ] `test-storage-matrix.py` / `test-installer-e2e.py` (if storage/VFS/installer changed)

### Notes / Untested Areas
- 
