$ErrorActionPreference = 'Stop'
$installer = (Resolve-Path "dist/$env:WINDOWS_FILE").Path
$plugin = "$env:CommonProgramFiles\VST3\$env:NAME.vst3"
$app = "$env:ProgramFiles\Human Being Audio\$env:NAME\$env:NAME.exe"
$pe = "$plugin\Contents\x86_64-win\$env:NAME.vst3"
function Test-Signature($path) {
    $signature = Get-AuthenticodeSignature -LiteralPath $path
    if ($signature.Status -ne 'Valid' -or !$signature.TimeStamperCertificate) {
        throw "Missing trusted, timestamped Authenticode signature: $path"
    }
    return $signature.SignerCertificate.Subject
}
function Invoke-Installer($file, $arguments) {
    $process = Start-Process -FilePath $file -ArgumentList $arguments -Wait -PassThru
    if ($process.ExitCode) { throw "Installer returned $($process.ExitCode)" }
}
$publisher = Test-Signature $installer
$uninstaller = Join-Path (Split-Path $app) 'Uninstall.exe'
Invoke-Installer $installer '/S'
try {
    foreach ($file in $pe, $app, $uninstaller) {
        if ((Test-Signature $file) -ne $publisher) { throw "Publisher mismatch: $file" }
    }
    $registration = Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\HumanBeingAudio-$env:PRODUCT"
    if ($registration.Publisher -ne $env:PUBLISHER -or $registration.DisplayVersion -ne $env:VERSION -or $registration.URLInfoAbout -ne $env:WEBSITE) {
        throw 'Installed product metadata does not match the studio catalog'
    }
    $standalone = Start-Process -FilePath $app -PassThru
    try {
        if (!$standalone.WaitForInputIdle(30000)) { throw 'Standalone did not initialize' }
        $limit = [DateTime]::UtcNow.AddSeconds(30)
        while (!$standalone.HasExited -and !$standalone.MainWindowHandle -and [DateTime]::UtcNow -lt $limit) {
            Start-Sleep -Milliseconds 100
            $standalone.Refresh()
        }
        if ($standalone.HasExited -or !$standalone.MainWindowHandle) { throw 'Standalone did not open its window' }
    } finally {
        if (!$standalone.HasExited) { $standalone.Kill(); $standalone.WaitForExit() }
    }
    python factory/release/verify_product.py $env:PRODUCT $plugin
    if ($LASTEXITCODE) { throw 'Installed VST3 identity changed' }
    & "$env:ProgramFiles\Git\bin\bash.exe" factory/release/pluginval.sh "$plugin"
    if ($LASTEXITCODE) { throw "pluginval returned $LASTEXITCODE" }
} finally {
    Invoke-Installer $uninstaller '/S'
}
if ((Test-Path $pe) -or (Test-Path $app)) { throw 'Uninstall left installed binaries behind' }
