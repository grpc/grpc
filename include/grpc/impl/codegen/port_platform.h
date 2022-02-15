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

// IWYU pragma: private, include <grpc/support/port_platform.h>

/*
 * Define GPR_BACKWARDS_COMPATIBILITY_MODE to try harder to be ABI
 * compatible with older platforms (currently only on Linux)
 * Causes:
 *  - some libc calls to be gotten via dlsym
 *  - some syscalls to be made directly
 */

// [[deprecated]] attribute is only available since C++14
#if __cplusplus >= 201402L
#define GRPC_DEPRECATED(reason) [[deprecated(reason)]]
#else
#define GRPC_DEPRECATED(reason)
#endif  // __cplusplus >= 201402L

/*
 * Defines GPR_ABSEIL_SYNC to use synchronization features from Abseil
 */
#ifndef GPR_ABSEIL_SYNC
#if defined(__APPLE__)
// This is disabled on Apple platforms because macos/grpc_basictests_c_cpp
// fails with this. https://github.com/grpc/grpc/issues/23661
#else
#define GPR_ABSEIL_SYNC 1
#endif
#endif  // GPR_ABSEIL_SYNC

/*
 * Defines GRPC_ERROR_IS_ABSEIL_STATUS to use absl::Status for grpc_error_handle
 */
#ifndef GRPC_ERROR_IS_ABSEIL_STATUS
// #define GRPC_ERROR_IS_ABSEIL_STATUS 1
#endif

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

#include <windows.h>

#ifndef _WIN32_WINNT
#error \
    "Please compile grpc with _WIN32_WINNT of at least 0x600 (aka Windows Vista)"
#else /* !defined(_WIN32_WINNT) */
#if (_WIN32_WINNT < 0x0600)
#error \
    "Please compile grpc with _WIN32_WINNT of at least 0x600 (aka Windows Vista)"
#endif /* _WIN32_WINNT < 0x0600 */
#endif /* defined(_WIN32_WINNT) */

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
#define GPR_WINDOWS_STAT
#define GPR_WINDOWS_STRING
#define GPR_WINDOWS_TIME
#endif
#ifdef __GNUC__
#define GPR_GCC_ATOMIC 1
#else
#define GPR_WINDOWS_ATOMIC 1
#endif
#elif defined(ANDROID) || defined(__ANDROID__)
#define GPR_PLATFORM_STRING "android"
#define GPR_ANDROID 1
#ifndef __ANDROID_API__
#error "__ANDROID_API__ must be defined for Android builds."
#endif
#if __ANDROID_API__ < 21
#error "Requires Android API v21 and above"
#endif
#define GPR_SUPPORT_BINDER_TRANSPORT 1
// TODO(apolcyn): re-evaluate support for c-ares
// on android after upgrading our c-ares dependency.
// See https://github.com/grpc/grpc/issues/18038.
#define GRPC_ARES 0
#ifdef _LP64
#define GPR_ARCH_64 1
#else /* _LP64 */
#define GPR_ARCH_32 1
#endif /* _LP64 */
#define GPR_CPU_POSIX 1
#define GPR_GCC_SYNC 1
#define GPR_POSIX_ENV 1
#define GPR_POSIX_TMPFILE 1
#define GPR_POSIX_STAT 1
#define GPR_ANDROID_LOG 1
#define GPR_POSIX_STRING 1
#define GPR_POSIX_SUBPROCESS 1
#define GPR_POSIX_SYNC 1
#define GPR_POSIX_TIME 1
#define GPR_HAS_PTHREAD_H 1
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
#define GPR_LINUX 1
#define GPR_LINUX_LOG
#define GPR_SUPPORT_CHANNELS_FROM_FD 1
#define GPR_LINUX_ENV 1
#define GPR_POSIX_TMPFILE 1
#define GPR_POSIX_STAT 1
#define GPR_POSIX_STRING 1
#define GPR_POSIX_SUBPROCESS 1
#define GPR_POSIX_SYNC 1
#define GPR_POSIX_TIME 1
#define GPR_HAS_PTHREAD_H 1
#define GPR_GETPID_IN_UNISTD_H 1
#ifdef _LP64
#define GPR_ARCH_64 1
#else /* _LP64 */
#define GPR_ARCH_32 1
#endif /* _LP64 */
#ifdef __GLIBC__
#define GPR_POSIX_CRASH_HANDLER 1
#ifdef __GLIBC_PREREQ
#if __GLIBC_PREREQ(2, 12)
#define GPR_LINUX_PTHREAD_NAME 1
#endif
#else
// musl libc & others
#define GPR_LINUX_PTHREAD_NAME 1
#endif
#include <linux/version.h>
#else /* musl libc */
#define GPR_MUSL_LIBC_COMPAT 1
#endif
#elif defined(__ASYLO__)
#define GPR_ARCH_64 1
#define GPR_CPU_POSIX 1
#define GPR_PLATFORM_STRING "asylo"
#define GPR_GCC_SYNC 1
#define GPR_POSIX_SYNC 1
#define GPR_POSIX_STRING 1
#define GPR_POSIX_LOG 1
#define GPR_POSIX_TIME 1
#define GPR_POSIX_ENV 1
#define GPR_ASYLO 1
#define GRPC_POSIX_SOCKET 1
#define GRPC_POSIX_SOCKETADDR
#define GRPC_POSIX_SOCKETUTILS 1
#define GRPC_TIMER_USE_GENERIC 1
#define GRPC_POSIX_NO_SPECIAL_WAKEUP_FD 1
#define GRPC_POSIX_WAKEUP_FD 1
#define GRPC_ARES 0
#define GPR_NO_AUTODETECT_PLATFORM 1
#elif defined(__APPLE__)
#include <Availability.h>
#include <TargetConditionals.h>
#ifndef _BSD_SOURCE
#define _BSD_SOURCE
#endif
#if TARGET_OS_IPHONE
#define GPR_PLATFORM_STRING "ios"
#define GPR_CPU_IPHONE 1
#define GRPC_CFSTREAM 1
/* the c-ares resolver isn't safe to enable on iOS */
#define GRPC_ARES 0
#else /* TARGET_OS_IPHONE */
#define GPR_PLATFORM_STRING "osx"
#define GPR_CPU_POSIX 1
#define GPR_POSIX_CRASH_HANDLER 1
#endif
#if !(defined(__has_feature) && __has_feature(cxx_thread_local))
#define GPR_PTHREAD_TLS 1
#endif
#define GPR_APPLE 1
#define GPR_GCC_ATOMIC 1
#define GPR_POSIX_LOG 1
#define GPR_POSIX_ENV 1
#define GPR_POSIX_TMPFILE 1
#define GPR_POSIX_STAT 1
#define GPR_POSIX_STRING 1
#define GPR_POSIX_SUBPROCESS 1
#define GPR_POSIX_SYNC 1
#define GPR_POSIX_TIME 1
#define GPR_HAS_PTHREAD_H 1
#define GPR_GETPID_IN_UNISTD_H 1
#ifndef GRPC_CFSTREAM
#define GPR_SUPPORT_CHANNELS_FROM_FD 1
#endif
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
#define GPR_POSIX_LOG 1
#define GPR_POSIX_ENV 1
#define GPR_POSIX_TMPFILE 1
#define GPR_POSIX_STAT 1
#define GPR_POSIX_STRING 1
#define GPR_POSIX_SUBPROCESS 1
#define GPR_POSIX_SYNC 1
#define GPR_POSIX_TIME 1
#define GPR_HAS_PTHREAD_H 1
#define GPR_GETPID_IN_UNISTD_H 1
#define GPR_SUPPORT_CHANNELS_FROM_FD 1
#ifdef _LP64
#define GPR_ARCH_64 1
#else /* _LP64 */
#define GPR_ARCH_32 1
#endif /* _LP64 */
#elif defined(__OpenBSD__)
#define GPR_PLATFORM_STRING "openbsd"
#ifndef _BSD_SOURCE
#define _BSD_SOURCE
#endif
#define GPR_OPENBSD 1
#define GPR_CPU_POSIX 1
#define GPR_GCC_ATOMIC 1
#define GPR_POSIX_LOG 1
#define GPR_POSIX_ENV 1
#define GPR_POSIX_TMPFILE 1
#define GPR_POSIX_STAT 1
#define GPR_POSIX_STRING 1
#define GPR_POSIX_SUBPROCESS 1
#define GPR_POSIX_SYNC 1
#define GPR_POSIX_TIME 1
#define GPR_HAS_PTHREAD_H 1
#define GPR_GETPID_IN_UNISTD_H 1
#define GPR_SUPPORT_CHANNELS_FROM_FD 1
#ifdef _LP64
#define GPR_ARCH_64 1
#else /* _LP64 */
#define GPR_ARCH_32 1
#endif /* _LP64 */
#elif defined(__sun) && defined(__SVR4)
#define GPR_PLATFORM_STRING "solaris"
#define GPR_SOLARIS 1
#define GPR_CPU_POSIX 1
#define GPR_GCC_ATOMIC 1
#define GPR_POSIX_LOG 1
#define GPR_POSIX_ENV 1
#define GPR_POSIX_TMPFILE 1
#define GPR_POSIX_STAT 1
#define GPR_POSIX_STRING 1
#define GPR_POSIX_SUBPROCESS 1
#define GPR_POSIX_SYNC 1
#define GPR_POSIX_TIME 1
#define GPR_HAS_PTHREAD_H 1
#define GPR_GETPID_IN_UNISTD_H 1
#ifdef _LP64
#define GPR_ARCH_64 1
#else /* _LP64 */
#define GPR_ARCH_32 1
#endif /* _LP64 */
#elif defined(_AIX)
#define GPR_PLATFORM_STRING "aix"
#ifndef _ALL_SOURCE
#define _ALL_SOURCE
#endif
#define GPR_AIX 1
#define GPR_CPU_POSIX 1
#define GPR_GCC_ATOMIC 1
#define GPR_POSIX_LOG 1
#define GPR_POSIX_ENV 1
#define GPR_POSIX_TMPFILE 1
#define GPR_POSIX_STAT 1
#define GPR_POSIX_STRING 1
#define GPR_POSIX_SUBPROCESS 1
#define GPR_POSIX_SYNC 1
#define GPR_POSIX_TIME 1
#define GPR_HAS_PTHREAD_H 1
#define GPR_GETPID_IN_UNISTD_H 1
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
#define GPR_POSIX_LOG 1
#define GPR_POSIX_ENV 1
#define GPR_POSIX_TMPFILE 1
#define GPR_POSIX_STAT 1
#define GPR_POSIX_STRING 1
#define GPR_POSIX_SUBPROCESS 1
#define GPR_POSIX_SYNC 1
#define GPR_POSIX_TIME 1
#define GPR_HAS_PTHREAD_H 1
#define GPR_GETPID_IN_UNISTD_H 1
#ifdef _LP64
#define GPR_ARCH_64 1
#else /* _LP64 */
#define GPR_ARCH_32 1
#endif /* _LP64 */
#elif defined(__Fuchsia__)
#define GRPC_ARES 0
#define GPR_FUCHSIA 1
#define GPR_ARCH_64 1
#define GPR_PLATFORM_STRING "fuchsia"
#include <features.h>
// Specifying musl libc affects wrap_memcpy.c. It causes memmove() to be
// invoked.
#define GPR_MUSL_LIBC_COMPAT 1
#define GPR_CPU_POSIX 1
#define GPR_GCC_ATOMIC 1
#define GPR_POSIX_LOG 1
#define GPR_POSIX_SYNC 1
#define GPR_POSIX_ENV 1
#define GPR_POSIX_TMPFILE 1
#define GPR_POSIX_STAT 1
#define GPR_POSIX_SUBPROCESS 1
#define GPR_POSIX_SYNC 1
#define GPR_POSIX_STRING 1
#define GPR_POSIX_TIME 1
#define GPR_HAS_PTHREAD_H 1
#define GPR_GETPID_IN_UNISTD_H 1
#define GRPC_ROOT_PEM_PATH "/config/ssl/cert.pem"
#else
#error "Could not auto-detect platform"
#endif
#endif /* GPR_NO_AUTODETECT_PLATFORM */

#if defined(GPR_BACKWARDS_COMPATIBILITY_MODE)
/*
 * For backward compatibility mode, reset _FORTIFY_SOURCE to prevent
 * a library from having non-standard symbols such as __asprintf_chk.
 * This helps non-glibc systems such as alpine using musl to find symbols.
 */
#if defined(_FORTIFY_SOURCE) && _FORTIFY_SOURCE > 0
#undef _FORTIFY_SOURCE
#define _FORTIFY_SOURCE 0
#endif
#endif

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

/* Type of cycle clock implementation */
#ifdef GPR_LINUX
/* Disable cycle clock by default.
   TODO(soheil): enable when we support fallback for unstable cycle clocks.
#if defined(__i386__)
#define GPR_CYCLE_COUNTER_RDTSC_32 1
#elif defined(__x86_64__) || defined(__amd64__)
#define GPR_CYCLE_COUNTER_RDTSC_64 1
#else
#define GPR_CYCLE_COUNTER_FALLBACK 1
#endif
*/
#define GPR_CYCLE_COUNTER_FALLBACK 1
#else
#define GPR_CYCLE_COUNTER_FALLBACK 1
#endif /* GPR_LINUX */

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

/* maximum alignment needed for any type on this platform, rounded up to a
   power of two */
#define GPR_MAX_ALIGNMENT 16

#ifndef GRPC_ARES
#define GRPC_ARES 1
#endif

#ifndef GRPC_IF_NAMETOINDEX
#define GRPC_IF_NAMETOINDEX 1
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

#ifndef GRPC_UNUSED
#if defined(__GNUC__) && !defined(__MINGW32__)
#define GRPC_UNUSED __attribute__((unused))
#else
#define GRPC_UNUSED
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

#ifndef GPR_HAS_CPP_ATTRIBUTE
#ifdef __has_cpp_attribute
#define GPR_HAS_CPP_ATTRIBUTE(a) __has_cpp_attribute(a)
#else
#define GPR_HAS_CPP_ATTRIBUTE(a) 0
#endif
#endif /* GPR_HAS_CPP_ATTRIBUTE */

#ifndef GPR_HAS_ATTRIBUTE
#ifdef __has_attribute
#define GPR_HAS_ATTRIBUTE(a) __has_attribute(a)
#else
#define GPR_HAS_ATTRIBUTE(a) 0
#endif
#endif /* GPR_HAS_ATTRIBUTE */

#ifndef GPR_HAS_FEATURE
#ifdef __has_feature
#define GPR_HAS_FEATURE(a) __has_feature(a)
#else
#define GPR_HAS_FEATURE(a) 0
#endif
#endif /* GPR_HAS_FEATURE */

#ifndef GPR_ATTRIBUTE_NOINLINE
#if GPR_HAS_ATTRIBUTE(noinline) || (defined(__GNUC__) && !defined(__clang__))
#define GPR_ATTRIBUTE_NOINLINE __attribute__((noinline))
#define GPR_HAS_ATTRIBUTE_NOINLINE 1
#else
#define GPR_ATTRIBUTE_NOINLINE
#endif
#endif /* GPR_ATTRIBUTE_NOINLINE */

#ifndef GPR_NO_UNIQUE_ADDRESS
#if GPR_HAS_CPP_ATTRIBUTE(no_unique_address)
#define GPR_NO_UNIQUE_ADDRESS [[no_unique_address]]
#else
#define GPR_NO_UNIQUE_ADDRESS
#endif
#endif /* GPR_NO_UNIQUE_ADDRESS */

#ifndef GRPC_DEPRECATED
#if GPR_HAS_CPP_ATTRIBUTE(deprecated)
#define GRPC_DEPRECATED(reason) [[deprecated(reason)]]
#else
#define GRPC_DEPRECATED(reason)
#endif
#endif /* GRPC_DEPRECATED */

#ifndef GPR_ATTRIBUTE_WEAK
/* Attribute weak is broken on LLVM/windows:
 * https://bugs.llvm.org/show_bug.cgi?id=37598 */
#if (GPR_HAS_ATTRIBUTE(weak) || (defined(__GNUC__) && !defined(__clang__))) && \
    !(defined(__llvm__) && defined(_WIN32))
#define GPR_ATTRIBUTE_WEAK __attribute__((weak))
#define GPR_HAS_ATTRIBUTE_WEAK 1
#else
#define GPR_ATTRIBUTE_WEAK
#endif
#endif /* GPR_ATTRIBUTE_WEAK */

#ifndef GPR_ATTRIBUTE_NO_TSAN /* (1) */
#if GPR_HAS_FEATURE(thread_sanitizer)
#define GPR_ATTRIBUTE_NO_TSAN __attribute__((no_sanitize("thread")))
#endif                        /* GPR_HAS_FEATURE */
#ifndef GPR_ATTRIBUTE_NO_TSAN /* (2) */
#define GPR_ATTRIBUTE_NO_TSAN
#endif /* GPR_ATTRIBUTE_NO_TSAN (2) */
#endif /* GPR_ATTRIBUTE_NO_TSAN (1) */

/* GRPC_TSAN_ENABLED will be defined, when compiled with thread sanitizer. */
#ifndef GRPC_TSAN_SUPPRESSED
#if defined(__SANITIZE_THREAD__)
#define GRPC_TSAN_ENABLED
#elif GPR_HAS_FEATURE(thread_sanitizer)
#define GRPC_TSAN_ENABLED
#endif
#endif

/* GRPC_ASAN_ENABLED will be defined, when compiled with address sanitizer. */
#ifndef GRPC_ASAN_SUPPRESSED
#if defined(__SANITIZE_ADDRESS__)
#define GRPC_ASAN_ENABLED
#elif GPR_HAS_FEATURE(address_sanitizer)
#define GRPC_ASAN_ENABLED
#endif
#endif

/* GRPC_ALLOW_EXCEPTIONS should be 0 or 1 if exceptions are allowed or not */
#ifndef GRPC_ALLOW_EXCEPTIONS
#ifdef GPR_WINDOWS
#if defined(_MSC_VER) && defined(_CPPUNWIND)
#define GRPC_ALLOW_EXCEPTIONS 1
#elif defined(__EXCEPTIONS)
#define GRPC_ALLOW_EXCEPTIONS 1
#else
#define GRPC_ALLOW_EXCEPTIONS 0
#endif
#else /* GPR_WINDOWS */
#ifdef __EXCEPTIONS
#define GRPC_ALLOW_EXCEPTIONS 1
#else /* __EXCEPTIONS */
#define GRPC_ALLOW_EXCEPTIONS 0
#endif /* __EXCEPTIONS */
#endif /* __GPR_WINDOWS */
#endif /* GRPC_ALLOW_EXCEPTIONS */

/* Use GPR_LIKELY only in cases where you are sure that a certain outcome is the
 * most likely. Ideally, also collect performance numbers to justify the claim.
 */
#ifdef __GNUC__
#define GPR_LIKELY(x) __builtin_expect((x), 1)
#define GPR_UNLIKELY(x) __builtin_expect((x), 0)
#else /* __GNUC__ */
#define GPR_LIKELY(x) (x)
#define GPR_UNLIKELY(x) (x)
#endif /* __GNUC__ */

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

/* Selectively enable EventEngine on specific platforms. This default can be
 * overridden using the GRPC_USE_EVENT_ENGINE compiler flag.
 */
#ifndef GRPC_USE_EVENT_ENGINE
/* Not enabled by default on any platforms yet. (2021.06) */
#elif GRPC_USE_EVENT_ENGINE == 0
/* Building with `-DGRPC_USE_EVENT_ENGINE=0` will override the default. */
#undef GRPC_USE_EVENT_ENGINE
#endif /* GRPC_USE_EVENT_ENGINE */

#ifdef GRPC_USE_EVENT_ENGINE
#undef GPR_SUPPORT_CHANNELS_FROM_FD
#define GRPC_ARES 0
#endif /* GRPC_USE_EVENT_ENGINE */

#define GRPC_CALLBACK_API_NONEXPERIMENTAL

/* clang 11 with msan miscompiles destruction of [[no_unique_address]] members
 * of zero size - for a repro see:
 * test/core/compiler_bugs/miscompile_with_no_unique_address_test.cc
 */
#ifdef __clang__
#if __clang__ && __clang_major__ <= 11 && __has_feature(memory_sanitizer)
#undef GPR_NO_UNIQUE_ADDRESS
#define GPR_NO_UNIQUE_ADDRESS
#endif
#endif

#endif /* GRPC_IMPL_CODEGEN_PORT_PLATFORM_H */
