# Inspect and exercise the build command generation on the trusted integration checkout.
# No Visual Studio installation is required.
param([Parameter(Mandatory)][string]$Checkout)
$ErrorActionPreference = "Stop"

$script = Join-Path $Checkout "scripts/build_runtime.ps1"
$errorsFound = $null
$tree = [System.Management.Automation.Language.Parser]::ParseFile(
    (Resolve-Path $script).Path, [ref]$null, [ref]$errorsFound)
if ($errorsFound.Count) {
    foreach ($err in $errorsFound) {
        Write-Output ("line {0}: {1}" -f $err.Extent.StartLineNumber, $err.Message)
    }
    exit 1
}

# Evaluate only the production branch that constructs the commands. The launcher
# setup and Start-Process lie outside this statement.
$branch = @($tree.FindAll({
    param($node)
    $node -is [System.Management.Automation.Language.IfStatementAst] -and
        $node.Clauses[0].Item1.Extent.Text -eq '$cmake -and $ninja'
}, $true))
if ($branch.Count -ne 1) { throw "Expected one CMake/fallback build choice." }
$generate = [scriptblock]::Create($branch[0].Extent.Text)
$root = Join-Path ([IO.Path]::GetTempPath()) "Build & Runtime Check"
$src = Join-Path $root "runtime"
$out = Join-Path $src "build"
$cfg = "RelWithDebInfo"
$jobs = 2
$sdl = $vk = ""
$cmake = Join-Path $root "cmake.exe"
$ninja = Join-Path $root "ninja.exe"
. $generate
if (!$body.Contains(('"{0}" -S "{1}"' -f $cmake, $src)) -or
    !$body.Contains(('"{0}" --build "{1}" -j 2' -f $cmake, $out))) {
    throw "CMake branch lost a quoted executable or directory."
}

$cmake = $ninja = ""
. $generate
$launcher = @($body -split '\r?\n' | Where-Object { $_ -match '^cl .*/Fe:gta6rt\.exe ' })
if ($launcher.Count -ne 1) { throw "Expected exactly one direct launcher compiler command." }
# The linked rootsig translation unit and four task-queue sources accompany the
# sources declared in the canonical CMake executable target.
$cmakeText = Get-Content -Raw -LiteralPath (Join-Path $Checkout "runtime/CMakeLists.txt")
$target = [regex]::Match($cmakeText, 'add_executable\(gta6rt\s+([^)]+)\)')
if (!$target.Success) { throw "Cannot find the CMake gta6rt target." }
$expected = @(foreach ($m in [regex]::Matches($target.Groups[1].Value, '[\w/]+\.cpp')) {
    "$src\$($m.Value.Replace('/', '\'))"
})
$expected += "$src\shims\d3d12_xs_rootsig.cpp"
foreach ($task in "TaskQueue", "AsyncLib", "ThreadPool_win32", "WaitTimer_win32") {
    $expected += "$xtaskqueueDir\Source\Task\$task.cpp"
}
$actual = @([regex]::Matches($launcher[0], '"([^"]+\.cpp)"') | ForEach-Object { $_.Groups[1].Value })
if ($actual.Count -ne $expected.Count -or (Compare-Object ($actual | Sort-Object) ($expected | Sort-Object))) {
    throw "Direct cl launcher input list differs from the CMake target and its linked source dependencies."
}
Write-Output "PASS: both branches generated commands and all $($expected.Count) direct launcher sources were quoted."
