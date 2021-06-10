#!/usr/bin/env powershell
# Install dotnet SDK needed to build C# projects on Windows

Set-StrictMode -Version 2
$ErrorActionPreference = 'Stop'

trap {
    $ErrorActionPreference = "Continue"
    Write-Error $_
    exit 1
}

# avoid "Unknown error on a send" in Invoke-WebRequest
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$InstallScriptUrl = 'https://dot.net/v1/dotnet-install.ps1'
$InstallScriptPath = Join-Path  "$env:TEMP" 'dotnet-install.ps1'

# Download install script
Write-Host "Downloading install script: $InstallScriptUrl => $InstallScriptPath"
Invoke-WebRequest -Uri $InstallScriptUrl -OutFile $InstallScriptPath
&$InstallScriptPath -Version 2.1.504
