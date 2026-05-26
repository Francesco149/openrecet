# _run_elevated.ps1 — spawned via Start-Process -Verb RunAs by
# ttd_capture.py.  Runs the record cycle as administrator because
# the recorder rejects non-elevated callers with Error 0x80070005.
#
# All output goes to -LogPath; the parent harness reads -StatusPath
# (a small JSON file) to discover the outcome, since Start-Process
# -Verb RunAs cannot redirect stdio across the elevation boundary.

param(
    [Parameter(Mandatory=$true)][string]$TtdExe,
    [Parameter(Mandatory=$true)][string]$OutPath,
    [Parameter(Mandatory=$true)][string]$TargetExe,
    [Parameter(Mandatory=$true)][double]$WallSec,
    [Parameter(Mandatory=$true)][string]$LogPath,
    [Parameter(Mandatory=$true)][string]$StatusPath,
    [string]$TargetCwd = ""
)

$ErrorActionPreference = "Stop"

function Append-Log {
    param([string]$line)
    Add-Content -Path $LogPath -Value $line -Encoding UTF8
}

function Write-Status {
    param([string]$status, [string]$stage = "", [int]$ttdExit = 0)
    $obj = [pscustomobject]@{
        status   = $status
        stage    = $stage
        ttd_exit = $ttdExit
    }
    $json = $obj | ConvertTo-Json -Compress
    Set-Content -Path $StatusPath -Value $json -Encoding ASCII
}

try {
    Append-Log "# elevated wrapper started: $(Get-Date -Format o)"
    Append-Log "# ttd_exe: $TtdExe"
    Append-Log "# out_path: $OutPath"
    Append-Log "# target: $TargetExe"
    Append-Log "# wall_s: $WallSec"
    Append-Log "# target_cwd: $TargetCwd"
    Append-Log ""

    $outDir = Split-Path -Parent $OutPath
    if (-not (Test-Path $outDir)) {
        New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    }

    # Start-Process insists stdout != stderr and would truncate the
    # main log on launch.  Use dedicated sibling files; we splice
    # both into the main log after the recorder finishes.
    $ttdStdout = $LogPath + ".ttd.stdout"
    $ttdStderr = $LogPath + ".ttd.stderr"
    # -WorkingDirectory of the TTD process propagates as the CWD of
    # the recorded target.  Without it, retail can't find its asset
    # bundles (lnkdatas) and bails before the first frame.
    $startArgs = @{
        FilePath               = $TtdExe
        ArgumentList           = @("-out", $OutPath, $TargetExe)
        RedirectStandardOutput = $ttdStdout
        RedirectStandardError  = $ttdStderr
        NoNewWindow            = $true
        PassThru               = $true
    }
    if ($TargetCwd -ne "") { $startArgs.WorkingDirectory = $TargetCwd }
    $ttdProc = Start-Process @startArgs

    Append-Log "# ttd pid: $($ttdProc.Id)"

    Start-Sleep -Seconds $WallSec

    Append-Log "# attempting to terminate target"
    $targetName = [System.IO.Path]::GetFileNameWithoutExtension($TargetExe)
    Stop-Process -Name $targetName -Force -ErrorAction SilentlyContinue

    Append-Log "# waiting for ttd to finalize"
    try {
        $ttdProc | Wait-Process -Timeout 180
    } catch {
        Append-Log "# wait timeout; force-killing ttd pid $($ttdProc.Id)"
        Stop-Process -Id $ttdProc.Id -Force -ErrorAction SilentlyContinue
        Write-Status -status "failed" -stage "finalize_timeout"
        exit 1
    }

    $ttdExit = $ttdProc.ExitCode
    Append-Log "# ttd exited with: $ttdExit"

    # Splice TTD's captured stdout/stderr into the main log so the
    # user has a single file to look at, then drop the temp files.
    if (Test-Path $ttdStdout) {
        Append-Log ""
        Append-Log "# === ttd stdout ==="
        Get-Content -Raw $ttdStdout | ForEach-Object { Append-Log $_ }
        Remove-Item -Force $ttdStdout
    }
    if (Test-Path $ttdStderr) {
        Append-Log ""
        Append-Log "# === ttd stderr ==="
        Get-Content -Raw $ttdStderr | ForEach-Object { Append-Log $_ }
        Remove-Item -Force $ttdStderr
    }

    Write-Status -status "ok" -ttdExit $ttdExit
    exit 0
} catch {
    Append-Log "# wrapper exception: $($_.ToString())"
    Write-Status -status "failed" -stage "wrapper_exception"
    exit 1
}
