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


class _WatchConnectivityFailed(Exception): pass
cdef CallbackFailureHandler _WATCH_CONNECTIVITY_FAILURE_HANDLER = CallbackFailureHandler(
    'watch_connectivity_state',
    'Maybe timed out.',
    _WatchConnectivityFailed)


cdef class AioChannel:
    def __cinit__(self, bytes target, tuple options, ChannelCredentials credentials):
        if options is None:
            options = ()
        cdef _ChannelArgs channel_args = _ChannelArgs(options)
        self._target = target
        if credentials is None:
            self.channel = grpc_insecure_channel_create(
                <char *>target,
                channel_args.c_args(),
                NULL)
        else:
            self.channel = grpc_secure_channel_create(
                <grpc_channel_credentials *> credentials.c(),
                <char *> target,
                channel_args.c_args(),
                NULL)
        self._loop = asyncio.get_event_loop()

    def __repr__(self):
        class_name = self.__class__.__name__
        id_ = id(self)
        return f"<{class_name} {id_}>"

    def check_connectivity_state(self, bint try_to_connect):
        return grpc_channel_check_connectivity_state(
            self.channel,
            try_to_connect,
        )

    async def watch_connectivity_state(self,
                                       grpc_connectivity_state last_observed_state,
                                       object deadline):
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
            return None
        else:
            return self.check_connectivity_state(False)

    def close(self):
        grpc_channel_destroy(self.channel)

    def call(self,
             bytes method,
             object deadline,
             CallCredentials credentials):
        """Assembles a Cython Call object.

        Returns:
          The _AioCall object.
        """
        cdef _AioCall call = _AioCall(self, deadline, method, credentials)
        return call
