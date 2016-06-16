@rem Copyright 2016, Google Inc.
@rem All rights reserved.
@rem
@rem Redistribution and use in source and binary forms, with or without
@rem modification, are permitted provided that the following conditions are
@rem met:
@rem
@rem     * Redistributions of source code must retain the above copyright
@rem notice, this list of conditions and the following disclaimer.
@rem     * Redistributions in binary form must reproduce the above
@rem copyright notice, this list of conditions and the following disclaimer
@rem in the documentation and/or other materials provided with the
@rem distribution.
@rem     * Neither the name of Google Inc. nor the names of its
@rem contributors may be used to endorse or promote products derived from
@rem this software without specific prior written permission.
@rem
@rem THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
@rem "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
@rem LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
@rem A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
@rem OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
@rem SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
@rem LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
@rem DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
@rem THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
@rem (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
@rem OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

@rem enter repo root
cd /d %~dp0\..

set PROTOC=protoc
set GRPC_CPP_PLUGIN_PATH=grpc_cpp_plugin

%PROTOC% src/proto/grpc/lb/v1/load_balancer.proto --grpc_out=./ --plugin=protoc-gen-grpc=%GRPC_CPP_PLUGIN_PATH%
%PROTOC% src/proto/grpc/lb/v1/load_balancer.proto --cpp_out=./
%PROTOC% src/proto/grpc/testing/compiler_test.proto --grpc_out=./ --plugin=protoc-gen-grpc=%GRPC_CPP_PLUGIN_PATH%
%PROTOC% src/proto/grpc/testing/compiler_test.proto --cpp_out=./
%PROTOC% src/proto/grpc/testing/control.proto --grpc_out=./ --plugin=protoc-gen-grpc=%GRPC_CPP_PLUGIN_PATH%
%PROTOC% src/proto/grpc/testing/control.proto --cpp_out=./
%PROTOC% src/proto/grpc/testing/duplicate/echo_duplicate.proto --grpc_out=./ --plugin=protoc-gen-grpc=%GRPC_CPP_PLUGIN_PATH%
%PROTOC% src/proto/grpc/testing/duplicate/echo_duplicate.proto --cpp_out=./
%PROTOC% src/proto/grpc/testing/echo.proto --grpc_out=./ --plugin=protoc-gen-grpc=%GRPC_CPP_PLUGIN_PATH%
%PROTOC% src/proto/grpc/testing/echo.proto --cpp_out=./
%PROTOC% src/proto/grpc/testing/echo_messages.proto --grpc_out=./ --plugin=protoc-gen-grpc=%GRPC_CPP_PLUGIN_PATH%
%PROTOC% src/proto/grpc/testing/echo_messages.proto --cpp_out=./
%PROTOC% src/proto/grpc/testing/empty.proto --grpc_out=./ --plugin=protoc-gen-grpc=%GRPC_CPP_PLUGIN_PATH%
%PROTOC% src/proto/grpc/testing/empty.proto --cpp_out=./
%PROTOC% src/proto/grpc/testing/messages.proto --grpc_out=./ --plugin=protoc-gen-grpc=%GRPC_CPP_PLUGIN_PATH%
%PROTOC% src/proto/grpc/testing/messages.proto --cpp_out=./
%PROTOC% src/proto/grpc/testing/metrics.proto --grpc_out=./ --plugin=protoc-gen-grpc=%GRPC_CPP_PLUGIN_PATH%
%PROTOC% src/proto/grpc/testing/metrics.proto --cpp_out=./
%PROTOC% src/proto/grpc/testing/payloads.proto --grpc_out=./ --plugin=protoc-gen-grpc=%GRPC_CPP_PLUGIN_PATH%
%PROTOC% src/proto/grpc/testing/payloads.proto --cpp_out=./
%PROTOC% src/proto/grpc/testing/services.proto --grpc_out=./ --plugin=protoc-gen-grpc=%GRPC_CPP_PLUGIN_PATH%
%PROTOC% src/proto/grpc/testing/services.proto --cpp_out=./
%PROTOC% src/proto/grpc/testing/stats.proto --grpc_out=./ --plugin=protoc-gen-grpc=%GRPC_CPP_PLUGIN_PATH%
%PROTOC% src/proto/grpc/testing/stats.proto --cpp_out=./
%PROTOC% src/proto/grpc/testing/test.proto --grpc_out=./ --plugin=protoc-gen-grpc=%GRPC_CPP_PLUGIN_PATH%
%PROTOC% src/proto/grpc/testing/test.proto --cpp_out=./
