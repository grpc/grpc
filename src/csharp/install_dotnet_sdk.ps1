#!/usr/bin/env powershell
# Install dotnet SDK needed to build C# projects on Windows

Set-StrictMode -Version 2
$ErrorActionPreference = 'Stop'
# Disable progress bar to avoid getting the
# '"Access is denied" 0x5 occurred while reading the console output buffer'
# error when running on kokoro (i.e. in non-interactive mode)
$global:ProgressPreference = 'SilentlyContinue'

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

# Installed versions should be kept in sync with
# templates/tools/dockerfile/csharp_dotnetcli_deps.include
&$InstallScriptPath -Version 3.1.415
&$InstallScriptPath -Version 6.0.100
