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
        self._loop = get_working_loop()
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

    async def _async_resolve(self, bytes host, bytes port):
        self._task_resolve = None
        try:
            resolved = await self._loop.getaddrinfo(host, port)
        except Exception as e:
            grpc_custom_resolve_callback(
                <grpc_custom_resolver*>self._grpc_resolver,
                NULL,
                grpc_socket_error("Resolve address [{}:{}] failed: {}: {}".format(
                    host, port, type(e), str(e)).encode())
            )
        else:
            grpc_custom_resolve_callback(
                <grpc_custom_resolver*>self._grpc_resolver,
                tuples_to_resolvaddr(resolved),
                <grpc_error_handle>0
            )

    cdef void resolve(self, const char* host, const char* port):
        assert not self._task_resolve

        self._task_resolve = self._loop.create_task(
            self._async_resolve(host, port)
        )
