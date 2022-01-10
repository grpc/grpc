# Copyright 2022 The gRPC Authors
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

cdef extern from "grpc/impl/codegen/atm.h":
    ctypedef int gpr_atm

cdef extern from "absl/status/status.h" namespace "absl":
    cdef cppclass Status "absl_status":
        pass

cdef extern from "src/core/lib/transport/byte_stream.h" namespace "grpc_core":
    cdef cppclass ByteStream:
        pass

cdef extern from "src/core/lib/transport/transport.h" namespace "grpc_core":
    cdef struct grpc_transport_stream_stats:
        pass

cdef extern from "src/core/lib/transport/metadata_batch.h" namespace "grpc_core":
    cdef cppclass MetadataMap:
        pass

    ctypedef MetadataMap grpc_metadata_batch

cdef extern from "src/core/lib/channel/call_tracer.h" namespace "grpc_core":
    cdef cppclass CallTracer:

        cppclass CallAttemptTracer:
            void RecordSendInitialMetadata(grpc_metadata_batch*, int)
            void RecordOnDoneSendInitialMetadata(gpr_atm*)
            void RecordSendTrailingMetadata(grpc_metadata_batch*)
            void RecordSendMessage(ByteStream&)
            void RecordReceivedInitialMetadata(grpc_metadata_batch*, int)
            void RecordReceivedMessage(ByteStream&)
            void RecordReceivedTrailingMetadata(absl_status, grpc_metadata_batch*, grpc_transport_stream_stats&)
            void RecordCancel(grpc_error_handle cancel_error)
            void RecordEnd(gpr_timespec& latency)

        CallAttemptTracer* StartNewAttempt(bool)
