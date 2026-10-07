# Drives one Pyroclast window through the interactive lifecycle and checks native validation:
# pause/clear/trigger/single steps, 30 odd-size resizes, three minimize/restore cycles, a running
# trigger, a normal close, and separately a start in a minimized window that is later restored.
# Passes when every launch exits 0, logs no ERROR/WARN lines (Vulkan loader notices excepted), in
# Debug reports "validation summary: 0 warnings, 0 errors", and the interactive run's log shows that
# the injected keys and resizes reached the application. Logs go to -OutDir; nothing else is written.
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [ValidateSet('d3d12', 'vulkan')][string]$Renderer = 'd3d12',
    [ValidateSet('full', 'half')][string]$SmokeResolution = 'full',
    [int]$Explosions = 100,
    [string]$OutDir = (Join-Path $PSScriptRoot '..\out\lifecycle'),
    [string]$ExtraArguments = ''
)
$ErrorActionPreference = 'Stop'
Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class LifecycleWindow {
    public delegate bool EnumProc(IntPtr hwnd, IntPtr parameter);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr parameter);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint process);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int capacity);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int w, int h, uint flags);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hwnd, int command);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint message, IntPtr wparam, IntPtr lparam);
    // Pyroclast is a console program, so the process also owns a console window; find the SDL one.
    public static IntPtr Find(uint processId) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((hwnd, parameter) => {
            uint owner;
            GetWindowThreadProcessId(hwnd, out owner);
            var title = new StringBuilder(256);
            GetWindowText(hwnd, title, title.Capacity);
            if (owner == processId && title.ToString().StartsWith("Pyroclast")) { found = hwnd; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
'@
$root = Resolve-Path (Join-Path $PSScriptRoot '..')
$exe = Join-Path $root "out\build\windows-x64\$Configuration\Pyroclast.exe"
$layers = Join-Path $root 'out\tools\vulkan\sdk\Bin'
if (Test-Path $layers) { $env:VK_ADD_LAYER_PATH = $layers }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$name = "$Renderer-$SmokeResolution-$Explosions-$Configuration"
$arguments = "--renderer=$Renderer --explosions=$Explosions --smoke-resolution=$SmokeResolution $ExtraArguments"

function Send-Key([IntPtr]$window, [int]$key, [int]$scan) {
    [void][LifecycleWindow]::PostMessage($window, 0x100, [IntPtr]$key, [IntPtr](($scan -shl 16) -bor 1))
    Start-Sleep -Milliseconds 20
    [void][LifecycleWindow]::PostMessage($window, 0x101, [IntPtr]$key, [IntPtr]((-1073741824) -bor ($scan -shl 16) -bor 1))
    Start-Sleep -Milliseconds 20
}

function Wait-Window($process) {
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        Start-Sleep -Milliseconds 100
        if ($process.HasExited) { throw "Application exited during startup (code $($process.ExitCode))." }
        $window = [LifecycleWindow]::Find([uint32]$process.Id)
    } while ($window -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $deadline)
    if ($window -eq [IntPtr]::Zero) { throw 'Application window not found.' }
    # Wait for the demo to finish initializing; its title changes once it is ready.
    Start-Sleep -Milliseconds 1000
    return $window
}

# SDL attaches posted keys to the window only while it has keyboard focus. Windows refuses
# SetForegroundWindow while another application is in active use (for example over remote desktop),
# but restoring a minimized window activates it.
function Focus-Window([IntPtr]$window) {
    for ($attempt = 0; $attempt -lt 5; ++$attempt) {
        [void][LifecycleWindow]::SetForegroundWindow($window)
        Start-Sleep -Milliseconds 200
        if ([LifecycleWindow]::GetForegroundWindow() -eq $window) { return }
        [void][LifecycleWindow]::ShowWindow($window, 6)
        Start-Sleep -Milliseconds 300
        [void][LifecycleWindow]::ShowWindow($window, 9)
        Start-Sleep -Milliseconds 500
        if ([LifecycleWindow]::GetForegroundWindow() -eq $window) { return }
    }
    throw 'The application window could not be given keyboard focus.'
}

function Close-Window([IntPtr]$window, $process) {
    [void][LifecycleWindow]::PostMessage($window, 0x10, [IntPtr]::Zero, [IntPtr]::Zero)   # WM_CLOSE
    if (-not $process.WaitForExit(15000)) { throw 'Application did not exit.' }
}

function Check-Log([string]$log, $process) {
    $text = (Get-Content -LiteralPath $log) -join "`n"
    $problems = (Get-Content -LiteralPath $log) | Where-Object { ($_ -cmatch '^(ERROR|WARN)') -and ($_ -notmatch 'Vulkan loader') }
    if ($process.ExitCode -ne 0) { throw "Exit code $($process.ExitCode). $text" }
    if ($problems) { throw "Diagnostics found:`n$($problems -join "`n")" }
    $backend = if ($Renderer -eq 'd3d12') { 'D3D12' } else { 'Vulkan' }
    if ($Configuration -eq 'Debug' -and -not $text.Contains("$backend validation summary: 0 warnings, 0 errors")) {
        throw 'Clean validation summary missing.'
    }
}

function Run-Interactive {
    $log = Join-Path $OutDir "$name-interactive.log"
    $process = Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru `
        -RedirectStandardError $log -RedirectStandardOutput (Join-Path $OutDir "$name-interactive.out")
    $handle = $process.Handle
    try {
        $window = Wait-Window $process
        [void][LifecycleWindow]::SetWindowPos($window, [IntPtr]::Zero, 100, 100, 1000, 650, 0x40)
        Focus-Window $window
        Send-Key $window 80 0x19      # P: pause
        Send-Key $window 82 0x13      # R: clear
        Send-Key $window 32 0x39      # Space: trigger
        for ($tick = 0; $tick -lt 60; ++$tick) { Send-Key $window 78 0x31 }   # N: single steps
        Start-Sleep -Milliseconds 300
        for ($iteration = 0; $iteration -lt 30; ++$iteration) {
            $width = 700 + 15 * ($iteration % 10) + ($iteration % 3)
            $height = 450 + 10 * ($iteration % 10) + ($iteration % 2)
            if (-not [LifecycleWindow]::SetWindowPos($window, [IntPtr]::Zero, 100, 100, $width, $height, 0x4)) { throw 'Resize failed.' }
            Start-Sleep -Milliseconds 50
        }
        for ($iteration = 0; $iteration -lt 3; ++$iteration) {
            [void][LifecycleWindow]::ShowWindow($window, 6)
            Start-Sleep -Milliseconds 200
            [void][LifecycleWindow]::ShowWindow($window, 9)
            Start-Sleep -Milliseconds 200
        }
        Focus-Window $window
        Send-Key $window 80 0x19      # P: resume
        Send-Key $window 32 0x39      # Space: running trigger
        Start-Sleep -Milliseconds 1500
        Close-Window $window $process
        Check-Log $log $process
        # Keys posted to a window that never received them would otherwise pass silently.
        $text = (Get-Content -LiteralPath $log) -join "`n"
        $backend = if ($Renderer -eq 'd3d12') { 'D3D12' } else { 'Vulkan' }
        foreach ($expected in 'Particle simulation: paused', 'Particle pool cleared.', 'Explosion group triggered',
            'Particle simulation: running', "$backend swapchain resized") {
            if (-not $text.Contains($expected)) { throw "The application never logged '$expected'; input or resizes did not arrive." }
        }
    } finally {
        if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
        $process.Dispose()
    }
}

function Run-MinimizedStart {
    $log = Join-Path $OutDir "$name-minimized.log"
    $process = Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Minimized -PassThru `
        -RedirectStandardError $log -RedirectStandardOutput (Join-Path $OutDir "$name-minimized.out")
    $handle = $process.Handle
    try {
        $window = Wait-Window $process
        Start-Sleep -Milliseconds 800
        [void][LifecycleWindow]::ShowWindow($window, 9)
        Start-Sleep -Milliseconds 800
        Close-Window $window $process
        Check-Log $log $process
    } finally {
        if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
        $process.Dispose()
    }
}

Run-Interactive
Run-MinimizedStart
Write-Output "PASS lifecycle $name"
