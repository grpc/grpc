# Copyright 2025 The gRPC Authors
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

from libcpp.string cimport string
from libcpp.vector cimport vector

cdef extern from "../../../test/cpp/sleuth/sleuth.h" namespace "grpc_sleuth":
    cdef int RunSleuth_Wrapper "grpc_sleuth::RunSleuth_Wrapper"(
        vector[string] args, void* python_print,
        void (*python_cb)(void*, const string&)) except +

cdef inline void _python_cb(void* python_print, const string& message) noexcept:
    (<object>python_print)(message.decode('UTF-8'))

def run_sleuth(args: list[str], print_fn: Callable[[str], None] = None):
    """
    args: Command line args excluding the binary name at [0].
    print_fn: Callback with one string argument that contains the output.
              Prints to stdout if None. May be called for any number of times
              until this function returns.
    """
    cdef vector[string] c_args
    for arg in args:
        c_args.push_back(arg.encode('UTF-8'))
    if print_fn is None:
        return RunSleuth_Wrapper(c_args, NULL, NULL)
    else:
        return RunSleuth_Wrapper(c_args, <void*>print_fn, _python_cb)
