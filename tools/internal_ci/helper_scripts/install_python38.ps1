#!/usr/bin/env powershell
# Install Python 3.8 for x64 and x86 in order to build wheels on Windows.

Set-StrictMode -Version 2

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
    & $PythonInstallerPath /quiet InstallAllUsers=1 PrependPath=1 Include_test=0 TargetDir=$PythonInstallPath
    if (-Not $?) {
        throw "The Python installation exited with error!"
    }

    # Validates Python binary
    # NOTE(lidiz) Even if the install command finishes in the script, that
    # doesn't mean the Python installation is finished. If using "ps" to check
    # for running processes, you might see ongoing installers at this point.
    # So, we needs this "hack" to reliably validate that the Python binary is
    # functioning properly.
    $ValidationStartTime = Get-Date
    $EarlyExitDDL = $ValidationStartTime.addminutes(5)
    $PythonBinary = "$PythonInstallPath\python.exe"
    While ($True) {
        $CurrentTime = Get-Date
        if ($CurrentTime -ge $EarlyExitDDL) {
            throw "Invalid Python installation! Timeout!"
        }
        & $PythonBinary -c 'print(42)'
        if ($?) {
            Write-Host "Python binary works properly."
            break
        }
        Start-Sleep -Seconds 1
    }

    # Waits until the installer process is gone
    $ValidationStartTime = Get-Date
    $EarlyExitDDL = $ValidationStartTime.addminutes(5)
    While ($True) {
        $CurrentTime = Get-Date
        if ($CurrentTime -ge $EarlyExitDDL) {
            throw "Python installation process hangs!"
        }
        $InstallProcess = Get-Process -Name $PythonInstaller
        if ($InstallProcess -eq $null) {
            Write-Host "Installation process exits normally."
            break
        }
        Start-Sleep -Seconds 1
    }

    # Installs pip
    & $PythonBinary -m ensurepip --user

    Write-Host "Python $PythonVersion installed by $PythonInstaller at $PythonInstallPath."
}

$Python38x86Config = @{
    PythonVersion = "3.8.0"
    PythonInstaller = "python-3.8.0"
    PythonInstallPath = "C:\Python38_32bit"
    PythonInstallerHash = "412a649d36626d33b8ca5593cf18318c"
}
Install-Python @Python38x86Config

$Python38x64Config = @{
    PythonVersion = "3.8.0"
    PythonInstaller = "python-3.8.0-amd64"
    PythonInstallPath = "C:\Python38"
    PythonInstallerHash = "29ea87f24c32f5e924b7d63f8a08ee8d"
}
Install-Python @Python38x64Config
