# Copyright 2026 The gRPC Authors.
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

cdef const char* SERVER_CALL_HOOK = "grpc_server_call_hook"


def register_server_call_hook(object capsule) -> None:
    """Install a process-wide server call hook, or clear it with None.

    `capsule` is a PyCapsule named "grpc_server_call_hook" holding the address of
    a `grpc_server_call_hook_vtable` owned by the caller. The vtable must outlive
    every call that may run against it, so install it before serving.
    """
    cdef void* ptr
    if capsule is None:
        grpc_server_call_hook_register(NULL)
        return
    ptr = cpython.PyCapsule_GetPointer(capsule, SERVER_CALL_HOOK)
    grpc_server_call_hook_register(
        <const grpc_server_call_hook_vtable*>ptr)
