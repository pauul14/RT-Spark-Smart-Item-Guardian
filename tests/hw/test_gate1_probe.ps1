<#
  Offline self-test of tools/hw/gate1_probe.ps1 and the upload gate (no hardware).
  Uses tests/hw/fake_openocd.c (compiled with PlatformIO's MinGW) and an evidence
  directory whose path contains a space. Exit 0 only if every scenario behaves.
#>
$ErrorActionPreference = "Stop"
$proj = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$mingw = Join-Path $env:USERPROFILE ".platformio\packages\toolchain-gccmingw32\bin"
$py = Join-Path $env:USERPROFILE ".platformio\penv\Scripts\python.exe"
$work = Join-Path $env:TEMP ("sig gate1 test " + [guid]::NewGuid().ToString("N").Substring(0, 8))
New-Item -ItemType Directory -Force $work | Out-Null
$fake = Join-Path $work "fake openocd.exe"
$env:PATH = "$mingw;$env:PATH"
& (Join-Path $mingw "gcc.exe") -O1 -static -o $fake (Join-Path $proj "tests\hw\fake_openocd.c")
if ($LASTEXITCODE -ne 0) { throw "fake openocd build failed" }

$cases = @(
    @{ s = "ok";            probeOnly = $false; code = 0; status = "PASS"; uploadOk = $true },
    @{ s = "ok";            probeOnly = $true;  code = 0; status = "PASS"; uploadOk = $false },
    @{ s = "probe_exit";    probeOnly = $false; code = 2; status = "STOP"; uploadOk = $false },
    @{ s = "missing";       probeOnly = $false; code = 2; status = "STOP"; uploadOk = $false },
    @{ s = "short_vec";     probeOnly = $false; code = 2; status = "STOP"; uploadOk = $false },
    @{ s = "bad_id";        probeOnly = $false; code = 3; status = "STOP"; uploadOk = $false },
    @{ s = "bad_flash";     probeOnly = $false; code = 3; status = "STOP"; uploadOk = $false },
    @{ s = "rdp1";          probeOnly = $false; code = 4; status = "STOP"; uploadOk = $false },
    @{ s = "wrp";           probeOnly = $false; code = 4; status = "STOP"; uploadOk = $false },
    @{ s = "dump_exit";     probeOnly = $false; code = 5; status = "STOP"; uploadOk = $false },
    @{ s = "dump_short";    probeOnly = $false; code = 5; status = "STOP"; uploadOk = $false },
    @{ s = "dump_mismatch"; probeOnly = $false; code = 5; status = "STOP"; uploadOk = $false }
)
$fail = 0
$rows = @()
foreach ($c in $cases) {
    $ev = Join-Path $work ("evidence " + $c.s + $(if ($c.probeOnly) { " probeonly" } else { "" }))
    New-Item -ItemType Directory -Force $ev | Out-Null
    $env:FAKE_OCD_SCENARIO = $c.s
    $env:FAKE_OCD_ARGLOG = Join-Path $ev "args.txt"
    $argList = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $proj "tools\hw\gate1_probe.ps1"),
                 "-OpenOcd", $fake, "-EvidenceRoot", $ev)
    if ($c.probeOnly) { $argList += "-ProbeOnly" }
    $null = & powershell.exe @argList 2>&1
    $code = $LASTEXITCODE
    $json = Get-ChildItem $ev -Recurse -Filter gate1.json | Select-Object -First 1
    $g = if ($json) { Get-Content $json.FullName -Raw | ConvertFrom-Json } else { $null }
    $gate = & $py -c "import sys; sys.path.insert(0, r'$proj\tools\build'); import gate1_status as g; ok, m = g.check(sys.argv[1]); print(ok); print(m)" $ev
    $uploadOk = ($gate[0] -eq "True")
    $argsTxt = if (Test-Path $env:FAKE_OCD_ARGLOG) { Get-Content $env:FAKE_OCD_ARGLOG -Raw } else { "" }
    $ok = ($code -eq $c.code) -and $g -and ($g.status -eq $c.status) -and ($uploadOk -eq $c.uploadOk)
    $ok = $ok -and ($argsTxt -notmatch 'program|erase|reset ')                       # only read commands were sent
    $ok = $ok -and ($argsTxt -match '\[-s\]\[[^\]]+ [^\]]*scripts\]')                 # scripts path with a space kept as ONE argument
    if ($c.s -eq "ok" -and -not $c.probeOnly) {
        $ok = $ok -and ($g.backup_tcl_path -notmatch '\\') -and ($g.backup_tcl_path -match ' ') -and ($g.backup_bytes -eq 1048576)
        $ok = $ok -and ($argsTxt -match [regex]::Escape("dump_image {" + $g.backup_tcl_path + "}"))
    }
    if (-not $ok) { $fail++ }
    $rows += "{0,-14} probeOnly={1,-5} exit={2} (want {3}) status={4} upload_allowed={5} -> {6}" -f $c.s, $c.probeOnly, $code, $c.code, $(if ($g) { $g.status } else { "none" }), $uploadOk, $(if ($ok) { "OK" } else { "FAIL: " + $(if ($g) { $g.reason } else { "no json" }) })
}
# latest-record rule: a later STOP blocks upload even after an earlier PASS
$mixed = Join-Path $work "evidence mixed"
foreach ($s in @("ok", "rdp1")) {
    $env:FAKE_OCD_SCENARIO = $s
    $null = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $proj "tools\hw\gate1_probe.ps1") -OpenOcd $fake -EvidenceRoot $mixed 2>&1
    Start-Sleep -Seconds 1.1
}
$gate = & $py -c "import sys; sys.path.insert(0, r'$proj\tools\build'); import gate1_status as g; print(g.check(sys.argv[1])[0])" $mixed
$latestOk = ($gate -eq "False")
if (-not $latestOk) { $fail++ }
$rows += "latest-record rule (PASS then STOP -> blocked): " + $(if ($latestOk) { "OK" } else { "FAIL" })

Remove-Item Env:FAKE_OCD_SCENARIO, Env:FAKE_OCD_ARGLOG -ErrorAction SilentlyContinue
$rows | ForEach-Object { Write-Host $_ }
Remove-Item -Recurse -Force $work
Write-Host ("gate1_probe self-test: {0} failure(s)" -f $fail)
exit $fail
