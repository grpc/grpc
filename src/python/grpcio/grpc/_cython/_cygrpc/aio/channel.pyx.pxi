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

cdef class AioChannel:
    def __cinit__(self, bytes target):
        self.channel = grpc_insecure_channel_create(<char *>target, NULL, NULL)
        self.cq = CallbackCompletionQueue()
        self._target = target

    def __repr__(self):
        class_name = self.__class__.__name__
        id_ = id(self)
        return f"<{class_name} {id_}>"

    def close(self):
        grpc_channel_destroy(self.channel)

    async def unary_unary(self, method, request, timeout, cancel_status):
        call = _AioCall(self)
        return await call.unary_unary(method, request, timeout, cancel_status)
