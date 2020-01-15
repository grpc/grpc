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


class _WatchConnectivityFailed(Exception):
    """Dedicated exception class for watch connectivity failed.

    It might be failed due to deadline exceeded.
    """
cdef CallbackFailureHandler _WATCH_CONNECTIVITY_FAILURE_HANDLER = CallbackFailureHandler(
    'watch_connectivity_state',
    'Timed out',
    _WatchConnectivityFailed)


cdef class AioChannel:
    def __cinit__(self, bytes target, tuple options, ChannelCredentials credentials):
        if options is None:
            options = ()
        cdef _ChannelArgs channel_args = _ChannelArgs(options)
        self._target = target
        self.cq = CallbackCompletionQueue()
        self._loop = asyncio.get_event_loop()
        self._status = AIO_CHANNEL_STATUS_READY

        if credentials is None:
            self.channel = grpc_insecure_channel_create(
                <char *>target,
                channel_args.c_args(),
                NULL)
        else:
            self.channel = grpc_secure_channel_create(
                <grpc_channel_credentials *> credentials.c(),
                <char *>target,
                channel_args.c_args(),
                NULL)

    def __repr__(self):
        class_name = self.__class__.__name__
        id_ = id(self)
        return f"<{class_name} {id_}>"

    def check_connectivity_state(self, bint try_to_connect):
        """A Cython wrapper for Core's check connectivity state API."""
        return grpc_channel_check_connectivity_state(
            self.channel,
            try_to_connect,
        )

    async def watch_connectivity_state(self,
                                       grpc_connectivity_state last_observed_state,
                                       object deadline):
        """Watch for one connectivity state change.

        Keeps mirroring the behavior from Core, so we can easily switch to
        other design of API if necessary.
        """
        if self._status == AIO_CHANNEL_STATUS_DESTROYED:
            # TODO(lidiz) switch to UsageError
            raise RuntimeError('Channel is closed.')
        cdef gpr_timespec c_deadline = _timespec_from_time(deadline)

        cdef object future = self._loop.create_future()
        cdef CallbackWrapper wrapper = CallbackWrapper(
            future,
            _WATCH_CONNECTIVITY_FAILURE_HANDLER)
        grpc_channel_watch_connectivity_state(
            self.channel,
            last_observed_state,
            c_deadline,
            self.cq.c_ptr(),
            wrapper.c_functor())

        try:
            await future
        except _WatchConnectivityFailed:
            return False
        else:
            return True

    def close(self):
        grpc_channel_destroy(self.channel)
        self._status = AIO_CHANNEL_STATUS_DESTROYED

    def call(self,
             bytes method,
             object deadline,
             object python_call_credentials):
        """Assembles a Cython Call object.

        Returns:
          The _AioCall object.
        """
        if self._status == AIO_CHANNEL_STATUS_DESTROYED:
            # TODO(lidiz) switch to UsageError
            raise RuntimeError('Channel is closed.')

        cdef CallCredentials cython_call_credentials
        if python_call_credentials is not None:
            cython_call_credentials = python_call_credentials._credentials
        else:
            cython_call_credentials = None

        cdef _AioCall call = _AioCall(self, deadline, method, cython_call_credentials)
        return call
