/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_IMPL_CODEGEN_PORT_PLATFORM_H
#define GRPC_IMPL_CODEGEN_PORT_PLATFORM_H

/*
 * Define GPR_BACKWARDS_COMPATIBILITY_MODE to try harder to be ABI
 * compatible with older platforms (currently only on Linux)
 * Causes:
 *  - some libc calls to be gotten via dlsym
 *  - some syscalls to be made directly
 */

/* Get windows.h included everywhere (we need it) */
#if defined(_WIN64) || defined(WIN64) || defined(_WIN32) || defined(WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define GRPC_WIN32_LEAN_AND_MEAN_WAS_NOT_DEFINED
#define WIN32_LEAN_AND_MEAN
#endif /* WIN32_LEAN_AND_MEAN */

#ifndef NOMINMAX
#define GRPC_NOMINMX_WAS_NOT_DEFINED
#define NOMINMAX
#endif /* NOMINMAX */

#ifndef _WIN32_WINNT
#error \
    "Please compile grpc with _WIN32_WINNT of at least 0x600 (aka Windows Vista)"
#else /* !defined(_WIN32_WINNT) */
#if (_WIN32_WINNT < 0x0600)
#error \
    "Please compile grpc with _WIN32_WINNT of at least 0x600 (aka Windows Vista)"
#endif /* _WIN32_WINNT < 0x0600 */
#endif /* defined(_WIN32_WINNT) */

#include <windows.h>

#ifdef GRPC_WIN32_LEAN_AND_MEAN_WAS_NOT_DEFINED
#undef GRPC_WIN32_LEAN_AND_MEAN_WAS_NOT_DEFINED
#undef WIN32_LEAN_AND_MEAN
#endif /* GRPC_WIN32_LEAN_AND_MEAN_WAS_NOT_DEFINED */

#ifdef GRPC_NOMINMAX_WAS_NOT_DEFINED
#undef GRPC_NOMINMAX_WAS_NOT_DEFINED
#undef NOMINMAX
#endif /* GRPC_WIN32_LEAN_AND_MEAN_WAS_NOT_DEFINED */
#endif /* defined(_WIN64) || defined(WIN64) || defined(_WIN32) || \
          defined(WIN32) */

/* Override this file with one for your platform if you need to redefine
   things.  */

#if !defined(GPR_NO_AUTODETECT_PLATFORM)
#if defined(_WIN64) || defined(WIN64) || defined(_WIN32) || defined(WIN32)
#if defined(_WIN64) || defined(WIN64)
#define GPR_ARCH_64 1
#else
#define GPR_ARCH_32 1
#endif
#define GPR_PLATFORM_STRING "windows"
#define GPR_WINDOWS 1
#define GPR_WINDOWS_SUBPROCESS 1
#define GPR_WINDOWS_ENV
#ifdef __MSYS__
#define GPR_GETPID_IN_UNISTD_H 1
#define GPR_MSYS_TMPFILE
#define GPR_POSIX_LOG
#define GPR_POSIX_STRING
#define GPR_POSIX_TIME
#else
#define GPR_GETPID_IN_PROCESS_H 1
#define GPR_WINDOWS_TMPFILE
#define GPR_WINDOWS_LOG
#define GPR_WINDOWS_CRASH_HANDLER 1
#define GPR_WINDOWS_STRING
#define GPR_WINDOWS_TIME
#endif
#ifdef __GNUC__
#define GPR_GCC_ATOMIC 1
#define GPR_GCC_TLS 1
#else
#define GPR_WINDOWS_ATOMIC 1
#define GPR_MSVC_TLS 1
#endif
#elif defined(GPR_MANYLINUX1)
// TODO(atash): manylinux1 is just another __linux__ but with ancient
// libraries; it should be integrated with the `__linux__` definitions below.
#define GPR_PLATFORM_STRING "manylinux"
#define GPR_POSIX_CRASH_HANDLER 1
#define GPR_CPU_POSIX 1
#define GPR_GCC_ATOMIC 1
#define GPR_GCC_TLS 1
#define GPR_LINUX 1
#define GPR_LINUX_LOG 1
#define GPR_SUPPORT_CHANNELS_FROM_FD 1
#define GPR_LINUX_ENV 1
#define GPR_POSIX_TMPFILE 1
#define GPR_POSIX_STRING 1
#define GPR_POSIX_SUBPROCESS 1
#define GPR_POSIX_SYNC 1
#define GPR_POSIX_TIME 1
#define GPR_GETPID_IN_UNISTD_H 1
#ifdef _LP64
#define GPR_ARCH_64 1
#else /* _LP64 */
#define GPR_ARCH_32 1
#endif /* _LP64 */
#elif defined(ANDROID) || defined(__ANDROID__)
#define GPR_PLATFORM_STRING "android"
#define GPR_ANDROID 1
#ifdef _LP64
#define GPR_ARCH_64 1
#else /* _LP64 */
#define GPR_ARCH_32 1
#endif /* _LP64 */
#define GPR_CPU_POSIX 1
#define GPR_GCC_SYNC 1
#define GPR_GCC_TLS 1
#define GPR_POSIX_ENV 1
#define GPR_POSIX_TMPFILE 1
#define GPR_ANDROID_LOG 1
#define GPR_POSIX_STRING 1
#define GPR_POSIX_SUBPROCESS 1
#define GPR_POSIX_SYNC 1
#define GPR_POSIX_TIME 1
#define GPR_GETPID_IN_UNISTD_H 1
#define GPR_SUPPORT_CHANNELS_FROM_FD 1
#elif defined(__linux__)
#define GPR_PLATFORM_STRING "linux"
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
#define GPR_LINUX_LOG
#define GPR_SUPPORT_CHANNELS_FROM_FD 1
#define GPR_LINUX_ENV 1
#define GPR_POSIX_TMPFILE 1
#define GPR_POSIX_STRING 1
#define GPR_POSIX_SUBPROCESS 1
#define GPR_POSIX_SYNC 1
#define GPR_POSIX_TIME 1
#define GPR_GETPID_IN_UNISTD_H 1
#ifdef _LP64
#define GPR_ARCH_64 1
#else /* _LP64 */
#define GPR_ARCH_32 1
#endif /* _LP64 */
#ifdef __GLIBC__
#define GPR_POSIX_CRASH_HANDLER 1
#else /* musl libc */
#define GPR_MUSL_LIBC_COMPAT 1
#endif
#elif defined(__APPLE__)
#include <Availability.h>
#include <TargetConditionals.h>
#ifndef _BSD_SOURCE
#define _BSD_SOURCE
#endif
#if TARGET_OS_IPHONE
#define GPR_PLATFORM_STRING "ios"
#define GPR_CPU_IPHONE 1
#define GPR_PTHREAD_TLS 1
#else /* TARGET_OS_IPHONE */
#define GPR_PLATFORM_STRING "osx"
#ifdef __MAC_OS_X_VERSION_MIN_REQUIRED
#if __MAC_OS_X_VERSION_MIN_REQUIRED < __MAC_10_7
#define GPR_CPU_IPHONE 1
#define GPR_PTHREAD_TLS 1
#else /* __MAC_OS_X_VERSION_MIN_REQUIRED < __MAC_10_7 */
#define GPR_CPU_POSIX 1
#define GPR_GCC_TLS 1
#endif
#else /* __MAC_OS_X_VERSION_MIN_REQUIRED */
#define GPR_CPU_POSIX 1
#define GPR_GCC_TLS 1
#endif
#define GPR_POSIX_CRASH_HANDLER 1
#endif
#define GPR_APPLE 1
#define GPR_GCC_ATOMIC 1
#define GPR_POSIX_LOG 1
#define GPR_POSIX_ENV 1
#define GPR_POSIX_TMPFILE 1
#define GPR_POSIX_STRING 1
#define GPR_POSIX_SUBPROCESS 1
#define GPR_POSIX_SYNC 1
#define GPR_POSIX_TIME 1
#define GPR_GETPID_IN_UNISTD_H 1
#define GPR_SUPPORT_CHANNELS_FROM_FD 1
#ifdef _LP64
#define GPR_ARCH_64 1
#else /* _LP64 */
#define GPR_ARCH_32 1
#endif /* _LP64 */
#elif defined(__FreeBSD__)
#define GPR_PLATFORM_STRING "freebsd"
#ifndef _BSD_SOURCE
#define _BSD_SOURCE
#endif
#define GPR_FREEBSD 1
#define GPR_CPU_POSIX 1
#define GPR_GCC_ATOMIC 1
#define GPR_GCC_TLS 1
#define GPR_POSIX_LOG 1
#define GPR_POSIX_ENV 1
#define GPR_POSIX_TMPFILE 1
#define GPR_POSIX_STRING 1
#define GPR_POSIX_SUBPROCESS 1
#define GPR_POSIX_SYNC 1
#define GPR_POSIX_TIME 1
#define GPR_GETPID_IN_UNISTD_H 1
#define GPR_SUPPORT_CHANNELS_FROM_FD 1
#ifdef _LP64
#define GPR_ARCH_64 1
#else /* _LP64 */
#define GPR_ARCH_32 1
#endif /* _LP64 */
#elif defined(__native_client__)
#define GPR_PLATFORM_STRING "nacl"
#ifndef _BSD_SOURCE
#define _BSD_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define GPR_NACL 1
#define GPR_CPU_POSIX 1
#define GPR_GCC_ATOMIC 1
#define GPR_GCC_TLS 1
#define GPR_POSIX_LOG 1
#define GPR_POSIX_ENV 1
#define GPR_POSIX_TMPFILE 1
#define GPR_POSIX_STRING 1
#define GPR_POSIX_SUBPROCESS 1
#define GPR_POSIX_SYNC 1
#define GPR_POSIX_TIME 1
#define GPR_GETPID_IN_UNISTD_H 1
#ifdef _LP64
#define GPR_ARCH_64 1
#else /* _LP64 */
#define GPR_ARCH_32 1
#endif /* _LP64 */
#else
#error "Could not auto-detect platform"
#endif
#endif /* GPR_NO_AUTODETECT_PLATFORM */

#if defined(__has_include)
#if __has_include(<atomic>)
#define GRPC_HAS_CXX11_ATOMIC
#endif /* __has_include(<atomic>) */
#endif /* defined(__has_include) */

#ifndef GPR_PLATFORM_STRING
#warning "GPR_PLATFORM_STRING not auto-detected"
#define GPR_PLATFORM_STRING "unknown"
#endif

#ifdef GPR_GCOV
#undef GPR_FORBID_UNREACHABLE_CODE
#define GPR_FORBID_UNREACHABLE_CODE 1
#endif

#ifdef _MSC_VER
#ifdef _PYTHON_MSVC
// The Python 3.5 Windows runtime is missing InetNtop
#define GPR_WIN_INET_NTOP
#endif  // _PYTHON_MSVC
#if _MSC_VER < 1700
typedef __int8 int8_t;
typedef __int16 int16_t;
typedef __int32 int32_t;
typedef __int64 int64_t;
typedef unsigned __int8 uint8_t;
typedef unsigned __int16 uint16_t;
typedef unsigned __int32 uint32_t;
typedef unsigned __int64 uint64_t;
#else
#include <stdint.h>
#endif /* _MSC_VER < 1700 */
#else
#include <stdint.h>
#endif /* _MSC_VER */

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
#if defined(GPR_GCC_ATOMIC) + defined(GPR_GCC_SYNC) + \
        defined(GPR_WINDOWS_ATOMIC) !=                \
    1
#error Must define exactly one of GPR_GCC_ATOMIC, GPR_GCC_SYNC, GPR_WINDOWS_ATOMIC
#endif

#if defined(GPR_ARCH_32) + defined(GPR_ARCH_64) != 1
#error Must define exactly one of GPR_ARCH_32, GPR_ARCH_64
#endif

#if defined(GPR_CPU_LINUX) + defined(GPR_CPU_POSIX) + defined(GPR_WINDOWS) + \
        defined(GPR_CPU_IPHONE) + defined(GPR_CPU_CUSTOM) !=                 \
    1
#error Must define exactly one of GPR_CPU_LINUX, GPR_CPU_POSIX, GPR_WINDOWS, GPR_CPU_IPHONE, GPR_CPU_CUSTOM
#endif

#if defined(GPR_MSVC_TLS) + defined(GPR_GCC_TLS) + defined(GPR_PTHREAD_TLS) + \
        defined(GPR_CUSTOM_TLS) !=                                            \
    1
#error Must define exactly one of GPR_MSVC_TLS, GPR_GCC_TLS, GPR_PTHREAD_TLS, GPR_CUSTOM_TLS
#endif

/* maximum alignment needed for any type on this platform, rounded up to a
   power of two */
#define GPR_MAX_ALIGNMENT 16

#ifndef GRPC_ARES
#ifdef GPR_WINDOWS
#define GRPC_ARES 0
#else
#define GRPC_ARES 1
#endif
#endif

#ifndef GRPC_MUST_USE_RESULT
#if defined(__GNUC__) && !defined(__MINGW32__)
#define GRPC_MUST_USE_RESULT __attribute__((warn_unused_result))
#define GPR_ALIGN_STRUCT(n) __attribute__((aligned(n)))
#else
#define GRPC_MUST_USE_RESULT
#define GPR_ALIGN_STRUCT(n)
#endif
#endif

#ifndef GPR_PRINT_FORMAT_CHECK
#ifdef __GNUC__
#define GPR_PRINT_FORMAT_CHECK(FORMAT_STR, ARGS) \
  __attribute__((format(printf, FORMAT_STR, ARGS)))
#else
#define GPR_PRINT_FORMAT_CHECK(FORMAT_STR, ARGS)
#endif
#endif /* GPR_PRINT_FORMAT_CHECK */

#if GPR_FORBID_UNREACHABLE_CODE
#define GPR_UNREACHABLE_CODE(STATEMENT)
#else
#define GPR_UNREACHABLE_CODE(STATEMENT)             \
  do {                                              \
    gpr_log(GPR_ERROR, "Should never reach here."); \
    abort();                                        \
    STATEMENT;                                      \
  } while (0)
#endif /* GPR_FORBID_UNREACHABLE_CODE */

#ifndef GPRAPI
#define GPRAPI
#endif

#ifndef GRPCAPI
#define GRPCAPI GPRAPI
#endif

#ifndef CENSUSAPI
#define CENSUSAPI GRPCAPI
#endif

#endif /* GRPC_IMPL_CODEGEN_PORT_PLATFORM_H */
