<#
  Hardware gate 1, steps G1-c / G1-d: READ-ONLY probe and full internal-flash backup.
  Run ONLY after explicit approval (docs/hardware_sessions.md).

  No erase, program, halt or reset command is ever sent. OpenOCD's stock
  target/stm32f4x.cfg writes two DEBUG-unit registers on attach (DBGMCU_CR |= 0x7,
  DBGMCU_APB1_FZ freezes IWDG/WWDG only while halted); they clear at power-on reset.

  Validation (any failure -> status STOP, nonzero exit, no backup, upload blocked):
    exit 2  OpenOCD failed or a required probe field is missing
    exit 3  unexpected device: DEV_ID != 0x413 (STM32F405/407) or flash size != 1024 KB
    exit 4  read protection != level 0 (RDP 0xAA) or any sector write-protected (nWRP != 0xFFF)
    exit 5  backup failed: OpenOCD error, file missing, size != 1,048,576 bytes,
            or its first vector words differ from the probe
  Exit 0 only when every check passed (and, without -ProbeOnly, the backup is valid).

  Result: evidence\hardware\gate1_<UTC>\gate1.json  {"status": "PASS"|"STOP", "backup_ok": ...}
  tools/build/upload_guard.py refuses `pio ... -t upload` unless the LATEST gate1.json has
  status PASS and backup_ok true.

  -OpenOcd / -EvidenceRoot exist only for the offline self-test (tests/hw/test_gate1_probe.ps1).
#>
param(
    [switch]$ProbeOnly,
    [string]$OpenOcd = "",
    [string]$EvidenceRoot = ""
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$pkg = Join-Path $env:USERPROFILE ".platformio\packages\tool-openocd"
if (-not $OpenOcd) { $OpenOcd = Join-Path $pkg "bin\openocd.exe" }
if (-not $EvidenceRoot) { $EvidenceRoot = Join-Path $root "evidence\hardware" }
$scripts = Join-Path $pkg "openocd\scripts"
$stamp = (Get-Date).ToUniversalTime().ToString("yyyyMMddTHHmmssZ")
$out = Join-Path $EvidenceRoot "gate1_$stamp"
New-Item -ItemType Directory -Force $out | Out-Null

$EXPECTED_DEV_ID = 0x413
$EXPECTED_FLASH_KB = 1024
$EXPECTED_BACKUP_BYTES = 1048576

$result = [ordered]@{ utc = $stamp; status = "STOP"; probe_ok = $false; backup_ok = $false; reason = "" }

function Save-Result { $result | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $out "gate1.json") }

function Stop-Gate([int]$code, [string]$why) {
    $result.status = "STOP"
    $result.exit_code = $code
    $result.reason = $why
    Save-Result
    Write-Host "STOP ($code): $why" -ForegroundColor Red
    Write-Host "Do NOT upload. Report this to the user; see docs/hardware_sessions.md (stop conditions)."
    exit $code
}

# Windows path -> Tcl path for OpenOCD: each single backslash becomes '/'.
# (In PowerShell '\' is one literal backslash; no escaping happens in single quotes.)
function ConvertTo-TclPath([string]$p) { return $p.Replace('\', '/') }

# Quote one argument for CreateProcess/CommandLineToArgvW.
function Format-Arg([string]$a) {
    if ($a -notmatch '[\s"]') { return $a }
    $a = $a -replace '(\\*)"', '$1$1\"'
    $a = $a -replace '(\\+)$', '$1$1'
    return '"' + $a + '"'
}

# Run OpenOCD capturing stdout+stderr without PowerShell's native-stderr error records.
function Invoke-OpenOcd([string[]]$ocdArgs, [string]$logFile) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $OpenOcd
    $psi.Arguments = ($ocdArgs | ForEach-Object { Format-Arg $_ }) -join ' '
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true
    try { $proc = [System.Diagnostics.Process]::Start($psi) }
    catch { Set-Content -Encoding utf8 $logFile "failed to start ${OpenOcd}: $_"; return @{ code = -1; text = "" } }
    $so = $proc.StandardOutput.ReadToEndAsync()
    $se = $proc.StandardError.ReadToEndAsync()
    if (-not $proc.WaitForExit(180000)) { $proc.Kill(); Set-Content -Encoding utf8 $logFile "timeout"; return @{ code = -2; text = "" } }
    $proc.WaitForExit()
    $text = $so.Result + $se.Result
    Set-Content -Encoding utf8 $logFile ("cmdline: " + $psi.Arguments + "`r`nexit: " + $proc.ExitCode + "`r`n" + $text)
    return @{ code = $proc.ExitCode; text = $text }
}

function Get-Word([string]$text, [string]$addr, [int]$count = 1) {
    # Match within one line only ([ \t], never \s) so a short line cannot borrow from the next.
    $m = [regex]::Match($text, "(?im)^$addr`:[ \t]+([0-9a-f \t]+?)[ \t]*\r?$")
    if (-not $m.Success) { return $null }
    $vals = @($m.Groups[1].Value.Trim() -split '[ \t]+' | Where-Object { $_ -match '^[0-9a-fA-F]+$' })
    if ($vals.Count -lt $count) { return $null }
    # Leading comma: PowerShell would otherwise unroll a 1-element array into a string,
    # and $x[0] would then be its first CHARACTER.
    return , @($vals[0..($count - 1)])
}

if (-not (Test-Path $OpenOcd)) { Stop-Gate 2 "OpenOCD not found at $OpenOcd" }
$common = @("-s", $scripts, "-f", "interface/stlink.cfg", "-c", "transport select swd", "-f", "target/stm32f4x.cfg")

# ---------------- probe ----------------
Write-Host "Probe (read-only) ..."
$p = Invoke-OpenOcd ($common + @("-c", "init", "-c", "mdw 0xE0042000", "-c", "mdh 0x1FFF7A22",
                                 "-c", "mdw 0x40023C14", "-c", "mdw 0x08000000 2", "-c", "shutdown")) (Join-Path $out "probe.log")
$result.probe_openocd_exit = $p.code
if ($p.code -ne 0) { Stop-Gate 2 "OpenOCD probe exited with code $($p.code) (see probe.log)" }

$id = Get-Word $p.text "0xe0042000"
$fs = Get-Word $p.text "0x1fff7a22"
$opt = Get-Word $p.text "0x40023c14"
$vec = Get-Word $p.text "0x08000000" 2
$missing = @()
if (-not $id)  { $missing += "DBGMCU_IDCODE" }
if (-not $fs)  { $missing += "flash size" }
if (-not $opt) { $missing += "FLASH_OPTCR" }
if (-not $vec) { $missing += "vector words" }
if ($missing.Count) { Stop-Gate 2 ("probe fields missing: " + ($missing -join ", ")) }

$idv = [Convert]::ToUInt32($id[0], 16)
$result.dev_id = "0x{0:X3}" -f ($idv -band 0xFFF)
$result.rev_id = "0x{0:X4}" -f ($idv -shr 16)
$result.flash_kb = [Convert]::ToUInt32($fs[0], 16)
$o = [Convert]::ToUInt32($opt[0], 16)
$rdp = ($o -shr 8) -band 0xFF
$nwrp = ($o -shr 16) -band 0xFFF
$result.optcr = "0x{0:X8}" -f $o
$result.rdp = "0x{0:X2}" -f $rdp
$result.rdp_level = if ($rdp -eq 0xAA) { 0 } elseif ($rdp -eq 0xCC) { 2 } else { 1 }
$result.nwrp = "0x{0:X3}" -f $nwrp
$result.current_initial_sp = $vec[0]
$result.current_reset_vector = $vec[1]
$result.flash_erased = ($vec[0] -ieq "ffffffff" -and $vec[1] -ieq "ffffffff")

if (($idv -band 0xFFF) -ne $EXPECTED_DEV_ID) { Stop-Gate 3 "unexpected DEV_ID $($result.dev_id) (expected 0x413)" }
if ($result.flash_kb -ne $EXPECTED_FLASH_KB) { Stop-Gate 3 "unexpected flash size $($result.flash_kb) KB (expected 1024)" }
if ($result.rdp_level -ne 0) { Stop-Gate 4 "read protection level $($result.rdp_level) (RDP $($result.rdp)); programming would need a mass erase" }
if ($nwrp -ne 0xFFF) { Stop-Gate 4 ("write protection active, nWRP {0} (protected sectors mask 0x{1:X3})" -f $result.nwrp, ((-bnot $nwrp) -band 0xFFF)) }
$result.probe_ok = $true

if ($ProbeOnly) {
    $result.status = "PASS"
    $result.reason = "probe only; no backup taken (upload stays blocked until a full run passes)"
    $result.exit_code = 0
    Save-Result
    $result | ConvertTo-Json
    exit 0
}

# ---------------- backup ----------------
$bin = Join-Path $out "flash_backup_$stamp.bin"
$binTcl = ConvertTo-TclPath $bin
$result.backup_tcl_path = $binTcl
Write-Host "Backing up 1 MB internal flash (read-only) ..."
$d = Invoke-OpenOcd ($common + @("-c", "init", "-c", "dump_image {$binTcl} 0x08000000 0x100000", "-c", "shutdown")) (Join-Path $out "backup.log")
$result.backup_openocd_exit = $d.code
if ($d.code -ne 0) { Stop-Gate 5 "OpenOCD dump exited with code $($d.code) (see backup.log; if it asks for a halted target, ask for approval)" }
if (-not (Test-Path $bin)) { Stop-Gate 5 "backup file was not created" }
$size = (Get-Item $bin).Length
$result.backup_bytes = $size
if ($size -ne $EXPECTED_BACKUP_BYTES) { Stop-Gate 5 "backup size $size bytes, expected $EXPECTED_BACKUP_BYTES" }
$head = [System.IO.File]::ReadAllBytes($bin)[0..7]
$w0 = "{0:x8}" -f [BitConverter]::ToUInt32($head, 0)
$w1 = "{0:x8}" -f [BitConverter]::ToUInt32($head, 4)
if ($w0 -ine $vec[0] -or $w1 -ine $vec[1]) { Stop-Gate 5 "backup vector words $w0 $w1 differ from probe $($vec[0]) $($vec[1])" }
$result.backup_file = Split-Path -Leaf $bin
$result.backup_sha256 = (Get-FileHash $bin -Algorithm SHA256).Hash
$result.backup_ok = $true
$result.status = "PASS"
$result.exit_code = 0
Save-Result
$result | ConvertTo-Json
Write-Host "PASS - written: $out"
exit 0
