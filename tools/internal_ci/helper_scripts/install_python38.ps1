#!/usr/bin/env powershell
# Install Python 3.8 for x64 and x86 in order to build wheels on Windows.

Set-StrictMode -Version 2
$ErrorActionPreference = 'Stop'

# Avoid "Could not create SSL/TLS secure channel"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

function Install-Python {
    Param(
        [string]$PythonVersion,
        [string]$PythonInstaller,
        [string]$PythonInstallPath,
        [string]$PythonInstallerHash
    )
    $PythonInstallerUrl = "https://www.python.org/ftp/python/$PythonVersion/$PythonInstaller"
    $PythonInstallerPath = "C:\tools\$PythonInstaller"    

    # Downloads installer
    Write-Host "Downloading the Python installer: $PythonInstallerUrl => $PythonInstallerPath"
    Invoke-WebRequest -Uri $PythonInstallerUrl -OutFile $PythonInstallerPath

    # Validates checksum
    $HashFromDownload = Get-FileHash -Path $PythonInstallerPath -Algorithm MD5
    if ($HashFromDownload.Hash -ne $PythonInstallerHash) {
        throw "Invalid Python installer: failed checksum!"
    }
    Write-Host "Python installer $PythonInstallerPath validated."

    # Installs Python
    & $PythonInstallerPath /quiet InstallAllUsers=1 PrependPath=1 Include_test=0 TargetDir=$PythonInstallPath
    if (-Not $?) {
        throw "The Python installation exited with error!"
    }
    Write-Host "Python $PythonVersion installed by $PythonInstaller at $PythonInstallPath."
}

$Python38x64Config = @{
    PythonVersion = "3.8.0"
    PythonInstaller = "python-3.8.0-amd64.exe"
    PythonInstallPath = "C:\Python38"
    PythonInstallerHash = "29ea87f24c32f5e924b7d63f8a08ee8d"
}
Install-Python @Python38x64Config
$Python38x86Config = @{
    PythonVersion = "3.8.0"
    PythonInstaller = "python-3.8.0.exe"
    PythonInstallPath = "C:\Python38_32bit"
    PythonInstallerHash = "412a649d36626d33b8ca5593cf18318c"
}
Install-Python @Python38x86Config
