@rem Copyright 2017 gRPC authors.
@rem
@rem Licensed under the Apache License, Version 2.0 (the "License");
@rem you may not use this file except in compliance with the License.
@rem You may obtain a copy of the License at
@rem
@rem     http://www.apache.org/licenses/LICENSE-2.0
@rem
@rem Unless required by applicable law or agreed to in writing, software
@rem distributed under the License is distributed on an "AS IS" BASIS,
@rem WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
@rem See the License for the specific language governing permissions and
@rem limitations under the License.

@rem allow timing of how long the script takes to run.
echo "!TIME!: prepare_build_windows.bat started"

@rem make sure msys binaries are preferred over cygwin binaries
@rem set path to python3.7
@rem set path to CMake
set PATH=C:\tools\msys64\usr\bin;C:\Python37;C:\Program Files\CMake\bin;%PATH%

@rem Print image ID of the windows kokoro image being used.
cat C:\image_id.txt

@rem create "python3" link that normally doesn't exist
dir C:\Python37\
mklink C:\Python37\python3.exe C:\Python37\python.exe

python --version
python3 --version

@rem If this is a PR using RUN_TESTS_FLAGS var, then add flags to filter tests
if defined KOKORO_GITHUB_PULL_REQUEST_NUMBER if defined RUN_TESTS_FLAGS (
  set RUN_TESTS_FLAGS=%RUN_TESTS_FLAGS% --filter_pr_tests --base_branch origin/%KOKORO_GITHUB_PULL_REQUEST_TARGET_BRANCH%
)

@rem Update DNS settings to:
@rem 1. allow resolving metadata.google.internal hostname
@rem 2. make fetching default GCE credential by oauth2client work
netsh interface ip set dns "Local Area Connection 8" static 169.254.169.254 primary
netsh interface ip add dnsservers "Local Area Connection 8" 8.8.8.8 index=2
netsh interface ip add dnsservers "Local Area Connection 8" 8.8.4.4 index=3

@rem Uninstall protoc so that it doesn't clash with C++ distribtests.
@rem (on grpc-win2016 kokoro workers it can result in GOOGLE_PROTOBUF_MIN_PROTOC_VERSION violation)
choco uninstall protoc -y --limit-output

@rem Install nasm (required for boringssl assembly optimized build as boringssl no long supports yasm)
@rem Downloading from GCS should be very reliables when on a GCP VM.
mkdir C:\nasm
curl -sSL --fail -o C:\nasm\nasm.exe https://storage.googleapis.com/grpc-build-helper/nasm-2.15.05/nasm.exe || goto :error
set PATH=C:\nasm;%PATH%
nasm

@rem Install ccache
mkdir C:\ccache
curl -sSL --fail -o C:\ccache\ccache.exe https://storage.googleapis.com/grpc-build-helper/ccache-4.8-windows-64/ccache.exe || goto :error
set PATH=C:\ccache;%PATH%
ccache --version

@rem Only install C# dependencies if we are running C# tests
If "%PREPARE_BUILD_INSTALL_DEPS_CSHARP%" == "true" (
  @rem C# prerequisites: Install dotnet SDK
  powershell -File src\csharp\install_dotnet_sdk.ps1 || goto :error

  @rem Explicitly add default nuget source.
  @rem (on Kokoro grpc-win2016 workers, the default nuget source is not configured,
  @rem which results in errors when "dotnet restore" is run)
  %LOCALAPPDATA%\Microsoft\dotnet\dotnet nuget add source https://api.nuget.org/v3/index.json -n "nuget.org"
)

@rem Add dotnet on path and disable some unwanted dotnet
@rem option regardless of PREPARE_BUILD_INSTALL_DEPS_CSHARP value.
@rem Always setting the env vars is fine since its instantaneous,
@rem it can't fail and it avoids possible issues with
@rem "setlocal" and "EnableDelayedExpansion" which would be required if
@rem we wanted to do the same under the IF block.
set PATH=%LOCALAPPDATA%\Microsoft\dotnet;%PATH%
set NUGET_XMLDOC_MODE=skip
set DOTNET_SKIP_FIRST_TIME_EXPERIENCE=true
set DOTNET_CLI_TELEMETRY_OPTOUT=true

@rem Workaround https://github.com/NuGet/Home/issues/11099 that exhibits
@rem on windows workers as "The repository primary signature's timestamping certificate is not trusted by the trust provider"
set NUGET_EXPERIMENTAL_CHAIN_BUILD_RETRY_POLICY=3,1000

@rem Only install Python interpreters if we are running Python tests
If "%PREPARE_BUILD_INSTALL_DEPS_PYTHON%" == "true" (
  echo "!TIME!: invoking install_python_interpreters.ps1"
  powershell -File tools\internal_ci\helper_scripts\install_python_interpreters.ps1 || goto :error
)

@rem Needed for uploading test results to bigquery
python -m pip install google-api-python-client oauth2client six==1.16.0 setuptools==59.6.0 || goto :error

git submodule update --init || goto :error

echo "!TIME!: prepare_build_windows.bat exiting with success"
goto :EOF

:error
echo "!TIME!: prepare_build_windows.bat exiting with error"
exit /b 1
