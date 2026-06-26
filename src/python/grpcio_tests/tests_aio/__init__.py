# Copyright 2019 gRPC authors.
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
    try:
        from grpc.experimental import aio

        _original_aio_server = aio.server

        def _aio_server(*args, **kwargs):
            srv = _original_aio_server(*args, **kwargs)

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

        aio.server = _aio_server

        _original_aio_insecure_channel = aio.insecure_channel

        def _aio_insecure_channel(target, options=None, compression=None):
            if isinstance(target, str):
                if target.startswith("[::]:"):
                    target = "127.0.0.1:" + target.split(":")[-1]
                elif target.startswith("localhost:"):
                    target = "127.0.0.1:" + target.split(":", 1)[1]
            return _original_aio_insecure_channel(target, options, compression)

        aio.insecure_channel = _aio_insecure_channel

        _original_aio_secure_channel = aio.secure_channel

        def _aio_secure_channel(target, credentials, options=None, compression=None):
            if isinstance(target, str):
                if target.startswith("[::]:"):
                    target = "127.0.0.1:" + target.split(":")[-1]
                elif target.startswith("localhost:"):
                    target = "127.0.0.1:" + target.split(":", 1)[1]
            return _original_aio_secure_channel(target, credentials, options, compression)

        aio.secure_channel = _aio_secure_channel
    except ImportError:
        pass
