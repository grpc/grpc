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

bash tools/internal_ci/helper_scripts/gen_report_index.sh

@rem Needed for big_query_utils
python -m pip install google-api-python-client

git submodule update --init
