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

cimport cpython

cdef struct CallbackContext:
    # C struct to store callback context in the form of pointers.
    #    
    #   Attributes:
    #     functor: A grpc_experimental_completion_queue_functor represents the
    #       callback function in the only way C-Core understands.
    #     waiter: An asyncio.Future object that fulfills when the callback is
    #       invoked by C-Core.
    #     failure_handler: A CallbackFailureHandler object that called when C-Core
    #       returns 'success == 0' state.
    grpc_experimental_completion_queue_functor functor
    cpython.PyObject *waiter
    cpython.PyObject *failure_handler
