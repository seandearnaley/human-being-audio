param([Parameter(Mandatory)][string]$Product)
$ErrorActionPreference = 'Stop'
$catalogue = Get-Content -Raw "$PSScriptRoot/../studio.json" | ConvertFrom-Json -AsHashtable
if (!$catalogue.products.ContainsKey($Product)) { throw "Unknown studio product: $Product" }
Set-Location -LiteralPath "$PSScriptRoot/../products/$Product"
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
if ($Product -ne 'livecut-redux') {
    $msi = Join-Path $env:RUNNER_TEMP 'LLVM-23.1.6-win64.msi'
    Invoke-WebRequest 'https://github.com/llvm/llvm-project/releases/download/llvmorg-23.1.6/LLVM-23.1.6-win64.msi' -OutFile $msi
    if ((Get-FileHash $msi).Hash -ne '81f7eb520ca698eb2d5cd8bc4b8f24f7add9ed66ed40a6f722a6e6f060737c00') {
      throw 'LLVM installer digest does not match the upstream release.'
    }
    $install = Start-Process msiexec.exe -ArgumentList "/i `"$msi`" /qn /norestart" -Wait -PassThru
    if ($install.ExitCode) { exit $install.ExitCode }
    $env:CC = 'clang'
    $env:CXX = 'clang++'
    # Use upstream LLVM's GNU-style driver with the MSVC SDK and runtime.
    $env:PATH = "${env:ProgramFiles}\LLVM\bin;$env:PATH"
}
cmake --preset release
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build --preset release
if ($LASTEXITCODE) { exit $LASTEXITCODE }
ctest --preset release
exit $LASTEXITCODE
