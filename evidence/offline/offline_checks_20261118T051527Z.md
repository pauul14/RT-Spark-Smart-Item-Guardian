# Offline checks 20261118T051527Z

Overall: **PASS** (no hardware was used)

| check | result | detail |
|---|---|---|
| python unit tests | PASS | Ran 10 tests OK |
| host C unit tests | PASS | 55 passed, 0 failed, compiler warnings 0 |
| exported C model parity (host) | PASS | model ph-fd5d5654a0 placeholder=True: classes 66/66, features bit-exact 66/66, baseline 66/66 |
| firmware build (pio run -e rt_spark) | PASS | FLASH 164872 B / 1 MB (15.72%); RAM 26400 B / 128 KB (20.14%); CCMRAM 48 KB / 64 KB (75.00%); warnings 0 |
| firmware footprint | PASS | model tables 55 B, ML code 1060 B, golden vectors 74160 B, FreeRTOS heap 49152 B (CCM) |
| repository hygiene | PASS | no issues |
