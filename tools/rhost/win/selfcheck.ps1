# selfcheck.ps1 — one capture piece's self-check (queued by play.ps1, run by the queue runner).
# Replays the piece's trace from its start save to the recorded end frame (plain replay: no --cut), then
# selfcheck.py compares: frame, rng, save.dat bytes, and the two state dumps (pointer slots aside).
# PASS → both dumps deleted; the saves gzipped either way (session-sync reads .gz or raw).
param([Parameter(Mandatory)][string]$Piece, [Parameter(Mandatory)][string]$Bin, [string]$Root = 'C:\orc-rhost\play')
$ErrorActionPreference = 'Stop'
$P = $Root
$game = 'C:\orc-rhost\game'
$c = "$Piece\check"
$frame = $null
$l = Select-String -Path "$Piece\rhost.log" -Pattern '^RESULT .* frame=(\d+)' | Select-Object -Last 1
if ($l) { $frame = $l.Matches[0].Groups[1].Value }
if (-not $frame) { '{"status": "no-result"}' | Set-Content "$Piece\check.json"; exit 0 }
New-Item -ItemType Directory -Force $c | Out-Null
Copy-Item "$Piece\recet.ini", "$Piece\volatile.txt" $c
if (Test-Path "$Piece\start.sav") { Copy-Item "$Piece\start.sav" "$c\save.dat" }
# machine-wide admission like rhost.py's batches: at most queue\procs.txt of ours, >= 1.5 GB free
$cap = 12; try { $cap = [int](Get-Content 'C:\orc-rhost\queue\procs.txt' -TotalCount 1 -ErrorAction Stop) } catch {}
while ($true) {
    $n = @(Get-Process rhost, openrecet -ErrorAction SilentlyContinue | Where-Object { $_.Path -like 'C:\orc-rhost\*' }).Count
    $free = (Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory
    if ($n -lt $cap -and $free -gt 1500000) { break }
    Start-Sleep -Seconds 2
}
$argv = @('--exe', "$game\recettear.unpacked.exe", '--game', $game, '--run', $c, '--trace', "$Piece\trace.jsonl",
          '--frames', $frame, '--dump-at', 'end', '--quiet')
$proc = Start-Process -FilePath "$P\bin\$Bin\rhost.exe" -ArgumentList $argv -PassThru -NoNewWindow `
     -RedirectStandardOutput "$c\stdout.txt" -RedirectStandardError "$c\stderr.txt"
$null = $proc.Handle
try { $proc.PriorityClass = 'Idle' } catch {}
$proc.WaitForExit()
$py = (Get-Command python -ErrorAction SilentlyContinue).Source
if (-not $py) { $py = 'C:\Users\headpats\AppData\Local\Programs\Python\Python312\python.exe' }
& $py "$P\selfcheck.py" $Piece
# gzip the saves (the piece keeps them for session-sync; 18.8 MB raw each)
foreach ($f in @("$Piece\start.sav", "$Piece\save.dat")) {
    if (-not (Test-Path $f)) { continue }
    $i = [IO.File]::OpenRead($f); $o = [IO.File]::Create("$f.gz.tmp")
    $z = New-Object IO.Compression.GZipStream($o, [IO.Compression.CompressionLevel]::Optimal)
    $i.CopyTo($z); $z.Close(); $o.Close(); $i.Close()
    Move-Item "$f.gz.tmp" "$f.gz" -Force; Remove-Item $f
}
Remove-Item "$c\save.dat" -ErrorAction SilentlyContinue
