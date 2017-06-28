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

set PATH=%PATH%;C:\Program Files\nodejs\;%APPDATA%\npm

del /f /q BUILD || rmdir build /s /q

call npm install --build-from-source

@rem delete the redundant openssl headers
for /f "delims=v" %%v in ('node --version') do (
  rmdir "%USERPROFILE%\.node-gyp\%%v\include\node\openssl" /S /Q
)



@rem rebuild, because it probably failed the first time
call npm install --build-from-source %*
