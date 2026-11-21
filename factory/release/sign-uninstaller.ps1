param([Parameter(Mandatory)][string]$Path)
$ErrorActionPreference = 'Stop'
# The Microsoft signing action installs this module and signs the other payloads.
Invoke-ArtifactSigning -Endpoint $env:AZURE_SIGNING_ENDPOINT -CodeSigningAccountName $env:AZURE_SIGNING_ACCOUNT -CertificateProfileName $env:AZURE_SIGNING_PROFILE -Files $Path -FileDigest SHA256 -TimestampRfc3161 'http://timestamp.acs.microsoft.com' -TimestampDigest SHA256
$signature = Get-AuthenticodeSignature -LiteralPath $Path
if ($signature.Status -ne 'Valid' -or !$signature.TimeStamperCertificate) {
    throw 'The embedded uninstaller must have a trusted, timestamped signature.'
}
