# Copyright 2018 The gRPC Authors
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


cdef object _custom_op_on_c_call(int op, grpc_call *call):
  raise NotImplementedError("No custom hooks are implemented")

def install_census_context_from_call(Call call):
  pass

def uninstall_context():
  pass

def build_context():
  pass

cdef class CensusContext:
  pass

def set_census_context_on_call(_CallState call_state, CensusContext census_ctx):
  pass
