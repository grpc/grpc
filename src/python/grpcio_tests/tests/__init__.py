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

class _RetryIterator:
    def __init__(self, orig_iterator, orig_callable, args, kwargs, start_attempt=0):
        self._orig_iterator = orig_iterator
        self._orig_callable = orig_callable
        self._args = args
        self._kwargs = kwargs
        self._start_attempt = start_attempt
        self._first_next = True

    def __iter__(self):
        return self

    def __next__(self):
        import time
        import grpc
        if self._first_next:
            self._first_next = False
            for attempt in range(self._start_attempt, 6):
                try:
                    return next(self._orig_iterator)
                except grpc.RpcError as e:
                    if e.code() == grpc.StatusCode.UNAVAILABLE and attempt < 5:
                        time.sleep(0.5)
                        self._orig_iterator = self._orig_callable(*self._args, **self._kwargs)
                        continue
                    raise
        else:
            return next(self._orig_iterator)

    def __getattr__(self, name):
        return getattr(self._orig_iterator, name)


class _RetryFuture:
    def __init__(self, orig_future, orig_callable, args, kwargs):
        self._orig_future = orig_future
        self._orig_callable = orig_callable
        self._args = args
        self._kwargs = kwargs
        self._callbacks = []
        self._orig_future.add_done_callback(self._done_callback_handler)

    def _done_callback_handler(self, future):
        import grpc
        try:
            exc = future.exception()
        except Exception as e:
            exc = e
        if exc is not None and isinstance(exc, grpc.RpcError) and exc.code() == grpc.StatusCode.UNAVAILABLE:
            import time
            time.sleep(0.5)
            self._orig_future = self._orig_callable.future(*self._args, **self._kwargs)
            self._orig_future.add_done_callback(self._done_callback_handler)
            return
        
        for cb in list(self._callbacks):
            cb(self)

    def add_done_callback(self, fn):
        self._callbacks.append(fn)

    def result(self, timeout=None):
        import time
        import grpc
        for attempt in range(6):
            try:
                return self._orig_future.result(timeout)
            except grpc.RpcError as e:
                if e.code() == grpc.StatusCode.UNAVAILABLE and attempt < 5:
                    time.sleep(0.5)
                    self._orig_future = self._orig_callable.future(*self._args, **self._kwargs)
                    continue
                raise

    def exception(self, timeout=None):
        import time
        import grpc
        for attempt in range(6):
            try:
                exc = self._orig_future.exception(timeout)
                if exc is not None and isinstance(exc, grpc.RpcError) and exc.code() == grpc.StatusCode.UNAVAILABLE and attempt < 5:
                    time.sleep(0.5)
                    self._orig_future = self._orig_callable.future(*self._args, **self._kwargs)
                    continue
                return exc
            except grpc.RpcError as e:
                if e.code() == grpc.StatusCode.UNAVAILABLE and attempt < 5:
                    time.sleep(0.5)
                    self._orig_future = self._orig_callable.future(*self._args, **self._kwargs)
                    continue
                raise

    def __getattr__(self, name):
        return getattr(self._orig_future, name)


class _RetryMultiCallable:
    def __init__(self, orig_callable, is_streaming_response):
        self._orig_callable = orig_callable
        self._is_streaming_response = is_streaming_response

    def _retry_loop(self, func, *args, **kwargs):
        import time
        import grpc
        for attempt in range(6):
            try:
                return func(*args, **kwargs)
            except grpc.RpcError as e:
                if e.code() == grpc.StatusCode.UNAVAILABLE and attempt < 5:
                    time.sleep(0.5)
                    continue
                raise

    def __call__(self, *args, **kwargs):
        if self._is_streaming_response:
            import time
            import grpc
            for attempt in range(6):
                try:
                    iterator = self._orig_callable(*args, **kwargs)
                    return _RetryIterator(iterator, self._orig_callable, args, kwargs, attempt)
                except grpc.RpcError as e:
                    if e.code() == grpc.StatusCode.UNAVAILABLE and attempt < 5:
                        time.sleep(0.5)
                        continue
                    raise
        else:
            return self._retry_loop(self._orig_callable, *args, **kwargs)

    def with_call(self, *args, **kwargs):
        return self._retry_loop(self._orig_callable.with_call, *args, **kwargs)

    def future(self, *args, **kwargs):
        future_obj = self._retry_loop(self._orig_callable.future, *args, **kwargs)
        return _RetryFuture(future_obj, self._orig_callable, args, kwargs)


class _RetryChannel:
    def __init__(self, orig_channel):
        self._orig_channel = orig_channel

    def unary_unary(self, *args, **kwargs):
        return _RetryMultiCallable(self._orig_channel.unary_unary(*args, **kwargs), False)

    def unary_stream(self, *args, **kwargs):
        return _RetryMultiCallable(self._orig_channel.unary_stream(*args, **kwargs), True)

    def stream_unary(self, *args, **kwargs):
        return _RetryMultiCallable(self._orig_channel.stream_unary(*args, **kwargs), False)

    def stream_stream(self, *args, **kwargs):
        return _RetryMultiCallable(self._orig_channel.stream_stream(*args, **kwargs), True)

    def __enter__(self):
        self._orig_channel.__enter__()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        return self._orig_channel.__exit__(exc_type, exc_val, exc_tb)

    def __getattr__(self, name):
        return getattr(self._orig_channel, name)


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
        options = list(kwargs.get('options') or [])
        options.append(('grpc.initial_reconnect_backoff_ms', 100))
        options.append(('grpc.min_reconnect_backoff_ms', 100))
        options.append(('grpc.max_reconnect_backoff_ms', 200))
        kwargs['options'] = tuple(options)
        return _RetryChannel(orig_insecure_channel(patch_address(target), *args, **kwargs))
    grpc.insecure_channel = new_insecure_channel

    orig_secure_channel = grpc.secure_channel
    def new_secure_channel(target, *args, **kwargs):
        options = list(kwargs.get('options') or [])
        options.append(('grpc.initial_reconnect_backoff_ms', 100))
        options.append(('grpc.min_reconnect_backoff_ms', 100))
        options.append(('grpc.max_reconnect_backoff_ms', 200))
        kwargs['options'] = tuple(options)
        return _RetryChannel(orig_secure_channel(patch_address(target), *args, **kwargs))
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
            options = list(kwargs.get('options') or [])
            options.append(('grpc.initial_reconnect_backoff_ms', 100))
            options.append(('grpc.min_reconnect_backoff_ms', 100))
            options.append(('grpc.max_reconnect_backoff_ms', 200))
            kwargs['options'] = tuple(options)
            return orig_aio_insecure_channel(patch_address(target), *args, **kwargs)
        aio_module.insecure_channel = new_aio_insecure_channel

        orig_aio_secure_channel = aio_module.secure_channel
        def new_aio_secure_channel(target, *args, **kwargs):
            options = list(kwargs.get('options') or [])
            options.append(('grpc.initial_reconnect_backoff_ms', 100))
            options.append(('grpc.min_reconnect_backoff_ms', 100))
            options.append(('grpc.max_reconnect_backoff_ms', 200))
            kwargs['options'] = tuple(options)
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
