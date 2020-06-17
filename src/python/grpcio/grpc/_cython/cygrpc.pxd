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
# distutils: language=c++

cimport cpython

include "_cygrpc/grpc.pxi"

include "_cygrpc/arguments.pxd.pxi"
include "_cygrpc/call.pxd.pxi"
include "_cygrpc/channel.pxd.pxi"
include "_cygrpc/credentials.pxd.pxi"
include "_cygrpc/completion_queue.pxd.pxi"
include "_cygrpc/event.pxd.pxi"
include "_cygrpc/metadata.pxd.pxi"
include "_cygrpc/operation.pxd.pxi"
include "_cygrpc/propagation_bits.pxd.pxi"
include "_cygrpc/records.pxd.pxi"
include "_cygrpc/security.pxd.pxi"
include "_cygrpc/server.pxd.pxi"
include "_cygrpc/tag.pxd.pxi"
include "_cygrpc/time.pxd.pxi"
include "_cygrpc/vtable.pxd.pxi"
include "_cygrpc/_hooks.pxd.pxi"

include "_cygrpc/iomgr.pxd.pxi"

include "_cygrpc/grpc_gevent.pxd.pxi"

IF UNAME_SYSNAME != "Windows":
    include "_cygrpc/fork_posix.pxd.pxi"

# Following pxi files are part of the Aio module
include "_cygrpc/aio/iomgr/socket.pxd.pxi"
include "_cygrpc/aio/iomgr/timer.pxd.pxi"
include "_cygrpc/aio/iomgr/resolver.pxd.pxi"
include "_cygrpc/aio/completion_queue.pxd.pxi"
include "_cygrpc/aio/rpc_status.pxd.pxi"
include "_cygrpc/aio/grpc_aio.pxd.pxi"
include "_cygrpc/aio/callback_common.pxd.pxi"
include "_cygrpc/aio/call.pxd.pxi"
include "_cygrpc/aio/channel.pxd.pxi"
include "_cygrpc/aio/server.pxd.pxi"
