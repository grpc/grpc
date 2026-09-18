#!/bin/sh
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

set -e

cd "$(dirname "$0")/../../.."

#
# Prevent the use of synchronization and threading constructs from std:: since
# the code should be using grpc_core::Mutex, grpc::internal::Mutex,
# grpc_core::Thread, etc.
#

grep -EIrn \
    '\bstd::(mutex|condition_variable|lock_guard|unique_lock|thread|string_view)' \
    include/grpc include/grpcpp src/core src/cpp | \
    grep -Ev 'include/grpcpp/impl/sync.h|src/core/util/work_serializer.cc' | \
    diff - /dev/null

#
# Prevent the include of disallowed C++ headers.
#

grep -EIrn \
    '^#include (<mutex>|<condition_variable>|<thread>|<ratio>|<filesystem>|<future>|<system_error>)' \
    include/grpc include/grpcpp src/core src/cpp | \
    grep -Ev 'include/grpcpp/impl/sync.h|src/core/util/work_serializer.cc' | \
    diff - /dev/null

#
# Prevent the include of headers that shouldn't be used in tests.
#

grep -EIrn \
    '^#include (<pthread.h>)' \
    test | \
    diff - /dev/null

#
# Prevent the use of CHECK in this folder.
# ABSL_CHECK should be used instead
#

grep -EIrn \
    '\s(CHECK_EQ\(|CHECK_GE\(|CHECK_GT\(|CHECK_LE\(|CHECK_LT\(|CHECK_NE\(|CHECK_OK\(|CHECK_STRCASEEQ\(|CHECK_STRCASENE\(|CHECK_STREQ\(|CHECK_STRNE\(|DCHECK_EQ\(|DCHECK_GE\(|DCHECK_GT\(|DCHECK_LE\(|DCHECK_LT\(|DCHECK_NE\(|DCHECK_OK\(|DCHECK_STRCASEEQ\(|DCHECK_STRCASENE\(|DCHECK_STREQ\(|DCHECK_STRNE\(|QCHECK_EQ\(|QCHECK_GE\(|QCHECK_GT\(|QCHECK_LE\(|QCHECK_LT\(|QCHECK_NE\(|QCHECK_OK\(|QCHECK_STRCASEEQ\(|QCHECK_STRCASENE\(|QCHECK_STREQ\(|QCHECK_STRNE\()' \
    include | \
    diff - /dev/null

#
# Prevent the use of absl LOG in this folder.
# ABSL_LOG should be used instead
#

grep -EIrn \
    '\s(DLOG\(|DLOG_EVERY_N\(|DLOG_EVERY_N_SEC\(|DLOG_EVERY_POW_2\(|DLOG_FIRST_N\(|DLOG_IF\(|DLOG_IF_EVERY_N\(|DLOG_IF_EVERY_N_SEC\(|DLOG_IF_EVERY_POW_2\(|DLOG_IF_FIRST_N\(|DVLOG\(|LOG\(|LOG_EVERY_N\(|LOG_EVERY_N_SEC\(|LOG_EVERY_POW_2\(|LOG_FIRST_N\(|LOG_IF\(|LOG_IF_EVERY_N\(|LOG_IF_EVERY_N_SEC\(|LOG_IF_EVERY_POW_2\(|LOG_IF_FIRST_N\(|PLOG\(|PLOG_EVERY_N\(|PLOG_EVERY_N_SEC\(|PLOG_EVERY_POW_2\(|PLOG_FIRST_N\(|PLOG_IF\(|PLOG_IF_EVERY_N\(|PLOG_IF_EVERY_N_SEC\(|PLOG_IF_EVERY_POW_2\(|PLOG_IF_FIRST_N\(|VLOG\(|VLOG_EVERY_N\(|VLOG_EVERY_N_SEC\(|VLOG_EVERY_POW_2\(|VLOG_FIRST_N\()' \
    include | \
    diff - /dev/null
