# Host C vs Python parity - `dt-adfe15c9f6`

- windows: 66 (evaluation sessions of train.py (heldout_windows.jsonl))
- class agreement: 66/66
- features bit-exact: 66/66
- baseline agreement: 66/66
- compiler: gcc.exe (i686-posix-dwarf-rev0, Built by MinGW-W64 project) 5.1.0
- flags: `-std=gnu11 -O2 -Wall -Wextra -ffp-contract=off -fno-math-errno -fexcess-precision=standard -msse2 -mfpmath=sse -static`
- result: **PASS**
