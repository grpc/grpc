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

@rem make sure msys binaries are preferred over cygwin binaries
@rem set path to python 2.7
set PATH=C:\tools\msys64\usr\bin;C:\Python27;%PATH%

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

@rem Needed for big_query_utils
python -m pip install google-api-python-client

@rem C# prerequisites: Install dotnet SDK
powershell -File src\csharp\install_dotnet_sdk.ps1
set PATH=%LOCALAPPDATA%\Microsoft\dotnet;%PATH%

@rem Newest version of Go is required to be able to build boringssl with cmake
@rem TODO(jtattermusch): try to eliminate the dependency on Go
choco install golang -y --version 1.13.1 --limit-output

@rem Install Python 3.8.0
@rem NOTE(lidiz): Python installer process may live longer than expected, and
@rem has other side effects. It needs to be installed last to reduce impact.
powershell -File tools\internal_ci\helper_scripts\install_python38.ps1

@rem Disable some unwanted dotnet options
set NUGET_XMLDOC_MODE=skip
set DOTNET_SKIP_FIRST_TIME_EXPERIENCE=true
set DOTNET_CLI_TELEMETRY_OPTOUT=true

git submodule update --init
