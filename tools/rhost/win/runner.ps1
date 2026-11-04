# rhost job runner — lives in the INTERACTIVE logon session (scheduled task at logon).
# Windows OpenSSH runs commands in session 0 (no desktop ⇒ no D3D device), so the
# ssh side only drops jobs here: C:\orc-rhost\queue\<id>.job.ps1 → runs it →
# <id>.out (stdout+stderr) and <id>.done (exit code).
# Up to N jobs at once, started in name (= timestamp) order:
#   queue\slots.txt     N (re-read every loop; default 8; 1 = the old FIFO behaviour)
#   queue\priority.txt  'low' → new jobs start BelowNormal (their rhost / port children
#                       inherit it): the owner's free-play session keeps the CPU
$q = 'C:\orc-rhost\queue'
New-Item -ItemType Directory -Force $q | Out-Null
Set-Content -Path "$q\runner.pid" -Value "$PID slots"
# a job left `.running` by a runner that died (reboot, crash) never finishes: fail it
Get-ChildItem $q -Filter '*.running.ps1' -ErrorAction SilentlyContinue | ForEach-Object {
    $id = $_.Name -replace '\.running\.ps1$', ''
    Add-Content (Join-Path $q "$id.out") 'runner restarted: job lost'
    Set-Content -Path (Join-Path $q "$id.done") -Value 255
    Remove-Item $_.FullName -ErrorAction SilentlyContinue
}
# results nobody collected (the ssh side died) pile up: drop those older than a day
Get-ChildItem $q -Include '*.done','*.out' -File -Recurse -ErrorAction SilentlyContinue |
    Where-Object { $_.LastWriteTime -lt (Get-Date).AddDays(-1) } | Remove-Item -ErrorAction SilentlyContinue
$live = @{}
while ($true) {
    foreach ($id in @($live.Keys)) {
        $p = $live[$id]
        if (-not $p.HasExited) { continue }
        $out = Join-Path $q "$id.out"
        if (Test-Path "$out.err") { Get-Content "$out.err" | Add-Content $out; Remove-Item "$out.err" }
        Set-Content -Path (Join-Path $q "$id.done") -Value $p.ExitCode
        Remove-Item (Join-Path $q "$id.running.ps1") -ErrorAction SilentlyContinue
        $live.Remove($id)
    }
    $n = 8
    try { $n = [int](Get-Content "$q\slots.txt" -TotalCount 1 -ErrorAction Stop) } catch {}
    if ($live.Count -lt $n) {
        $low = $false
        try { $low = ((Get-Content "$q\priority.txt" -TotalCount 1 -ErrorAction Stop).Trim() -eq 'low') } catch {}
        foreach ($j in (Get-ChildItem $q -Filter '*.job.ps1' -ErrorAction SilentlyContinue | Sort-Object Name)) {
            if ($live.Count -ge $n) { break }
            $id = $j.Name -replace '\.job\.ps1$', ''
            $run = Join-Path $q "$id.running.ps1"
            try { Move-Item $j.FullName $run -ErrorAction Stop } catch { continue }
            $out = Join-Path $q "$id.out"
            $p = Start-Process -FilePath powershell.exe -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',$run) `
                 -NoNewWindow -PassThru -RedirectStandardOutput $out -RedirectStandardError "$out.err"
            $null = $p.Handle            # hold the handle: without it ExitCode reads empty after exit
            if ($low) { try { $p.PriorityClass = 'BelowNormal' } catch {} }
            $live[$id] = $p
        }
    }
    Start-Sleep -Milliseconds 100
}
