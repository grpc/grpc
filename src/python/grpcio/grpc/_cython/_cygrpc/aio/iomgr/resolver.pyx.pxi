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


cdef class _AsyncioResolver:
    def __cinit__(self):
        self._grpc_resolver = NULL
        self._task_resolve = None

    @staticmethod
    cdef _AsyncioResolver create(grpc_custom_resolver* grpc_resolver):
        resolver = _AsyncioResolver()
        resolver._grpc_resolver = grpc_resolver
        return resolver

    def __repr__(self):
        class_name = self.__class__.__name__ 
        id_ = id(self)
        return f"<{class_name} {id_}>"

    def _resolve_cb(self, future):
        error = False
        try:
            res = future.result()
        except Exception as e:
            error = True
            error_msg = str(e)
        finally:
            self._task_resolve = None

        if not error:
            grpc_custom_resolve_callback(
                <grpc_custom_resolver*>self._grpc_resolver,
                tuples_to_resolvaddr(res),
                <grpc_error*>0
            )
        else:
            grpc_custom_resolve_callback(
                <grpc_custom_resolver*>self._grpc_resolver,
                NULL,
                grpc_socket_error("getaddrinfo {}".format(error_msg).encode())
            )

    cdef void resolve(self, char* host, char* port):
        assert not self._task_resolve

        loop = asyncio.get_event_loop()
        self._task_resolve = asyncio.ensure_future(
            loop.getaddrinfo(host, port)
        )
        self._task_resolve.add_done_callback(self._resolve_cb)
