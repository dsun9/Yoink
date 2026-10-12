param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [ValidateRange(-1, 255)][int]$ProbeExitCode = -1,
    [ValidateRange(0, 10000)][int]$ProbeDelayMilliseconds = 0,
    [ValidateRange(100, 180000)][int]$ProcessTimeoutMilliseconds = 180000,
    [string]$BuildDirectory = '',
    [switch]$Performance,
    [switch]$PngRetained,
    [switch]$GifRetained,
    [switch]$ScalableRetained,
    [switch]$ScalableSharp,
    [switch]$ScalableWorkerInput,
    [ValidateSet('', 'success', 'budget', 'input', 'sharp', 'stop', 'missing', 'incomplete', 'idle', 'warmup', 'pending', 'target', 'logging', 'timeout')][string]$PerformanceControl = '',
    [string]$TestRegex = '',
    [ValidatePattern('^[a-zA-Z0-9-]+$')][string]$LogPrefix = 'task11'
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not (Test-Path -LiteralPath (Join-Path $root 'build'))) { New-Item -ItemType Directory -Path (Join-Path $root 'build') | Out-Null }
function Invoke-PointyBoundedProcess($FilePath, $Arguments, $Log, $ErrorLog, $Timeout = 180000) {
    $run = $null
    try {
        $run = Start-Process -FilePath $FilePath -ArgumentList $Arguments -WorkingDirectory $root -WindowStyle Hidden -PassThru -RedirectStandardOutput $Log -RedirectStandardError $ErrorLog
        # Windows PowerShell 5 must own the process handle before the child
        # exits, or ExitCode can be null after WaitForExit.
        $processHandle = $run.Handle
        if (-not $run.WaitForExit($Timeout)) {
            & taskkill.exe /PID $run.Id /T /F | Out-Null
            Write-Host 'FAIL process hard timeout; owned test tree terminated.'
            return 2
        }
        if ($null -eq $run.ExitCode) { throw 'Child exited without a readable exit code.' }
        return $run.ExitCode
    } finally {
        if ($run -and -not $run.HasExited) { & taskkill.exe /PID $run.Id /T /F | Out-Null }
    }
}
# Controlled launcher regression. This exits before loading or calling any
# desktop API, and uses the same bounded child-launch path as the suite.
if ($ProbeExitCode -ge 0) {
    $probe = Invoke-PointyBoundedProcess (Get-Command powershell).Source @('-NoProfile', '-NonInteractive', '-Command', "Start-Sleep -Milliseconds $ProbeDelayMilliseconds; exit $ProbeExitCode") (Join-Path $root 'build/task11-exit-probe.log') (Join-Path $root 'build/task11-exit-probe-stderr.log') $ProcessTimeoutMilliseconds
    Write-Output "Controlled child expected=$ProbeExitCode actual=$probe"
    exit $probe
}
if (-not $BuildDirectory) {
    $BuildDirectory = Join-Path $root $(if ($Configuration -eq 'Release') { 'build/windows-release' } else { 'build/windows-dev' })
}
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
$log = Join-Path $root "build/$LogPrefix-desktop.log"
$stderr = Join-Path $root "build/$LogPrefix-desktop-stderr.log"
if ($PerformanceControl) {
    if (-not $Performance) { throw 'PerformanceControl requires Performance.' }
    $probePath = Join-Path $BuildDirectory "$Configuration/pointy_performance_test.exe"
    $result = Invoke-PointyBoundedProcess $probePath @('--acceptance-control', $PerformanceControl) $log $stderr $ProcessTimeoutMilliseconds
    Get-Content -LiteralPath $log
    Get-Content -LiteralPath $stderr
    exit $result
}
if ($Performance -and $Configuration -ne 'Release') { throw 'Performance acceptance requires a Release build.' }
if ($PngRetained -and -not $Performance) { throw 'PngRetained requires Performance.' }
if ($GifRetained -and -not $Performance) { throw 'GifRetained requires Performance.' }
if ($ScalableRetained -and -not $Performance) { throw 'ScalableRetained requires Performance.' }
if ($ScalableSharp -and -not $Performance) { throw 'ScalableSharp requires Performance.' }
if ($ScalableSharp -and ($ScalableRetained -or $GifRetained -or $PngRetained)) { throw 'Choose one source diagnostic.' }
if ($ScalableWorkerInput -and -not $Performance) { throw 'ScalableWorkerInput requires Performance.' }
if ($ScalableWorkerInput -and ($ScalableSharp -or $ScalableRetained -or $GifRetained -or $PngRetained)) { throw 'Choose one source diagnostic.' }
if ($ScalableRetained -and ($GifRetained -or $PngRetained)) { throw 'Choose one source diagnostic.' }
if ($GifRetained -and $PngRetained) { throw 'Choose one source diagnostic.' }
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class PointyDesktopRecovery {
  [StructLayout(LayoutKind.Sequential)] public struct Point { public int X, Y; }
  [StructLayout(LayoutKind.Sequential)] struct Mouse { public int X,Y; public uint Data,Flags,Time; public UIntPtr Extra; }
  [StructLayout(LayoutKind.Sequential)] struct Key { public ushort VK,Scan; public uint Flags,Time; public UIntPtr Extra; }
  [StructLayout(LayoutKind.Explicit)] struct Union { [FieldOffset(0)] public Mouse Mouse; [FieldOffset(0)] public Key Key; }
  [StructLayout(LayoutKind.Sequential)] struct Input { public uint Type; public Union Data; }
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out Point point);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr window);
  [DllImport("user32.dll")] static extern bool SetCursorPos(int x,int y);
  [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr window);
  [DllImport("user32.dll")] static extern IntPtr SendMessageTimeoutW(IntPtr window,uint message,UIntPtr wparam,IntPtr lparam,uint flags,uint timeout,out UIntPtr result);
  [DllImport("user32.dll")] static extern short GetAsyncKeyState(int key);
  [DllImport("user32.dll")] static extern uint SendInput(uint count,Input[] inputs,int size);
  static readonly ushort[] keys={0x10,0x11,0x12,0x1b,0x41,0x4e,0x53,0x4f,0x43,0x56,0x58,0x5a,0x09,0x0d,0x08,0x24,0x23,0x25,0x26,0x27,0x28,0x2e};
  public static bool Released() {
    foreach(int key in new int[]{1,2,4}) if((GetAsyncKeyState(key)&0x8000)!=0) return false;
    foreach(int key in keys) if((GetAsyncKeyState(key)&0x8000)!=0) return false;
    return true;
  }
  public static bool Restore(Point point,IntPtr foreground) {
    Input[] inputs=new Input[keys.Length+1];
    inputs[0].Data.Mouse.Flags=0x0004|0x0010|0x0040;
    for(int i=0;i<keys.Length;i++) { inputs[i+1].Type=1;inputs[i+1].Data.Key.VK=keys[i];inputs[i+1].Data.Key.Flags=2; }
    bool sent=SendInput((uint)inputs.Length,inputs,Marshal.SizeOf(typeof(Input)))==inputs.Length;
    SetCursorPos(point.X,point.Y);
    if(IsWindow(foreground)&&GetForegroundWindow()!=foreground) {
      SetForegroundWindow(foreground);
      UIntPtr response;SendMessageTimeoutW(foreground,0,UIntPtr.Zero,IntPtr.Zero,2,1000,out response);
    }
    Point current;GetCursorPos(out current);
    bool cursor=current.X==point.X&&current.Y==point.Y;
    bool valid=IsWindow(foreground);
    bool focus=valid&&GetForegroundWindow()==foreground;
    Console.WriteLine("SUITE FOREGROUND original-hwnd={0} actual-hwnd={1}",foreground,GetForegroundWindow());
    Console.WriteLine("SUITE RESTORE input={0} cursor={1} original-window-valid={2} foreground={3}",sent&&Released()?"released":"FAILED",cursor?"restored":"FAILED",valid?"yes":"no",!valid?"unavailable":focus?"restored":"FAILED");
    return sent&&Released()&&cursor&&focus;
  }
}
'@
if (-not [PointyDesktopRecovery]::Released()) { throw 'Desktop input is held; no test was started.' }
$savedPoint = New-Object PointyDesktopRecovery+Point
if (-not [PointyDesktopRecovery]::GetCursorPos([ref]$savedPoint)) { throw 'Cannot record cursor position.' }
$savedForeground = [PointyDesktopRecovery]::GetForegroundWindow()
Write-Output "SUITE SAVED foreground-hwnd=$savedForeground cursor=$($savedPoint.X),$($savedPoint.Y)"
$desktopLock = New-Object System.Threading.Mutex($false, 'Local\PointyPointerDesktopAcceptance')
if (-not $desktopLock.WaitOne(0)) { $desktopLock.Dispose(); throw 'Another supervised desktop acceptance command is running.' }
$exitCode = 1
try {
    if ($Performance) {
        $probePath = Join-Path $BuildDirectory "$Configuration/pointy_performance_test.exe"
        $probeMode = if ($PngRetained) { '--png-retained' } elseif ($GifRetained) { '--gif-retained' } elseif ($ScalableRetained) { '--scalable-retained' } elseif ($ScalableSharp) { '--scalable-sharp' } elseif ($ScalableWorkerInput) { '--scalable-worker-input' } else { '--acceptance' }
        $exitCode = Invoke-PointyBoundedProcess $probePath @($probeMode) $log $stderr $ProcessTimeoutMilliseconds
    } else {
        $ctest = (Get-Command ctest).Source
        $arguments = @('--test-dir', $BuildDirectory, '-C', $Configuration, '-L', 'desktop', '--stop-on-failure', '--output-on-failure')
        if ($TestRegex) { $arguments += @('-R', $TestRegex) }
        $exitCode = Invoke-PointyBoundedProcess $ctest $arguments $log $stderr $ProcessTimeoutMilliseconds
    }
} finally {
    try {
        $restored = [PointyDesktopRecovery]::Restore($savedPoint, $savedForeground)
        if (-not $restored) { $exitCode = 3 }
        if (Test-Path -LiteralPath $log) { Get-Content -LiteralPath $log }
        if (Test-Path -LiteralPath $stderr) { Get-Content -LiteralPath $stderr }
    } finally {
        $desktopLock.ReleaseMutex()
        $desktopLock.Dispose()
    }
}
exit $exitCode
