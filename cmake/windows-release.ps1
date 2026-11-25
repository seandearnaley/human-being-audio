param(
    [Parameter(Mandatory)][string]$Product,
    [ValidateSet('release', 'dev')][string]$Preset = 'release'
)
$ErrorActionPreference = 'Stop'
$catalogue = Get-Content -Raw "$PSScriptRoot/../studio.json" | ConvertFrom-Json -AsHashtable
if (!$catalogue.products.ContainsKey($Product)) { throw "Unknown studio product: $Product" }
Set-Location -LiteralPath "$PSScriptRoot/../products/$Product"
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'

$tempDir = if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { $env:TEMP }

# Optional ccache setup (used in CI when HBA_TOOLS_DIR is configured; also respects any ccache already on PATH).
if ($env:HBA_TOOLS_DIR) {
    $ccacheDir = Join-Path $env:HBA_TOOLS_DIR 'ccache-4.14.3-windows-x86_64'
    $ccacheExe = Join-Path $ccacheDir 'ccache.exe'
    if (!(Test-Path $ccacheExe)) {
        New-Item -ItemType Directory -Force -Path $env:HBA_TOOLS_DIR | Out-Null
        $ccacheZip = Join-Path $tempDir 'ccache-4.14.3-windows-x86_64.zip'
        Invoke-WebRequest 'https://github.com/ccache/ccache/releases/download/v4.14.3/ccache-4.14.3-windows-x86_64.zip' -OutFile $ccacheZip
        if ((Get-FileHash $ccacheZip).Hash -ne 'c845c3d65be81954c75659145a522a793a27586bf940c5dcfe18af33fa1a67dd') {
            throw 'ccache archive digest does not match the upstream release.'
        }
        Expand-Archive -Path $ccacheZip -DestinationPath $env:HBA_TOOLS_DIR -Force
    }
    $env:PATH = "$ccacheDir;$env:PATH"
}

$extraArgs = @()
if ($Product -ne 'livecut-redux') {
    $llvmBin = "${env:ProgramFiles}\LLVM\bin"
    $clangExe = Join-Path $llvmBin 'clang++.exe'
    $hasLlvm = $false
    if (Test-Path $clangExe) {
        $ver = (& $clangExe --version 2>$null | Out-String)
        $hasLlvm = $ver -match '23\.1\.6'
    }
    if (!$hasLlvm) {
        $msi = Join-Path $tempDir 'LLVM-23.1.6-win64.msi'
        Invoke-WebRequest 'https://github.com/llvm/llvm-project/releases/download/llvmorg-23.1.6/LLVM-23.1.6-win64.msi' -OutFile $msi
        if ((Get-FileHash $msi).Hash -ne '81f7eb520ca698eb2d5cd8bc4b8f24f7add9ed66ed40a6f722a6e6f060737c00') {
            throw 'LLVM installer digest does not match the upstream release.'
        }
        $install = Start-Process msiexec.exe -ArgumentList "/i `"$msi`" /qn /norestart" -Wait -PassThru
        if ($install.ExitCode) { exit $install.ExitCode }
    }
    $env:CC = 'clang'
    $env:CXX = 'clang++'
    # Use upstream LLVM's GNU-style driver with the MSVC SDK and runtime.
    $env:PATH = "$llvmBin;$env:PATH"
} elseif ($Preset -eq 'dev') {
    # Embed debug info (/Z7) so ccache can cache MSVC RelWithDebInfo object files cleanly.
    $extraArgs += '-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded'
}

cmake --preset $Preset @extraArgs
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build --preset $Preset
if ($LASTEXITCODE) { exit $LASTEXITCODE }
ctest --preset $Preset
exit $LASTEXITCODE
