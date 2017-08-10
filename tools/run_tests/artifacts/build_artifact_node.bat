@rem Copyright 2016 gRPC authors.
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

set node_versions=4.0.0 5.0.0 6.0.0 7.0.0 8.0.0

set electron_versions=1.0.0 1.1.0 1.2.0 1.3.0 1.4.0 1.5.0 1.6.0

set PATH=%PATH%;C:\Program Files\nodejs\;%APPDATA%\npm

del /f /q BUILD || rmdir build /s /q

call npm update || goto :error

mkdir -p %ARTIFACTS_OUT%

for %%v in (%node_versions%) do (
  call .\node_modules\.bin\node-pre-gyp.cmd configure build --target=%%v --target_arch=%1

@rem Try again after removing openssl headers
  rmdir "%USERPROFILE%\.node-gyp\%%v\include\node\openssl" /S /Q
  rmdir "%USERPROFILE%\.node-gyp\iojs-%%v\include\node\openssl" /S /Q
  call .\node_modules\.bin\node-pre-gyp.cmd build package --target=%%v --target_arch=%1 || goto :error

  xcopy /Y /I /S build\stage\* %ARTIFACTS_OUT%\ || goto :error
)

for %%v in (%electron_versions%) do (
  cmd /V /C "set "HOME=%USERPROFILE%\electron-gyp" && call .\node_modules\.bin\node-pre-gyp.cmd configure rebuild package --runtime=electron --target=%%v --target_arch=%1 --disturl=https://atom.io/download/electron" || goto :error

  xcopy /Y /I /S build\stage\* %ARTIFACTS_OUT%\ || goto :error
)
if %errorlevel% neq 0 exit /b %errorlevel%

goto :EOF

:error
exit /b 1
