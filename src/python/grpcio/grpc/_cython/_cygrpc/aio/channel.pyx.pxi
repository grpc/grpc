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
#


class _WatchConnectivityFailed(Exception):
    """Dedicated exception class for watch connectivity failed.

    It might be failed due to deadline exceeded.
    """
cdef CallbackFailureHandler _WATCH_CONNECTIVITY_FAILURE_HANDLER = CallbackFailureHandler(
    'watch_connectivity_state',
    'Timed out',
    _WatchConnectivityFailed)


cdef class AioChannel:
    def __cinit__(self, bytes target, tuple options, ChannelCredentials credentials, object loop):
        init_grpc_aio()
        if options is None:
            options = ()
        cdef _ChannelArgs channel_args = _ChannelArgs(options)
        self.target = target
        self.loop = loop
        self._status = AIO_CHANNEL_STATUS_READY
        self._registered_call_handles = {}

        if credentials is None:
            self._is_secure = False
            creds = grpc_insecure_credentials_create();
            self.channel = grpc_channel_create(<char *>target,
                creds,
                channel_args.c_args())
            grpc_channel_credentials_release(creds)
        else:
            self._is_secure = True
            creds = <grpc_channel_credentials *> credentials.c()
            self.channel = grpc_channel_create(<char *>target,
                creds,
                channel_args.c_args())
            grpc_channel_credentials_release(creds)

    def __dealloc__(self):
        shutdown_grpc_aio()

    def __repr__(self):
        class_name = self.__class__.__name__
        id_ = id(self)
        return f"<{class_name} {id_}>"

    def check_connectivity_state(self, bint try_to_connect):
        """A Cython wrapper for Core's check connectivity state API."""
        if self._status == AIO_CHANNEL_STATUS_DESTROYED:
            return ConnectivityState.shutdown
        else:
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
        if self._status in (AIO_CHANNEL_STATUS_DESTROYED, AIO_CHANNEL_STATUS_CLOSING):
            raise UsageError('Channel is closed.')

        cdef gpr_timespec c_deadline = _timespec_from_time(deadline)

        cdef object future = self.loop.create_future()
        cdef CallbackWrapper wrapper = CallbackWrapper(
            future,
            self.loop,
            _WATCH_CONNECTIVITY_FAILURE_HANDLER)
        grpc_channel_watch_connectivity_state(
            self.channel,
            last_observed_state,
            c_deadline,
            global_completion_queue(),
            wrapper.c_functor())

        try:
            await future
        except _WatchConnectivityFailed:
            return False
        else:
            return True

    def closing(self):
        self._status = AIO_CHANNEL_STATUS_CLOSING

    def close(self):
        self._status = AIO_CHANNEL_STATUS_DESTROYED
        grpc_channel_destroy(self.channel)

    def closed(self):
        return self._status in (AIO_CHANNEL_STATUS_CLOSING, AIO_CHANNEL_STATUS_DESTROYED)

    def get_registered_call_handle(self, bytes method):
        """
        Get or registers a call handler for a method.

        This method is not thread-safe. It is acceptable since method is only called
        during multicallable construction, not during RPC exeution. Moreover there
        are no `await` suspension points, which can interleave.

        Args:
          method: Required, the method name for the RPC.

        Returns:
          The registered call handle pointer as a Python Long.
        """
        if method not in self._registered_call_handles:
            self._registered_call_handles[method] = CallHandle(
                cpython.PyLong_FromVoidPtr(self.channel), method)
        return self._registered_call_handles[method].call_handle

    def call(self,
             bytes method,
             object deadline,
             object python_call_credentials,
             object wait_for_ready,
             object registered_call_handle = None):
        """Assembles a Cython Call object.

        Returns:
          An _AioCall object.
        """
        if self.closed():
            raise UsageError('Channel is closed.')

        cdef CallCredentials cython_call_credentials
        if python_call_credentials is not None:
            if not self._is_secure:
                raise UsageError("Call credentials are only valid on secure channels")

            cython_call_credentials = python_call_credentials._credentials
        else:
            cython_call_credentials = None

        return _AioCall(self, deadline, method, cython_call_credentials, wait_for_ready, registered_call_handle)
