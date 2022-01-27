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

cdef extern from "grpc/impl/codegen/propagation_bits.h":
    cdef int _GRPC_PROPAGATE_DEADLINE "GRPC_PROPAGATE_DEADLINE"
    cdef int _GRPC_PROPAGATE_CENSUS_STATS_CONTEXT "GRPC_PROPAGATE_CENSUS_STATS_CONTEXT"
    cdef int _GRPC_PROPAGATE_CENSUS_TRACING_CONTEXT "GRPC_PROPAGATE_CENSUS_TRACING_CONTEXT"
    cdef int _GRPC_PROPAGATE_CANCELLATION "GRPC_PROPAGATE_CANCELLATION"
    cdef int _GRPC_PROPAGATE_DEFAULTS "GRPC_PROPAGATE_DEFAULTS"
