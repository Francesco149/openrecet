# play.ps1 — the free-play capture session (docs/plans/free-play-capture.md FPC-4).
# Launched from the desktop shortcut (play.cmd; installed by `rhost.py session-install`). Retail runs in
# rhost with real input, one PIECE at a time: a save past the day the piece started on, or a game start
# after the first (Continue / New Game from the title) ends the piece (rhost --cut: exit 10 / 11). The
# next piece boots from the current save.dat and a preamble drives the title to that same start (turbo,
# ~1-2 s), then hands over. Close the game window (or Exit at the title) to end the session.
#   C:\orc-rhost\play\current.txt         the frozen rhost build (play\bin\<sha>) new sessions use
#   C:\orc-rhost\play\latest.sav          the newest save.dat: every piece starts from it
#   C:\orc-rhost\play\sessions\<s>\<NNN>\  trace.jsonl start.sav save.dat cut.json meta.json rhost.log ...
# Test mode (`rhost.py session-test`: -Root C:\orc-rhost\playtest, from the queue runner): -TestTrace F replays F
# as piece 1's "human" (--trace F --flatten), -TestArgs "--quiet --turbo" goes to every piece, -TestFrames N caps
# the later (zero-input --record) pieces; no prompts.
param([string]$Bin = '', [string]$Root = 'C:\orc-rhost\play', [string]$TestTrace = '', [string]$TestArgs = '', [string]$TestFrames = '')
$ErrorActionPreference = 'Stop'
$P = $Root
$test = [bool]($TestTrace -or $TestArgs -or $TestFrames)
$game = 'C:\orc-rhost\game'
$SAVE_BYTES = 18838832                    # save.dat = header 0xb10 + 100 banks x 0x2dfc8
if (-not $Bin) { $Bin = (Get-Content "$P\current.txt" -TotalCount 1).Trim() }
$exe = "$P\bin\$Bin\rhost.exe"
if (-not (Test-Path $exe)) { Write-Host "no rhost build at $exe (rhost.py session-install)"; exit 2 }
$one = New-Object System.Threading.Mutex($false, $(if ($test) { 'Local\orc-rhost-playtest' } else { 'Local\orc-rhost-play' }))
try { $mine = $one.WaitOne(0) } catch [System.Threading.AbandonedMutexException] { $mine = $true }
if (-not $mine) { Write-Host 'a capture session is already running (its console window)'; exit 2 }
$session = Get-Date -Format 'yyyyMMdd-HHmmss'
$S = "$P\sessions\$session"
New-Item -ItemType Directory -Force $S | Out-Null
$latest = "$P\latest.sav"
$q = 'C:\orc-rhost\queue'
$prioFile = "$q\priority.txt"
$prioOld = $null
if (Test-Path $prioFile) { $prioOld = Get-Content $prioFile -Raw }
if (-not $test) { Set-Content $prioFile 'low' }   # the agent's queued jobs start BelowNormal while we play

function Sha([string]$f) { if (Test-Path $f) { (Get-FileHash $f -Algorithm SHA256).Hash.ToLower() } else { $null } }
function Result([string]$log) {
    $r = @{}
    if (Test-Path $log) {
        $l = Select-String -Path $log -Pattern '^RESULT ' | Select-Object -Last 1
        if ($l) { foreach ($kv in ($l.Line -split ' ')) { $i = $kv.IndexOf('='); if ($i -gt 0) { $r[$kv.Substring(0, $i)] = $kv.Substring($i + 1) } } }
    }
    $r
}

Write-Host "Recettear capture session $session (rhost $Bin)"
Write-Host "  start: $(if (Test-Path $latest) { 'the newest save (' + (Get-Item $latest).LastWriteTime + ')' } else { 'no save yet: New Game' })"
Write-Host "  close the game window to stop; saves past a day and reloads restart the game in a second or two."
$n = 0; $pre = $null; $fails = 0
try {
    while ($true) {
        $n++
        $id = '{0:d3}' -f $n
        $d = "$S\$id"
        New-Item -ItemType Directory -Force $d | Out-Null
        Copy-Item "$P\recet.ini", "$P\volatile.txt" $d
        if (Test-Path $latest) { Copy-Item $latest "$d\start.sav"; Copy-Item $latest "$d\save.dat" }
        $argv = @('--exe', "$game\recettear.unpacked.exe", '--game', $game, '--run', $d, '--cut', '--dump-at', 'end')
        if ($TestTrace -and $n -eq 1) { $argv += @('--trace', $TestTrace, '--flatten', 'trace.jsonl') }
        else { $argv += @('--record', 'trace.jsonl'); if ($TestFrames) { $argv += @('--frames', $TestFrames) } }
        if ($pre) { $argv += @('--preamble', $pre) }
        if ($TestArgs) { $argv += ($TestArgs -split ' ' | Where-Object { $_ }) }
        $t0 = Get-Date
        Write-Host ("[{0}] {1:HH:mm:ss} {2}" -f $id, $t0, $(if ($pre) { "resume $pre" } else { 'title' }))
        $proc = Start-Process -FilePath $exe -ArgumentList $argv -PassThru -NoNewWindow `
             -RedirectStandardOutput "$d\stdout.txt" -RedirectStandardError "$d\stderr.txt"
        $null = $proc.Handle                  # hold the handle: ExitCode reads empty after exit without it
        try { $proc.PriorityClass = 'AboveNormal' } catch {}
        $proc.WaitForExit()
        $code = $proc.ExitCode
        $t1 = Get-Date
        $res = Result "$d\rhost.log"
        $cut = $null
        if (Test-Path "$d\cut.json") { $cut = Get-Content "$d\cut.json" -Raw | ConvertFrom-Json }
        # the newest save: only a whole file (a crash mid-write must not poison the next piece)
        $adopted = $false
        if ((Test-Path "$d\save.dat") -and (Get-Item "$d\save.dat").Length -eq $SAVE_BYTES) {
            Copy-Item "$d\save.dat" "$latest.tmp" -Force; Move-Item "$latest.tmp" $latest -Force; $adopted = $true
        } elseif (Test-Path "$d\save.dat") { Write-Host "  save.dat has the wrong size: the newest save stays the previous one" }
        $meta = [ordered]@{ session = $session; piece = $id; bin = $Bin; preamble = $pre; exit = $code;
                            started = $t0.ToString('s'); ended = $t1.ToString('s'); wall_s = [int]($t1 - $t0).TotalSeconds;
                            start_sha256 = (Sha "$d\start.sav"); end_sha256 = (Sha "$d\save.dat"); end_adopted = $adopted;
                            result = $res; cut = $cut }
        $meta | ConvertTo-Json -Depth 4 | Set-Content "$d\meta.json"
        # the background self-check (the queue runner, BelowNormal): replay == recording, then compress
        if ($res.Count -gt 0) {
            $jid = (Get-Date -Format 'yyyyMMdd-HHmmss') + "-selfcheck-$session-$id"
            Set-Content "$q\$jid.tmp" "& '$P\selfcheck.ps1' -Piece '$d' -Bin '$Bin' -Root '$P'"
            Move-Item "$q\$jid.tmp" "$q\$jid.job.ps1"
        }
        $why = $res['reason']
        Write-Host ("[{0}] f{1} {2:N0}s  {3}{4}" -f $id, $res['frame'], ($t1 - $t0).TotalSeconds, $(if ($why) { $why } else { "exit $code" }),
                    $(if ($cut -and $cut.reason -eq 'cut-save') { " (day $($cut.day + 1) saved to slot $($cut.slot + 1))" } else { '' }))
        if ($code -eq 10 -or $code -eq 11) { $pre = $cut.resume; $fails = 0; continue }
        if ($code -eq 0) { break }        # window closed / Exit at the title
        # fatal (3) or a crash: a failed preamble just hands the title to you; anything else asks first
        $fails++
        $prefail = (Test-Path "$d\stdout.txt") -and (Select-String -Path "$d\stdout.txt" -Pattern 'FATAL preamble' -Quiet)
        if ($test) { break }
        if ($prefail -and $fails -lt 3) { Write-Host "  the auto-resume failed: pick your save at the title"; $pre = $null; continue }
        Get-Content "$d\stdout.txt" -Tail 5 -ErrorAction SilentlyContinue | ForEach-Object { Write-Host "  $_" }
        $ans = Read-Host "  rhost stopped (exit $code). Enter = restart from the newest save at the title, q = quit"
        if ($ans -eq 'q') { break }
        $pre = $null
    }
} finally {
    if ($test) {} elseif ($null -eq $prioOld) { Remove-Item $prioFile -ErrorAction SilentlyContinue } else { Set-Content $prioFile $prioOld.Trim() }
}
Write-Host "session ${session}: $n piece(s) in $S"
