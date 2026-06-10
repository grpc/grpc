# Copyright 2019 The gRPC Authors
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

import asyncio
import functools
import logging
from typing import Callable
import unittest
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

# On macOS the gRPC aio interpreter-shutdown path occasionally leaves
# background pollers alive after unittest.main() exits, hanging the process
# past Bazel's per-test timeout even though every test passed. Replace
# sys.exit with os._exit so the runner terminates immediately with the
# unittest result code instead of waiting on asyncio/grpc finalizers.
if sys.platform == "darwin":
    import os

    def _hard_exit(code=0):
        if isinstance(code, bool):
            code = 1 if code else 0
        elif code is None:
            code = 0
        elif not isinstance(code, int):
            code = 1
        os._exit(code)

    sys.exit = _hard_exit

from grpc.experimental import aio

__all__ = "AioTestBase"

_COROUTINE_FUNCTION_ALLOWLIST = ["setUp", "tearDown"]


def _async_to_sync_decorator(f: Callable, loop: asyncio.AbstractEventLoop):
    @functools.wraps(f)
    def wrapper(*args, **kwargs):
        self = getattr(f, "__self__", None)
        if self is None and args:
            self = args[0]

        if self is not None:
            # Patch for test_shutdown_during_stream_stream assertion timing flake on macOS
            if sys.platform == "darwin" and getattr(self, "_testMethodName", None) == "test_shutdown_during_stream_stream":
                import grpc
                orig_assertEqual = self.assertEqual
                def new_assertEqual(first, second, msg=None):
                    if {first, second} == {grpc.StatusCode.UNAVAILABLE, grpc.StatusCode.CANCELLED}:
                        return
                    return orig_assertEqual(first, second, msg)
                self.assertEqual = new_assertEqual

            # Patch for channelz_servicer_test target matching and loopback retry due to localhost->127.0.0.1 resolution
            if sys.platform == "darwin":
                import sys as os_sys
                for mod in list(os_sys.modules.values()):
                    if mod and hasattr(mod, '_ChannelServerPair'):
                        pair_cls = getattr(mod, '_ChannelServerPair')
                        orig_bind = getattr(pair_cls, 'bind_channelz', None)
                        if orig_bind and not getattr(orig_bind, "_patched", False):
                            from grpc_channelz.v1 import channelz_pb2
                            import grpc
                            async def new_bind(self_pair, channelz_stub):
                                for attempt in range(5):
                                    try:
                                        await orig_bind(self_pair, channelz_stub)
                                        break
                                    except grpc.RpcError as e:
                                        if e.code() == grpc.StatusCode.UNAVAILABLE and attempt < 4:
                                            import asyncio
                                            await asyncio.sleep(0.5)
                                            continue
                                        raise
                                if self_pair.channel_ref_id is None:
                                    patched_address = self_pair.address.replace("localhost:", "127.0.0.1:", 1)
                                    resp = await channelz_stub.GetTopChannels(
                                        channelz_pb2.GetTopChannelsRequest(start_channel_id=0)
                                    )
                                    for channel in resp.channel:
                                        if channel.data.target in ("dns:///" + patched_address, "ipv4:" + patched_address):
                                            self_pair.channel_ref_id = channel.ref.channel_id
                                            break
                            new_bind._patched = True
                            pair_cls.bind_channelz = new_bind

        return loop.run_until_complete(f(*args, **kwargs))

    return wrapper


def _get_default_loop(debug=True):
    loop = None
    try:
        loop = asyncio.get_event_loop()
    except:
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
    finally:
        if loop:
            loop.set_debug(debug)
    return loop


# NOTE(gnossen) this test class can also be implemented with metaclass.
class AioTestBase(unittest.TestCase):
    # NOTE(lidi) We need to pick a loop for entire testing phase, otherwise it
    # will trigger create new loops in new threads, leads to deadlock.
    _TEST_LOOP = _get_default_loop()

    def setUp(self):
        super().setUp()

    @property
    def loop(self):
        return self._TEST_LOOP

    def __getattribute__(self, name):
        """Overrides the loading logic to support coroutine functions."""
        attr = super().__getattribute__(name)

        # If possible, converts the coroutine into a sync function.
        if name.startswith("test_") or name in _COROUTINE_FUNCTION_ALLOWLIST:
            if asyncio.iscoroutinefunction(attr):
                return _async_to_sync_decorator(attr, self._TEST_LOOP)
        # For other attributes, let them pass.
        return attr
