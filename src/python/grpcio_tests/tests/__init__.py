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

from __future__ import absolute_import

import sys

def _patch_grpc_localhost():
    if sys.platform != "darwin":
        return
    try:
        import grpc
    except ImportError:
        return
    if getattr(grpc, "_patched_localhost", False):
        return
    grpc._patched_localhost = True

    def patch_address(address):
        if not isinstance(address, str):
            return address
        if address.startswith("[::]:"):
            return address.replace("[::]:", "127.0.0.1:", 1)
        if address.startswith("localhost:"):
            return address.replace("localhost:", "127.0.0.1:", 1)
        if address == "[::]":
            return "127.0.0.1"
        if address == "localhost":
            return "127.0.0.1"
        return address

    orig_server = grpc.server
    def new_server(*args, **kwargs):
        options = list(kwargs.get('options') or [])
        if not any(k == 'grpc.so_reuseport' for k, _ in options):
            options.append(('grpc.so_reuseport', 0))
        kwargs['options'] = tuple(options)

        server = orig_server(*args, **kwargs)
        orig_add_insecure = server.add_insecure_port
        def new_add_insecure(address):
            return orig_add_insecure(patch_address(address))
        server.add_insecure_port = new_add_insecure
        
        orig_add_secure = server.add_secure_port
        def new_add_secure(address, *args, **kwargs):
            return orig_add_secure(patch_address(address), *args, **kwargs)
        server.add_secure_port = new_add_secure
        return server
    grpc.server = new_server

    orig_insecure_channel = grpc.insecure_channel
    def new_insecure_channel(target, *args, **kwargs):
        return orig_insecure_channel(patch_address(target), *args, **kwargs)
    grpc.insecure_channel = new_insecure_channel

    orig_secure_channel = grpc.secure_channel
    def new_secure_channel(target, *args, **kwargs):
        return orig_secure_channel(patch_address(target), *args, **kwargs)
    grpc.secure_channel = new_secure_channel

    def patch_aio(aio_module):
        orig_aio_server = aio_module.server
        def new_aio_server(*args, **kwargs):
            options = list(kwargs.get('options') or [])
            if not any(k == 'grpc.so_reuseport' for k, _ in options):
                options.append(('grpc.so_reuseport', 0))
            kwargs['options'] = tuple(options)

            server = orig_aio_server(*args, **kwargs)
            orig_add_insecure = server.add_insecure_port
            def new_add_insecure(address):
                return orig_add_insecure(patch_address(address))
            server.add_insecure_port = new_add_insecure
            
            orig_add_secure = server.add_secure_port
            def new_add_secure(address, *args, **kwargs):
                return orig_add_secure(patch_address(address), *args, **kwargs)
            server.add_secure_port = new_add_secure
            return server
        aio_module.server = new_aio_server

        orig_aio_insecure_channel = aio_module.insecure_channel
        def new_aio_insecure_channel(target, *args, **kwargs):
            return orig_aio_insecure_channel(patch_address(target), *args, **kwargs)
        aio_module.insecure_channel = new_aio_insecure_channel

        orig_aio_secure_channel = aio_module.secure_channel
        def new_aio_secure_channel(target, *args, **kwargs):
            return orig_aio_secure_channel(patch_address(target), *args, **kwargs)
        aio_module.secure_channel = new_aio_secure_channel

    try:
        from grpc.experimental import aio as exp_aio
        patch_aio(exp_aio)
    except (ImportError, AttributeError):
        pass

    try:
        import grpc.aio as public_aio
        patch_aio(public_aio)
    except (ImportError, AttributeError):
        pass

_patch_grpc_localhost()

from tests import _loader
from tests import _runner

Loader = _loader.Loader
Runner = _runner.Runner
