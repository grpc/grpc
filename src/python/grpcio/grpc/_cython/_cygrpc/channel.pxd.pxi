# Copyright 2015 gRPC authors.
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


cdef _check_call_error_no_metadata(c_call_error)


cdef _check_and_raise_call_error_no_metadata(c_call_error)


cdef _check_call_error(c_call_error, metadata)


cdef class _CallState:

  cdef grpc_call *c_call
  cdef set due
  # call_tracer_capsule should have type of grpc._observability.ClientCallTracerCapsule
  cdef object call_tracer_capsule
  cdef void maybe_set_client_call_tracer_on_call(self, bytes method_name) except *
  cdef void maybe_delete_call_tracer(self) except *


cdef class _ChannelState:

  cdef object condition
  cdef grpc_channel *c_channel
  # A boolean field indicating that the channel is open (if True) or is being
  # closed (i.e. a call to close is currently executing) or is closed (if
  # False).
  # TODO(https://github.com/grpc/grpc/issues/3064): Eliminate "is being closed"
  # a state in which condition may be acquired by any thread, eliminate this
  # field and just use the NULLness of c_channel as an indication that the
  # channel is closed.
  cdef object open
  cdef object closed_reason

  # A dict from _BatchOperationTag to _CallState
  cdef dict integrated_call_states
  cdef grpc_completion_queue *c_call_completion_queue

  # A set of _CallState
  cdef set segregated_call_states

  cdef set connectivity_due
  cdef grpc_completion_queue *c_connectivity_completion_queue


cdef class IntegratedCall:

  cdef _ChannelState _channel_state
  cdef _CallState _call_state


cdef class SegregatedCall:

  cdef _ChannelState _channel_state
  cdef _CallState _call_state
  cdef grpc_completion_queue *_c_completion_queue


cdef class Channel:

  cdef _ChannelState _state

  # TODO(https://github.com/grpc/grpc/issues/15662): Eliminate this.
  cdef tuple _arguments
