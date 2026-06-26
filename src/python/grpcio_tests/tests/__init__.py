# Copyright 2015 gRPC authors.
#
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

from tests import _loader
from tests import _runner

Loader = _loader.Loader
Runner = _runner.Runner

import sys

if sys.platform == "darwin":
    import grpc

    _original_grpc_server = grpc.server

    def _grpc_server(*args, **kwargs):
        srv = _original_grpc_server(*args, **kwargs)

        _orig_add_insecure = srv.add_insecure_port

        def _add_insecure(addr):
            if isinstance(addr, str):
                if addr.startswith("[::]:"):
                    addr = "127.0.0.1:" + addr.split(":")[-1]
                elif addr.startswith("localhost:"):
                    addr = "127.0.0.1:" + addr.split(":", 1)[1]
            return _orig_add_insecure(addr)

        srv.add_insecure_port = _add_insecure

        _orig_add_secure = srv.add_secure_port

        def _add_secure(addr, creds):
            if isinstance(addr, str):
                if addr.startswith("[::]:"):
                    addr = "127.0.0.1:" + addr.split(":")[-1]
                elif addr.startswith("localhost:"):
                    addr = "127.0.0.1:" + addr.split(":", 1)[1]
            return _orig_add_secure(addr, creds)

        srv.add_secure_port = _add_secure

        return srv

    grpc.server = _grpc_server

    _original_insecure_channel = grpc.insecure_channel

    def _insecure_channel(target, options=None, compression=None):
        if isinstance(target, str):
            if target.startswith("[::]:"):
                target = "127.0.0.1:" + target.split(":")[-1]
            elif target.startswith("localhost:"):
                target = "127.0.0.1:" + target.split(":", 1)[1]
        return _original_insecure_channel(target, options, compression)

    grpc.insecure_channel = _insecure_channel

    _original_secure_channel = grpc.secure_channel

    def _secure_channel(target, credentials, options=None, compression=None):
        if isinstance(target, str):
            if target.startswith("[::]:"):
                target = "127.0.0.1:" + target.split(":")[-1]
            elif target.startswith("localhost:"):
                target = "127.0.0.1:" + target.split(":", 1)[1]
        return _original_secure_channel(target, credentials, options, compression)

    grpc.secure_channel = _secure_channel
