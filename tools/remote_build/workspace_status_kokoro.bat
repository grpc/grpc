@rem Copyright 2019 gRPC authors.
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

@echo off
@rem Adds additional labels to results page for Bazel RBE builds on Kokoro
@rem Note that this script needs to be a .bat file, the .sh version of this
@rem script is simply ignored when used on Windows.

@rem Provide a way to go from Bazel RBE links back to Kokoro job results
@rem which is important for debugging test infrastructure problems.
@rem TODO(jtattermusch): replace this workaround by something more user-friendly.

echo KOKORO_RESULTSTORE_URL https://source.cloud.google.com/results/invocations/%KOKORO_BUILD_ID%
echo KOKORO_SPONGE_URL http://sponge.corp.google.com/%KOKORO_BUILD_ID%

echo KOKORO_BUILD_NUMBER %KOKORO_BUILD_NUMBER%
echo KOKORO_JOB_NAME %KOKORO_JOB_NAME%
echo KOKORO_GITHUB_COMMIT %KOKORO_GITHUB_COMMIT%
