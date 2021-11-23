# Copyright 2021 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Wrapper classes used to modify the behavior of response objects."""

import grpc

class _UnaryStreamWrapper(grpc.Call, grpc.Future):
    def __init__(self, underlay_call):
        super().__init__()
        self._underlay_call = underlay_call

    def initial_metadata(self):
        return self._underlay_call.initial_metadata()

    def trailing_metadata(self):
        return self._underlay_call.initial_metadata()

    def code(self):
        return self._underlay_call.code()

    def details(self):
        return self._underlay_call.details()

    def debug_error_string(self):
        return self._underlay_call.debug_error_string()

    def cancelled(self):
        return self._underlay_call.cancelled()

    def running(self):
        return self._underlay_call.running()

    def done(self):
        return self._underlay_call.done()

    def result(self, timeout=None):
        return self._underlay_call.result(timeout=timeout)

    def exception(self, timeout=None):
        if self._exception:
            return self._exception
        else:
            return self._underlay_call.exception(timeout=timeout)

    def traceback(self, timeout=None):
        return self._underlay_call.traceback(timeout=timeout)

    def add_done_callback(self, fn):
        return self._underlay_call.add_done_callback(fn)

    def add_callback(self, callback):
        return self._underlay_call.add_callback(callback)

    def is_active(self):
        return self._underlay_call.is_active()

    def time_remaining(self):
        return self._underlay_call.time_remaining()

    def cancel(self):
        return self._underlay_call.cancel()

    def __iter__(self):
        return self

    def __next__(self):
        try:
            msg = next(self._underlay_call)
            print("Interceptor logging {}".format(msg))
            return msg
        except StopIteration:
            raise
