
/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#ifndef GPR_C_
#define GPR_C_

#ifdef __cplusplus
extern "C" {
#endif


#include "src/core/profiling/basic_timers.c"
#include "src/core/profiling/stap_timers.c"
#include "src/core/support/alloc.c"
#include "src/core/support/avl.c"
#include "src/core/support/cmdline.c"
#include "src/core/support/cpu_iphone.c"
#include "src/core/support/cpu_linux.c"
#include "src/core/support/cpu_posix.c"
#include "src/core/support/cpu_windows.c"
#include "src/core/support/env_linux.c"
#include "src/core/support/env_posix.c"
#include "src/core/support/env_win32.c"
#include "src/core/support/file.c"
#include "src/core/support/file_posix.c"
#include "src/core/support/file_win32.c"
#include "src/core/support/histogram.c"
#include "src/core/support/host_port.c"
#include "src/core/support/log.c"
#include "src/core/support/log_android.c"
#include "src/core/support/log_linux.c"
#include "src/core/support/log_posix.c"
#include "src/core/support/log_win32.c"
#include "src/core/support/murmur_hash.c"
#include "src/core/support/slice.c"
#include "src/core/support/slice_buffer.c"
#include "src/core/support/stack_lockfree.c"
#include "src/core/support/string.c"
#include "src/core/support/string_posix.c"
#include "src/core/support/string_win32.c"
#include "src/core/support/subprocess_posix.c"
#include "src/core/support/sync.c"
#include "src/core/support/sync_posix.c"
#include "src/core/support/sync_win32.c"
#include "src/core/support/thd.c"
#include "src/core/support/thd_posix.c"
#include "src/core/support/thd_win32.c"
#include "src/core/support/time.c"
#include "src/core/support/time_posix.c"
#include "src/core/support/time_precise.c"
#include "src/core/support/time_win32.c"
#include "src/core/support/tls_pthread.c"

#ifdef __cplusplus
}
#endif

#endif /* GPR_C_ */
