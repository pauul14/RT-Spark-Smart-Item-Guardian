# Hardware sessions: gates, exact operations, and recovery

The board's current flash content is **unknown**. The last log on this PC (2026-11-07, COM3) came from a Lab 3 networked firmware ("int3-net3-r1"), which had recorded a hard-fault reset. Nothing below may run without the user's explicit approval for that gate. Any operation not listed needs its own approval.

## Physical connection (verified from schematic sheets 4, 11 and 12 and the vendor photo)

- **USB connector:** the **lower** of the two USB-C sockets on the right edge, labelled **"ST-Link"** in the vendor photo (schematic CN9, "USB-DBG"). It carries power, the on-board ST-LINK/V2.1 (SWD + NRST) and the virtual COM port wired to USART1. The **upper** USB-C ("USB-FS", CN4) goes to the STM32's own USB and is unused; do not use it.
- **Power switch:** the self-locking push switch on the right edge, above the USB sockets ("自锁开关"/PWR, SW1). Both USB sockets feed +5 V *through* this switch, so the board and the ST-LINK stay off until it is latched **ON** (pressed in).
- **Expected when connected:** Windows shows "STMicroelectronics STLink Virtual COM Port (COMx)" and an "ST-Link Debug" device (USB 0483:374B). If neither appears, check the switch and that the lower socket is in use.
- Use a data-capable USB-C cable that is long enough for gentle handling, and keep it slack.

---

## Gate 1: bring-up and real-data collection

| step | operation | affected resources | expected outcome | state change? |
|---|---|---|---|---|
| G1-a | **User:** plug the lower "ST-Link" USB-C into this PC, then latch the power switch ON | board power | board powers up and runs **whatever firmware is in flash** (possibly the Lab 3 build, which may try Wi-Fi/MQTT on its own) | power-up only |
| G1-b | List COM ports (`python -c "import serial.tools.list_ports as l; print([ (p.device,hex(p.vid or 0),hex(p.pid or 0)) for p in l.comports()])"`) and listen on the VCP for 20 s **without sending anything** | none | identifies COMx and the current firmware's output | no |
| G1-c+d | `powershell -NoProfile -ExecutionPolicy Bypass -File tools\hw\gate1_probe.ps1`: OpenOCD attach (no halt/reset); read IDCODE, flash size, FLASH_OPTCR, first vector words; then a **read-only dump of all 1 MB internal flash** to `evidence\hardware\gate1_*\flash_backup_*.bin` (git-ignored) with SHA-256 | debug unit only (the stock target script writes DBGMCU_CR/APB1_FZ; cleared at power-on) | exit 0 and `gate1.json` status PASS: DEV_ID 0x413, flash 1024 KB, RDP 0xAA, nWRP 0xFFF, backup exactly 1,048,576 B whose vector words match the probe | debug registers only |
| G1-e | **Flash:** `pio run -e rt_spark -t upload` → OpenOCD `program firmware.elf verify reset; shutdown`. Runs **only if** G1-c+d exited 0; `tools/build/upload_guard.py` also refuses the upload unless the latest `gate1.json` is PASS with a valid backup. | internal flash **sectors 0–5** (≈ first 256 KB) erased and programmed; MCU halted and **reset** | `** Verified OK **`; board boots Smart Item Guardian v1.0.0 with the **PLACEHOLDER** model (LCD footer says so) | **yes** |
| G1-f | First-boot effects of our firmware | RTC backup registers 0–19 cleared and claimed (magic 0x5165A001); LSE + RTC enabled **only if** the RTC was never configured; IWDG started (~8 s, stays on until reset). External W25Q flash: **read-only** (JEDEC ID; event-log writes are off by default). | `#BOOT`, `#INIT,...`, `#RTOS,scheduler_running` lines | yes (backup domain, watchdog) |
| G1-g | `tools\hw\bringup.py --wait-boot 180 --ui board`, started **in its own terminal and left running** *before* G1-e is launched from a second terminal (so the boot log is captured while the upload resets the MCU). Instructions appear on the LCD and in the terminal; answers via board keys (UP yes / DOWN no). Console commands only: all module diagnostics; LCD/matrix/buzzer/LED output checks; keys; ALS/PS cover test; lid calibration; 10 s sampling check; `golden`; `time set`. Active effects: `diag wifi` pulses the **RW007 reset** once; `diag ir` sends up to 3 NEC IR frames; IR pairing (if a remote is available) stores codes in backup registers; **microSD**: if a FAT card is inserted, creates/appends `SIGUARD/EVENTS.CSV` and `SIGUARD/CLIMATE.CSV` (never formats); **CAN** and **RS485** telemetry each switched on for ~7 s then off (CAN frames at 500 kbit/s on CN8, RS485 lines on CN7; nothing is attached, so no device receives them). | RTC time; backup registers; RW007 reset; optional card files; ~7 s of bus transmissions | `evidence\hardware\bringup_*\summary.md` | minor (listed) |
| G1-h | **User, physical** (operator at the box, agent runs the tool): `tools\hw\collect_all.py` runs S01–S04 from `data\collection_plan.json`. Before each session the LCD/terminal shows the new placement and waits for **UP**; each trial then runs on beeps (long beep = start, 5 short = stop) with **gentle, controlled** handling only (never drop or strike); press DOWN within 10 s after a trial to redo it. Then the offline pipeline (`post_capture.py`) trains, verifies parity and rebuilds - no hardware. | none beyond console commands | `data
aw\S01..S04`, `models\dt-*`, `evidence\offline\post_capture_*` | no |
| G1-i | **User:** release the power switch (OFF) when finished, or leave it on for gate 2 | power | — | power |

Session-1 time: about 15 min bring-up and 4 × ~5 min capture (6 trials × 25 s each plus prompts), plus re-placement: roughly 40–45 min of supervised time.

### Exact gate-1 command sequence (two terminals)

```powershell
# both terminals
cd "<path to>\Smart-Item-Guardian"
$py  = "$env:USERPROFILE\.platformio\penv\Scripts\python.exe"
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"

# Terminal A - G1-b: read-only port listing and passive listen (sends nothing)
& $py -c "import serial.tools.list_ports as l; print([(p.device, hex(p.vid or 0), hex(p.pid or 0)) for p in l.comports()])"

# Terminal A - G1-c+d: probe + backup. Continue ONLY if it prints PASS and exits 0.
powershell -NoProfile -ExecutionPolicy Bypass -File tools\hw\gate1_probe.ps1
if ($LASTEXITCODE -ne 0) { throw "gate 1 STOP - do not upload" }

# Terminal A - G1-g listener: start it and LEAVE IT RUNNING (it waits up to 180 s for #BOOT)
& $py tools\hw\bringup.py --wait-boot 180

# Terminal B - G1-e, launched while terminal A is waiting
& $pio run -e rt_spark -t upload        # upload_guard re-checks the latest gate1.json
```

### Gate 1 stop conditions

`gate1_probe.ps1` enforces these itself with a nonzero exit, and the upload guard then blocks flashing. Stop and ask before doing anything else if:

| exit code | condition |
|---|---|
| 2 | OpenOCD fails, or a probe field is missing (the probe cannot attach; connect-under-reset asserts NRST and needs approval) |
| 3 | DEV_ID is not 0x413, or the flash is not 1024 KB |
| 4 | RDP is not level 0 (flashing would need a mass erase), or any sector is write-protected |
| 5 | the backup fails, is not exactly 1,048,576 bytes, or its first vector words differ from the probe (e.g. `dump_image` asks for a halted target) |

The script and the guard were verified offline against a fake OpenOCD covering all of these cases (`tests/hw/test_gate1_probe.ps1`, 13/13).

---

## Gate 2: final hardware session (handheld prototype, proximity acknowledgement)

History: the first gate-2 upload (2026-11-18, `evidence\hardware\upload_gate2.log`) flashed the frozen build of model `dt-adfe15c9f6` with lid detection. The scope then changed: no enclosure, lid detection removed, AP3216C proximity used for touchless acknowledgement. The integration build is frozen as a documented **successor** (`models\frozen.json`; the original record is in `models\freeze_history\`; every ML artifact hashes identically, only `firmware.bin` changed). The model, recordings, splits and the S06 evaluation are unchanged. All confirmations were typed in the terminal (`--ui terminal`), because the key mapping was not yet verified at that point (it was verified in this session: 4/4 match).

| step | operation | affected resources | expected outcome | state change? |
|---|---|---|---|---|
| G2-a | **User:** board stays connected (lower "ST-Link" USB-C, switch ON), bare board on the table, USB cable slack | — | — | power |
| G2-b | `tools\ml\freeze.py --verify` (must print "intact"), then **flash** the successor firmware: `pio run -e rt_spark -t upload` (both guards: gate-1 record + frozen build hashes; OpenOCD program/verify/reset), then `freeze.py --verify` again | internal flash sectors 0–5, MCU reset (once) | `Verified OK`; `ver` reports `model=dt-adfe15c9f6,placeholder=0` and a build stamp listed in `models\frozen.json` | **yes** |
| G2-c | `tools\hw\gate2_run.py --port COM3` (one local runner, works offline): identity check (STOP on mismatch) → `key_check.py` (press each key by its PRINTED word, type what you pressed; no mapping change) → `prox_check.py` (raw PS with hands away / hand near, then the `prox cal` wizard; thresholds only from measured data) → network OFF → `final_test.py --ui terminal` | proximity calibration in RTC backup registers; prompt answers | `evidence\hardware\gate2_*\runner.log`, `keymap_check\verify_*.md`, `prox_check_*\result.md` | minor |
| G2-c (final test) | ~35–45 min, offline: `time set`, `golden`, a read-only snapshot of this PC's network-adapter status (statuses only), 60 handheld trials (20 per class; rough alerts acknowledged via the console), **P2 proximity**: 10 deliberate gestures on TEST alerts, a hand held near during 3 alerts (expect 0 acknowledgements), 15 s hands away (expect 0 gestures); an **injected IMU read failure for 20 s** (`fault imu on/off`, software only; operator presses the named keys), `diag all` (one RW007 reset pulse, IR frames; re-tests RTC and the CAN loop-back), SD/IR/wired status, wrap-up to ≥ 15 min | RTC time; RW007 reset once; microSD lines if a card is present; IR relay frames on alerts | `evidence\hardware\final_*\summary.md` with R1–R11 | minor |
| G2-d | Optional, separate approval: `log enable` (event log in the **last 64 KB** of the external W25Q, used only if that region is blank or already ours; `log erase CONFIRM` erases only that region) | external flash, last 64 KB of the detected size (W25Q64 found at gate 1: 0x7F0000–0x7FFFFF) | persistent event records | yes (opt-in) |

---

## Recovery procedures

| situation | action | needs approval |
|---|---|---|
| Upload fails or verification mismatches | re-run `pio run -e rt_spark -t upload` | yes (flash) |
| SWD cannot attach | check cable/switch, power-cycle with the switch; if still failing, connect under reset (`-c "reset_config srst_only srst_nogate connect_assert_srst"`) | yes (reset) |
| New firmware misbehaves or a hang is suspected | read the `#FAULT,previous` line after the watchdog reset; power-cycle with the switch | power cycle by the user |
| Restore the previous (Lab 3) firmware | `openocd -s <scripts> -f interface/stlink.cfg -c "transport select swd" -f target/stm32f4x.cfg -c "program {flash_backup_<UTC>.bin} 0x08000000 verify reset; shutdown;"` | yes (flash) |
| RTC backup registers / time wrong | `time set ...`; `prox cal` again | no (console) |
| Serial port busy | close other monitors (PlatformIO monitor, the tools) | no |

None of the tools here erase the whole chip, change option bytes, or touch the ST-LINK firmware.
