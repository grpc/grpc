# Copyright 2026 gRPC authors.
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

$ErrorActionPreference = "Stop"

# Log messages with timestamp to allow timing command execution.
function Write-Log($Message, $Level = "Info") {
    $timestamp = (Get-Date).ToString("HH:mm:ss")
    $fullMessage = "{$timestamp}: $Message"

    switch ($Level) {
        "Error"   { Write-Error $fullMessage }
        "Warning" { Write-Warning $fullMessage }
        "Success" { Write-Host $fullMessage -ForegroundColor Green }
        Default   { Write-Host $fullMessage }
    }
}

# allow timing of how long the script takes to run.
Write-Log "prepare_build_windows.ps1 started"

# make sure msys binaries are preferred over cygwin binaries
# set path to python3.9
# set path to CMake
$newPaths = @(
    "C:\tools\msys64\usr\bin",
    "C:\Python39",
    "C:\Program Files\CMake\bin"
)
$env:Path = ($newPaths + $env:Path) -join ";"

# Print image ID of the windows kokoro image being used.
if (Test-Path "C:\image_id.txt") {
    Get-Content "C:\image_id.txt"
}

# install python 3.9
$maxRetries = 3
$success = $false

for ($i = 1; $i -le $maxRetries; $i++) {
    try {
        $process = Start-Process choco -ArgumentList "install -y --no-progress python --version=3.9.13" -Wait -PassThru
        if ($process.ExitCode -eq 0) {
            $success = $true
            break
        }
        $reason = "Exit code $($process.ExitCode)"
    } catch {
        # Catching execution error message
        $reason = $_.Exception.Message
    }

    # If we haven't succeeded and have retries left, log the retry attempt
    if ($i -lt $maxRetries) {
        Start-Sleep -Seconds 1
        Write-Log "Python installation failed with ($reason), retrying..." -Level Warning
    }
}

# If we haven't succeeded after all retries, log the failure and exit
if (-not $success) {
    Write-Log "Failed to install python after $maxRetries attempts." -Level Error
    exit 1
}

Write-Log "Successfully installed python" -Level Success

# create "python3" link that normally doesn't exist
if (-not (Test-Path "C:\Python39\python3.exe")) {
    New-Item -ItemType SymbolicLink -Path "C:\Python39\python3.exe" -Target "C:\Python39\python.exe"
}

& python --version
& python3 --version

# If this is a PR using RUN_TESTS_FLAGS var, then add flags to filter tests
if ($env:KOKORO_GITHUB_PULL_REQUEST_NUMBER -and $env:RUN_TESTS_FLAGS) {
    $env:RUN_TESTS_FLAGS += " --filter_pr_tests --base_branch origin/$($env:KOKORO_GITHUB_PULL_REQUEST_TARGET_BRANCH)"
}

# Update DNS settings to:
# 1. allow resolving metadata.google.internal hostname
# 2. make fetching default GCE credential by oauth2client work
netsh interface ip set dns "Local Area Connection 8" static 169.254.169.254 primary
netsh interface ip add dnsservers "Local Area Connection 8" 8.8.8.8 index=2
netsh interface ip add dnsservers "Local Area Connection 8" 8.8.4.4 index=3

# Uninstall protoc so that it doesn't clash with C++ distribtests.
# (on grpc-win2016 kokoro workers it can result in GOOGLE_PROTOBUF_MIN_PROTOC_VERSION violation)
choco uninstall protoc -y --limit-output

# Install nasm (required for boringssl assembly optimized build as boringssl no long supports yasm)
# Downloading from GCS should be very reliables when on a GCP VM.
if (-not (Test-Path "C:\nasm")) {
    mkdir "C:\nasm" | Out-Null
}
curl.exe -sSL --fail -o "C:\nasm\nasm.exe" https://storage.googleapis.com/grpc-build-helper/nasm-2.15.05/nasm.exe
$env:Path = "C:\nasm;" + $env:Path
& nasm

# Install ccache
if (-not (Test-Path "C:\ccache")) {
    mkdir "C:\ccache" | Out-Null
}
curl.exe -sSL --fail -o "C:\ccache\ccache.exe" https://storage.googleapis.com/grpc-build-helper/ccache-4.8-windows-64/ccache.exe
$env:Path = "C:\ccache;" + $env:Path
& ccache --version

# Only install C# dependencies if we are running C# tests
if ($env:PREPARE_BUILD_INSTALL_DEPS_CSHARP -eq "true") {
  # C# prerequisites: Install dotnet SDK
  & "src\csharp\install_dotnet_sdk.ps1"

  # Explicitly add default nuget source.
  # (on Kokoro grpc-win2016 workers, the default nuget source is not configured,
  # which results in errors when "dotnet restore" is run)
  $dotnetExe = "$env:LOCALAPPDATA\Microsoft\dotnet\dotnet.exe"
  & $dotnetExe nuget add source https://api.nuget.org/v3/index.json -n "nuget.org"
}

# Add dotnet on path and disable some unwanted dotnet
# option regardless of PREPARE_BUILD_INSTALL_DEPS_CSHARP value.
# Always setting the env vars is fine since its instantaneous,
# it can't fail and it avoids possible issues with
# "setlocal" and "EnableDelayedExpansion" which would be required if
# we wanted to do the same under the IF block.
$env:Path = "$env:LOCALAPPDATA\Microsoft\dotnet;" + $env:Path
$env:NUGET_XMLDOC_MODE = "skip"
$env:DOTNET_SKIP_FIRST_TIME_EXPERIENCE = "true"
$env:DOTNET_CLI_TELEMETRY_OPTOUT = "true"

# Workaround https://github.com/NuGet/Home/issues/11099 that exhibits
# on windows workers as "The repository primary signature's timestamping certificate is not trusted by the trust provider"
$env:NUGET_EXPERIMENTAL_CHAIN_BUILD_RETRY_POLICY = "3,1000"

# Only install Python interpreters if we are running Python tests
if ($env:PREPARE_BUILD_INSTALL_DEPS_PYTHON -eq "true") {
  Write-Log "Invoking install_python_interpreters.ps1" -Level Info
  & "tools\internal_ci\helper_scripts\install_python_interpreters.ps1"
}

# Needed for uploading test results to bigquery
& python -m pip install google-api-python-client oauth2client "six==1.16.0"

& git submodule update --init

Write-Log "prepare_build_windows.ps1 exiting with success" -Level Success
