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
# distutils: language=c++

cdef class _AioState:
    cdef object lock  # threading.RLock
    cdef int refcount
    cdef object engine  # AsyncIOEngine
    cdef BaseCompletionQueue cq


cdef grpc_completion_queue *global_completion_queue()


cpdef init_grpc_aio()


cpdef shutdown_grpc_aio()


cdef extern from "src/core/lib/iomgr/timer_manager.h":
  void grpc_timer_manager_set_threading(bint enabled)


cdef extern from "src/core/lib/iomgr/iomgr_internal.h":
  void grpc_set_default_iomgr_platform()


cdef extern from "src/core/lib/iomgr/executor.h" namespace "grpc_core":
    cdef cppclass Executor:
        @staticmethod
        void SetThreadingAll(bint enable)
