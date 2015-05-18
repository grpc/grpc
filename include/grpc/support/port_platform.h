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

#ifndef GRPC_SUPPORT_PORT_PLATFORM_H
#define GRPC_SUPPORT_PORT_PLATFORM_H

/* Override this file with one for your platform if you need to redefine
   things.  */

#if !defined(GPR_NO_AUTODETECT_PLATFORM)
#if defined(_WIN64) || defined(WIN64)
#define GPR_WIN32 1
#define GPR_ARCH_64 1
#define GPR_GETPID_IN_PROCESS_H 1
#define GPR_WINSOCK_SOCKET 1
#ifdef __GNUC__
#define GPR_GCC_ATOMIC 1
#define GPR_GCC_TLS 1
#else
#define GPR_WIN32_ATOMIC 1
#define GPR_MSVC_TLS 1
#endif
#elif defined(_WIN32) || defined(WIN32)
#define GPR_ARCH_32 1
#define GPR_WIN32 1
#define GPR_GETPID_IN_PROCESS_H 1
#define GPR_WINSOCK_SOCKET 1
#ifdef __GNUC__
#define GPR_GCC_ATOMIC 1
#define GPR_GCC_TLS 1
#else
#define GPR_WIN32_ATOMIC 1
#define GPR_MSVC_TLS 1
#endif
#elif defined(ANDROID) || defined(__ANDROID__)
#define GPR_ANDROID 1
#define GPR_ARCH_32 1
#define GPR_CPU_LINUX 1
#define GPR_GCC_SYNC 1
#define GPR_GCC_TLS 1
#define GPR_POSIX_MULTIPOLL_WITH_POLL 1
#define GPR_POSIX_WAKEUP_FD 1
#define GPR_LINUX_EVENTFD 1
#define GPR_POSIX_SOCKET 1
#define GPR_POSIX_SOCKETADDR 1
#define GPR_POSIX_SOCKETUTILS 1
#define GPR_POSIX_ENV 1
#define GPR_POSIX_FILE 1
#define GPR_POSIX_STRING 1
#define GPR_POSIX_SUBPROCESS 1
#define GPR_POSIX_SYNC 1
#define GPR_POSIX_TIME 1
#define GPR_GETPID_IN_UNISTD_H 1
#define GPR_HAVE_MSG_NOSIGNAL 1
#elif defined(__linux__)
#ifndef _BSD_SOURCE
#define _BSD_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <features.h>
#define GPR_CPU_LINUX 1
#define GPR_GCC_ATOMIC 1
#define GPR_GCC_TLS 1
#define GPR_LINUX 1
#define GPR_LINUX_MULTIPOLL_WITH_EPOLL 1
#define GPR_POSIX_WAKEUP_FD 1
#define GPR_POSIX_SOCKET 1
#define GPR_POSIX_SOCKETADDR 1
#ifdef __GLIBC_PREREQ
#if __GLIBC_PREREQ(2, 9)
#define GPR_LINUX_EVENTFD 1
#endif
#if __GLIBC_PREREQ(2, 10)
#define GPR_LINUX_SOCKETUTILS 1
#endif
#if __GLIBC_PREREQ(2, 17)
#define GPR_LINUX_ENV 1
#endif
#endif
#ifndef GPR_LINUX_EVENTFD
#define GPR_POSIX_NO_SPECIAL_WAKEUP_FD 1
#endif
#ifndef GPR_LINUX_SOCKETUTILS
#define GPR_POSIX_SOCKETUTILS
#endif
#ifndef GPR_LINUX_ENV
#define GPR_POSIX_ENV 1
#endif
#define GPR_POSIX_FILE 1
#define GPR_POSIX_STRING 1
#define GPR_POSIX_SUBPROCESS 1
#define GPR_POSIX_SYNC 1
#define GPR_POSIX_TIME 1
#define GPR_GETPID_IN_UNISTD_H 1
#define GPR_HAVE_MSG_NOSIGNAL 1
#ifdef _LP64
#define GPR_ARCH_64 1
#else /* _LP64 */
#define GPR_ARCH_32 1
#endif /* _LP64 */
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#ifndef _BSD_SOURCE
#define _BSD_SOURCE
#endif
#if TARGET_OS_IPHONE
#define GPR_CPU_IPHONE 1
#define GPR_PTHREAD_TLS 1
#else /* TARGET_OS_IPHONE */
#define GPR_CPU_POSIX 1
#define GPR_GCC_TLS 1
#endif
#define GPR_GCC_ATOMIC 1
#define GPR_POSIX_LOG 1
#define GPR_POSIX_MULTIPOLL_WITH_POLL 1
#define GPR_POSIX_WAKEUP_FD 1
#define GPR_POSIX_NO_SPECIAL_WAKEUP_FD 1
#define GPR_POSIX_SOCKET 1
#define GPR_POSIX_SOCKETADDR 1
#define GPR_POSIX_SOCKETUTILS 1
#define GPR_POSIX_ENV 1
#define GPR_POSIX_FILE 1
#define GPR_POSIX_STRING 1
#define GPR_POSIX_SUBPROCESS 1
#define GPR_POSIX_SYNC 1
#define GPR_POSIX_TIME 1
#define GPR_GETPID_IN_UNISTD_H 1
#define GPR_HAVE_SO_NOSIGPIPE 1
#ifdef _LP64
#define GPR_ARCH_64 1
#else /* _LP64 */
#define GPR_ARCH_32 1
#endif /* _LP64 */
#elif defined(__FreeBSD__)
#ifndef _BSD_SOURCE
#define _BSD_SOURCE
#endif
#define GPR_CPU_POSIX 1
#define GPR_GCC_ATOMIC 1
#define GPR_GCC_TLS 1
#define GPR_POSIX_LOG 1
#define GPR_POSIX_MULTIPOLL_WITH_POLL 1
#define GPR_POSIX_WAKEUP_FD 1
#define GPR_POSIX_NO_SPECIAL_WAKEUP_FD 1
#define GPR_POSIX_SOCKET 1
#define GPR_POSIX_SOCKETADDR 1
#define GPR_POSIX_SOCKETUTILS 1
#define GPR_POSIX_ENV 1
#define GPR_POSIX_FILE 1
#define GPR_POSIX_STRING 1
#define GPR_POSIX_SUBPROCESS 1
#define GPR_POSIX_SYNC 1
#define GPR_POSIX_TIME 1
#define GPR_GETPID_IN_UNISTD_H 1
#define GPR_HAVE_SO_NOSIGPIPE 1
#ifdef _LP64
#define GPR_ARCH_64 1
#else /* _LP64 */
#define GPR_ARCH_32 1
#endif /* _LP64 */
#else
#error Could not auto-detect platform
#endif
#endif /* GPR_NO_AUTODETECT_PLATFORM */

/* For a common case, assume that the platform has a C99-like stdint.h */

#include <stdint.h>

/* Cache line alignment */
#ifndef GPR_CACHELINE_SIZE_LOG
#if defined(__i386__) || defined(__x86_64__)
#define GPR_CACHELINE_SIZE_LOG 6
#endif
#ifndef GPR_CACHELINE_SIZE_LOG
/* A reasonable default guess. Note that overestimates tend to waste more
   space, while underestimates tend to waste more time. */
#define GPR_CACHELINE_SIZE_LOG 6
#endif /* GPR_CACHELINE_SIZE_LOG */
#endif /* GPR_CACHELINE_SIZE_LOG */

#define GPR_CACHELINE_SIZE (1 << GPR_CACHELINE_SIZE_LOG)

/* scrub GCC_ATOMIC if it's not available on this compiler */
#if defined(GPR_GCC_ATOMIC) && !defined(__ATOMIC_RELAXED)
#undef GPR_GCC_ATOMIC
#define GPR_GCC_SYNC 1
#endif

/* Validate platform combinations */
#if defined(GPR_GCC_ATOMIC) + defined(GPR_GCC_SYNC) + defined(GPR_WIN32_ATOMIC) != 1
#error Must define exactly one of GPR_GCC_ATOMIC, GPR_GCC_SYNC, GPR_WIN32_ATOMIC
#endif

#if defined(GPR_ARCH_32) + defined(GPR_ARCH_64) != 1
#error Must define exactly one of GPR_ARCH_32, GPR_ARCH_64
#endif

#if defined(GPR_CPU_LINUX) + defined(GPR_CPU_POSIX) + defined(GPR_WIN32) + defined(GPR_CPU_IPHONE) + defined(GPR_CPU_CUSTOM) != 1
#error Must define exactly one of GPR_CPU_LINUX, GPR_CPU_POSIX, GPR_WIN32, GPR_CPU_IPHONE, GPR_CPU_CUSTOM
#endif

#if defined(GPR_POSIX_MULTIPOLL_WITH_POLL) && !defined(GPR_POSIX_SOCKET)
#error Must define GPR_POSIX_SOCKET to use GPR_POSIX_MULTIPOLL_WITH_POLL
#endif

#if defined(GPR_POSIX_SOCKET) + defined(GPR_WINSOCK_SOCKET) + defined(GPR_CUSTOM_SOCKET) != 1
#error Must define exactly one of GPR_POSIX_SOCKET, GPR_WINSOCK_SOCKET, GPR_CUSTOM_SOCKET
#endif

#if defined(GPR_MSVC_TLS) + defined(GPR_GCC_TLS) + defined(GPR_PTHREAD_TLS) + defined(GPR_CUSTOM_TLS) != 1
#error Must define exactly one of GPR_MSVC_TLS, GPR_GCC_TLS, GPR_PTHREAD_TLS, GPR_CUSTOM_TLS
#endif

typedef int16_t gpr_int16;
typedef int32_t gpr_int32;
typedef int64_t gpr_int64;
typedef uint8_t gpr_uint8;
typedef uint16_t gpr_uint16;
typedef uint32_t gpr_uint32;
typedef uint64_t gpr_uint64;
typedef intmax_t gpr_intmax;
typedef intptr_t gpr_intptr;
typedef uintmax_t gpr_uintmax;
typedef uintptr_t gpr_uintptr;

/* INT64_MAX is unavailable on some platforms. */
#define GPR_INT64_MAX (gpr_int64)(~(gpr_uint64)0 >> 1)

/* maximum alignment needed for any type on this platform, rounded up to a
   power of two */
#define GPR_MAX_ALIGNMENT 16

#endif  /* GRPC_SUPPORT_PORT_PLATFORM_H */
