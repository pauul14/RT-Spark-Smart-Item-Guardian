# Host C vs Python parity - `ph-fd5d5654a0`

> PLACEHOLDER model on SYNTHETIC data: proves the toolchain only.

- windows: 66 (held-out test sessions (heldout_windows.jsonl))
- class agreement: 66/66
- features bit-exact: 66/66
- baseline agreement: 66/66
- compiler: gcc.exe (i686-posix-dwarf-rev0, Built by MinGW-W64 project) 5.1.0
- flags: `-std=gnu11 -O2 -Wall -Wextra -ffp-contract=off -fno-math-errno -fexcess-precision=standard -msse2 -mfpmath=sse -static`
- result: **PASS**
