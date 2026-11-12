<#
.SYNOPSIS
  Makes a self-signed TLS certificate for the IW8 Radiant web server (Tools > Web Server Settings > HTTPS).

.DESCRIPTION
  Writes cert.pem and key.pem (RSA 2048, valid 3 years) into -Out (default: web_tls beside the editor exe). The
  certificate names localhost, 127.0.0.1, this PC's IPv4 addresses and every -Name you add (a public IP or a DNS name).
  Needs openssl.exe (Git for Windows ships one). Browsers warn once about a self-signed certificate; accept it.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\make_web_cert.ps1 -Name 198.51.100.7
#>
param(
    [string]$Out = '',
    [string[]]$Name = @()
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $Out) { $Out = Join-Path $root 'bin\release\web_tls' }
$openssl = (Get-Command openssl.exe -ErrorAction SilentlyContinue).Source
if (-not $openssl) {
    foreach ($c in "$env:ProgramFiles\Git\usr\bin\openssl.exe", "$env:ProgramFiles\Git\mingw64\bin\openssl.exe") {
        if (Test-Path $c) { $openssl = $c; break }
    }
}
if (-not $openssl) { throw 'openssl.exe not found (install Git for Windows, or put openssl on PATH)' }
New-Item -ItemType Directory -Force $Out | Out-Null

$sans = @('DNS:localhost', 'IP:127.0.0.1')
foreach ($ip in Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.IPAddress -notlike '127.*' -and $_.PrefixOrigin -ne 'WellKnown' }) {
    $sans += "IP:$($ip.IPAddress)"
}
foreach ($n in $Name) { $sans += $(if ($n -match '^\d+\.\d+\.\d+\.\d+$') { "IP:$n" } else { "DNS:$n" }) }
$cnf = Join-Path $Out 'openssl.cnf'
@"
[req]
distinguished_name = dn
x509_extensions = ext
prompt = no
[dn]
CN = IW8 Radiant web server
[ext]
subjectAltName = $($sans -join ',')
keyUsage = critical, digitalSignature, keyEncipherment
extendedKeyUsage = serverAuth
basicConstraints = critical, CA:FALSE
"@ | Set-Content -Encoding ascii $cnf
$ErrorActionPreference = 'Continue'  # openssl writes its progress to stderr
& $openssl req -x509 -newkey rsa:2048 -nodes -sha256 -days 1095 -keyout (Join-Path $Out 'key.pem') -out (Join-Path $Out 'cert.pem') -config $cnf 2>&1 | Out-Null
$code = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
if ($code -ne 0) { throw 'openssl failed' }
Remove-Item $cnf
Write-Host "Wrote $Out\cert.pem and key.pem for: $($sans -join ', ')"
