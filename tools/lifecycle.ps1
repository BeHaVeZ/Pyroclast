# Drives one Pyroclast window through the interactive lifecycle and checks native validation:
# pause/clear/trigger/single steps, a click on the terrain or a prop, effect lines, the showcase's keys (preset,
# count, fire at the crosshair, slot saturation, barrage, bombardment, slow motion, walk, a bookmark), 30 odd-size resizes, three
# minimize/restore cycles, a running trigger, a normal close, and separately a start in a minimized
# window that is later restored.
# Passes when every launch exits 0, logs no ERROR/WARN lines (Vulkan loader notices excepted), in
# Debug reports "validation summary: 0 warnings, 0 errors", and the interactive run's log shows that
# the injected keys and resizes reached the application. Logs go to -OutDir; nothing else is written.
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [ValidateSet('d3d12', 'vulkan')][string]$Renderer = 'd3d12',
    [ValidateSet('full', 'half', 'auto')][string]$SmokeResolution = 'full',
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
        Send-Key $window 76 0x26      # L: effect lines
        # A left click in the window. SDL places the button at the real cursor, which a posted message cannot
        # move (and moving it would disturb the user), so the check below only requires the click to arrive:
        # it either places an explosion or reports that nothing (before M17: no terrain) is under the cursor.
        # Unit tests cover where the ray lands.
        $centre = [IntPtr]((325 -shl 16) -bor 490)
        [void][LifecycleWindow]::PostMessage($window, 0x200, [IntPtr]::Zero, $centre)   # WM_MOUSEMOVE
        Start-Sleep -Milliseconds 20
        [void][LifecycleWindow]::PostMessage($window, 0x201, [IntPtr]1, $centre)   # WM_LBUTTONDOWN
        Start-Sleep -Milliseconds 20
        [void][LifecycleWindow]::PostMessage($window, 0x202, [IntPtr]::Zero, $centre)   # WM_LBUTTONUP
        Start-Sleep -Milliseconds 20
        for ($tick = 0; $tick -lt 60; ++$tick) { Send-Key $window 78 0x31 }   # N: single steps
        Start-Sleep -Milliseconds 300
        # The showcase, still paused. Keys aim at the crosshair, the screen centre, so they need no cursor: the
        # startup pose's crosshair meets the range (unit-tested).
        Send-Key $window 0x33 0x04    # 3: artillery-he
        Send-Key $window 0x43 0x2E    # C: count 10
        Send-Key $window 0x46 0x21    # F: fire 10
        Send-Key $window 0x43 0x2E    # C: count 50
        Send-Key $window 0x43 0x2E    # C: count 100
        Send-Key $window 0x46 0x21    # F: fire 100
        Send-Key $window 0x46 0x21    # F: fire 100 more; 210 events cannot all find one of 128 slots
        Start-Sleep -Milliseconds 300
        Send-Key $window 0x42 0x30    # B: barrage
        Send-Key $window 0x56 0x2F    # V: bombardment
        Send-Key $window 0x77 0x42    # F8: spawn regions, on through the resizes below
        Send-Key $window 0x78 0x43    # F9: wind arrow
        for ($tick = 0; $tick -lt 30; ++$tick) { Send-Key $window 78 0x31 }   # N: impacts release while stepping
        Send-Key $window 0x54 0x14    # T: 0.25x
        Send-Key $window 0x54 0x14    # T: 1x
        Send-Key $window 0x47 0x22    # G: walk
        Send-Key $window 0x47 0x22    # G: fly
        Send-Key $window 0x72 0x3D    # F3: bookmark 2, the ground view
        Send-Key $window 0x56 0x2F    # V: stop the bombardment
        Send-Key $window 82 0x13      # R: clear, stopping the barrage
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
            'Particle simulation: running', "$backend swapchain resized", 'Selected preset: artillery-he', 'Spawn count: 10',
            'Spawned 10 artillery-he around (', 'Spawn count: 100', 'Spawned 100 artillery-he around (',
            'effect slots are busy', 'Barrage started at (', 'Bombardment started at (', 'Simulation speed: 0.25x',
            'Simulation speed: 1x', 'Camera: walk', 'Camera: fly', 'Camera bookmark 2 (ground) recalled', 'Bombardment stopped after',
            'Barrage stopped after', 'spawn regions on', 'wind arrow on') {
            if (-not $text.Contains($expected)) { throw "The application never logged '$expected'; input or resizes did not arrive." }
        }
        if (-not ($text.Contains('Explosion at (') -or $text.Contains('Nothing under the cursor.') -or
                $text.Contains('No terrain under the cursor.'))) {
            throw 'The left click never reached the application.'
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
