#!/usr/bin/env powershell
# Install Python 3.8 for x64 and x86 in order to build wheels on Windows.

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

# Avoid "Could not create SSL/TLS secure channel"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

function Install-Python {
    Param(
        [string]$PythonVersion,
        [string]$PythonInstaller,
        [string]$PythonInstallPath,
        [string]$PythonInstallerHash
    )
    $PythonInstallerUrl = "https://www.python.org/ftp/python/$PythonVersion/$PythonInstaller.exe"
    $PythonInstallerPath = "C:\tools\$PythonInstaller.exe"

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
    & $PythonInstallerPath /passive InstallAllUsers=1 PrependPath=1 Include_test=0 TargetDir=$PythonInstallPath
    if (-Not $?) {
        throw "The Python installation exited with error!"
    }

    # NOTE(lidiz) Even if the install command finishes in the script, that
    # doesn't mean the Python installation is finished. If using "ps" to check
    # for running processes, you might see ongoing installers at this point.
    # So, we needs this "hack" to reliably validate that the Python binary is
    # functioning properly.

    # Wait for the installer process
    Wait-Process -Name $PythonInstaller -Timeout 300
    Write-Host "Installation process exits normally."

    # Validate Python binary
    $PythonBinary = "$PythonInstallPath\python.exe"
    & $PythonBinary -c 'print(42)'
    Write-Host "Python binary works properly."

    # Installs pip
    & $PythonBinary -m ensurepip --user

    Write-Host "Python $PythonVersion installed by $PythonInstaller at $PythonInstallPath."
}

# Python 3.8
$Python38x86Config = @{
    PythonVersion = "3.8.10"
    PythonInstaller = "python-3.8.10"
    PythonInstallPath = "C:\Python38_32bit"
    PythonInstallerHash = "b355cfc84b681ace8908ae50908e8761"
}
Install-Python @Python38x86Config

$Python38x64Config = @{
    PythonVersion = "3.8.10"
    PythonInstaller = "python-3.8.10-amd64"
    PythonInstallPath = "C:\Python38"
    PythonInstallerHash = "62cf1a12a5276b0259e8761d4cf4fe42"
}
Install-Python @Python38x64Config

# Python 3.9
$Python39x86Config = @{
    PythonVersion = "3.9.11"
    PythonInstaller = "python-3.9.11"
    PythonInstallPath = "C:\Python39_32bit"
    PythonInstallerHash = "4210652b14a030517046cdf111c09c1e"
}
Install-Python @Python39x86Config

$Python39x64Config = @{
    PythonVersion = "3.9.11"
    PythonInstaller = "python-3.9.11-amd64"
    PythonInstallPath = "C:\Python39"
    PythonInstallerHash = "fef52176a572efd48b7148f006b25801"
}
Install-Python @Python39x64Config

# Python 3.10
$Python310x86Config = @{
    PythonVersion = "3.10.3"
    PythonInstaller = "python-3.10.3"
    PythonInstallPath = "C:\Python310_32bit"
    PythonInstallerHash = "6a336cb2aca62dd05805316ab3aaf2b5"
}
Install-Python @Python310x86Config

$Python310x64Config = @{
    PythonVersion = "3.10.3"
    PythonInstaller = "python-3.10.3-amd64"
    PythonInstallPath = "C:\Python310"
    PythonInstallerHash = "9ea305690dbfd424a632b6a659347c1e"
}
Install-Python @Python310x64Config

# Python 3.11
$Python311x86Config = @{
    PythonVersion = "3.11.0"
    PythonInstaller = "python-3.11.0rc1"
    PythonInstallPath = "C:\Python311_32bit"
    PythonInstallerHash = "d2e5420e53d9e71c82b4a19763dbaa12"
}
Install-Python @Python311x86Config

$Python311x64Config = @{
    PythonVersion = "3.11.0"
    PythonInstaller = "python-3.11.0rc1-amd64"
    PythonInstallPath = "C:\Python311"
    PythonInstallerHash = "5943d8702e40a5ccd62e5a8d4c8852aa"
}
Install-Python @Python311x64Config
