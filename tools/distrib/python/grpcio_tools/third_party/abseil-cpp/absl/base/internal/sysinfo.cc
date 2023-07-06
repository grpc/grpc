// Copyright 2017 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/base/internal/sysinfo.h"

#include "absl/base/attributes.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#ifdef __linux__
#include <sys/syscall.h>
#endif

#if defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/sysctl.h>
#endif

#if defined(__myriad2__)
#include <rtems.h>
#endif

#include <string.h>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <limits>
#include <thread>  // NOLINT(build/c++11)
#include <utility>
#include <vector>

#include "absl/base/call_once.h"
#include "absl/base/config.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/internal/spinlock.h"
#include "absl/base/internal/unscaledcycleclock.h"
#include "absl/base/thread_annotations.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace base_internal {

namespace {

#if defined(_WIN32)

// Returns number of bits set in `bitMask`
DWORD Win32CountSetBits(ULONG_PTR bitMask) {
  for (DWORD bitSetCount = 0; ; ++bitSetCount) {
    if (bitMask == 0) return bitSetCount;
    bitMask &= bitMask - 1;
  }
}

// Returns the number of logical CPUs using GetLogicalProcessorInformation(), or
// 0 if the number of processors is not available or can not be computed.
// https://docs.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getlogicalprocessorinformation
int Win32NumCPUs() {
#pragma comment(lib, "kernel32.lib")
  using Info = SYSTEM_LOGICAL_PROCESSOR_INFORMATION;

  DWORD info_size = sizeof(Info);
  Info* info(static_cast<Info*>(malloc(info_size)));
  if (info == nullptr) return 0;

  bool success = GetLogicalProcessorInformation(info, &info_size);
  if (!success && GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
    free(info);
    info = static_cast<Info*>(malloc(info_size));
    if (info == nullptr) return 0;
    success = GetLogicalProcessorInformation(info, &info_size);
  }

  DWORD logicalProcessorCount = 0;
  if (success) {
    Info* ptr = info;
    DWORD byteOffset = 0;
    while (byteOffset + sizeof(Info) <= info_size) {
      switch (ptr->Relationship) {
        case RelationProcessorCore:
          logicalProcessorCount += Win32CountSetBits(ptr->ProcessorMask);
          break;

        case RelationNumaNode:
        case RelationCache:
        case RelationProcessorPackage:
          // Ignore other entries
          break;

        default:
          // Ignore unknown entries
          break;
      }
      byteOffset += sizeof(Info);
      ptr++;
    }
  }
  free(info);
  return static_cast<int>(logicalProcessorCount);
}

#endif

}  // namespace

static int GetNumCPUs() {
#if defined(__myriad2__)
  return 1;
#elif defined(_WIN32)
  const int hardware_concurrency = Win32NumCPUs();
  return hardware_concurrency ? hardware_concurrency : 1;
#elif defined(_AIX)
  return sysconf(_SC_NPROCESSORS_ONLN);
#else
  // Other possibilities:
  //  - Read /sys/devices/system/cpu/online and use cpumask_parse()
  //  - sysconf(_SC_NPROCESSORS_ONLN)
  return static_cast<int>(std::thread::hardware_concurrency());
#endif
}

#if defined(_WIN32)

static double GetNominalCPUFrequency() {
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_APP) && \
    !WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
  // UWP apps don't have access to the registry and currently don't provide an
  // API informing about CPU nominal frequency.
  return 1.0;
#else
#pragma comment(lib, "advapi32.lib")  // For Reg* functions.
  HKEY key;
  // Use the Reg* functions rather than the SH functions because shlwapi.dll
  // pulls in gdi32.dll which makes process destruction much more costly.
  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                    "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0,
                    KEY_READ, &key) == ERROR_SUCCESS) {
    DWORD type = 0;
    DWORD data = 0;
    DWORD data_size = sizeof(data);
    auto result = RegQueryValueExA(key, "~MHz", 0, &type,
                                   reinterpret_cast<LPBYTE>(&data), &data_size);
    RegCloseKey(key);
    if (result == ERROR_SUCCESS && type == REG_DWORD &&
        data_size == sizeof(data)) {
      return data * 1e6;  // Value is MHz.
    }
  }
  return 1.0;
#endif  // WINAPI_PARTITION_APP && !WINAPI_PARTITION_DESKTOP
}

#elif defined(CTL_HW) && defined(HW_CPU_FREQ)

static double GetNominalCPUFrequency() {
  unsigned freq;
  size_t size = sizeof(freq);
  int mib[2] = {CTL_HW, HW_CPU_FREQ};
  if (sysctl(mib, 2, &freq, &size, nullptr, 0) == 0) {
    return static_cast<double>(freq);
  }
  return 1.0;
}

#else

// Helper function for reading a long from a file. Returns true if successful
// and the memory location pointed to by value is set to the value read.
static bool ReadLongFromFile(const char *file, long *value) {
  bool ret = false;
  int fd = open(file, O_RDONLY | O_CLOEXEC);
  if (fd != -1) {
    char line[1024];
    char *err;
    memset(line, '\0', sizeof(line));
    ssize_t len;
    do {
      len = read(fd, line, sizeof(line) - 1);
    } while (len < 0 && errno == EINTR);
    if (len <= 0) {
      ret = false;
    } else {
      const long temp_value = strtol(line, &err, 10);
      if (line[0] != '\0' && (*err == '\n' || *err == '\0')) {
        *value = temp_value;
        ret = true;
      }
    }
    close(fd);
  }
  return ret;
}

#if defined(ABSL_INTERNAL_UNSCALED_CYCLECLOCK_FREQUENCY_IS_CPU_FREQUENCY)

// Reads a monotonic time source and returns a value in
// nanoseconds. The returned value uses an arbitrary epoch, not the
// Unix epoch.
static int64_t ReadMonotonicClockNanos() {
  struct timespec t;
#ifdef CLOCK_MONOTONIC_RAW
  int rc = clock_gettime(CLOCK_MONOTONIC_RAW, &t);
#else
  int rc = clock_gettime(CLOCK_MONOTONIC, &t);
#endif
  if (rc != 0) {
    perror("clock_gettime() failed");
    abort();
  }
  return int64_t{t.tv_sec} * 1000000000 + t.tv_nsec;
}

class UnscaledCycleClockWrapperForInitializeFrequency {
 public:
  static int64_t Now() { return base_internal::UnscaledCycleClock::Now(); }
};

struct TimeTscPair {
  int64_t time;  // From ReadMonotonicClockNanos().
  int64_t tsc;   // From UnscaledCycleClock::Now().
};

// Returns a pair of values (monotonic kernel time, TSC ticks) that
// approximately correspond to each other.  This is accomplished by
// doing several reads and picking the reading with the lowest
// latency.  This approach is used to minimize the probability that
// our thread was preempted between clock reads.
static TimeTscPair GetTimeTscPair() {
  int64_t best_latency = std::numeric_limits<int64_t>::max();
  TimeTscPair best;
  for (int i = 0; i < 10; ++i) {
    int64_t t0 = ReadMonotonicClockNanos();
    int64_t tsc = UnscaledCycleClockWrapperForInitializeFrequency::Now();
    int64_t t1 = ReadMonotonicClockNanos();
    int64_t latency = t1 - t0;
    if (latency < best_latency) {
      best_latency = latency;
      best.time = t0;
      best.tsc = tsc;
    }
  }
  return best;
}

// Measures and returns the TSC frequency by taking a pair of
// measurements approximately `sleep_nanoseconds` apart.
static double MeasureTscFrequencyWithSleep(int sleep_nanoseconds) {
  auto t0 = GetTimeTscPair();
  struct timespec ts;
  ts.tv_sec = 0;
  ts.tv_nsec = sleep_nanoseconds;
  while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {}
  auto t1 = GetTimeTscPair();
  double elapsed_ticks = t1.tsc - t0.tsc;
  double elapsed_time = (t1.time - t0.time) * 1e-9;
  return elapsed_ticks / elapsed_time;
}

// Measures and returns the TSC frequency by calling
// MeasureTscFrequencyWithSleep(), doubling the sleep interval until the
// frequency measurement stabilizes.
static double MeasureTscFrequency() {
  double last_measurement = -1.0;
  int sleep_nanoseconds = 1000000;  // 1 millisecond.
  for (int i = 0; i < 8; ++i) {
    double measurement = MeasureTscFrequencyWithSleep(sleep_nanoseconds);
    if (measurement * 0.99 < last_measurement &&
        last_measurement < measurement * 1.01) {
      // Use the current measurement if it is within 1% of the
      // previous measurement.
      return measurement;
    }
    last_measurement = measurement;
    sleep_nanoseconds *= 2;
  }
  return last_measurement;
}

#endif  // ABSL_INTERNAL_UNSCALED_CYCLECLOCK_FREQUENCY_IS_CPU_FREQUENCY

static double GetNominalCPUFrequency() {
  long freq = 0;

  // Google's production kernel has a patch to export the TSC
  // frequency through sysfs. If the kernel is exporting the TSC
  // frequency use that. There are issues where cpuinfo_max_freq
  // cannot be relied on because the BIOS may be exporting an invalid
  // p-state (on x86) or p-states may be used to put the processor in
  // a new mode (turbo mode). Essentially, those frequencies cannot
  // always be relied upon. The same reasons apply to /proc/cpuinfo as
  // well.
  if (ReadLongFromFile("/sys/devices/system/cpu/cpu0/tsc_freq_khz", &freq)) {
    return freq * 1e3;  // Value is kHz.
  }

#if defined(ABSL_INTERNAL_UNSCALED_CYCLECLOCK_FREQUENCY_IS_CPU_FREQUENCY)
  // On these platforms, the TSC frequency is the nominal CPU
  // frequency.  But without having the kernel export it directly
  // though /sys/devices/system/cpu/cpu0/tsc_freq_khz, there is no
  // other way to reliably get the TSC frequency, so we have to
  // measure it ourselves.  Some CPUs abuse cpuinfo_max_freq by
  // exporting "fake" frequencies for implementing new features. For
  // example, Intel's turbo mode is enabled by exposing a p-state
  // value with a higher frequency than that of the real TSC
  // rate. Because of this, we prefer to measure the TSC rate
  // ourselves on i386 and x86-64.
  return MeasureTscFrequency();
#else

  // If CPU scaling is in effect, we want to use the *maximum*
  // frequency, not whatever CPU speed some random processor happens
  // to be using now.
  if (ReadLongFromFile("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq",
                       &freq)) {
    return freq * 1e3;  // Value is kHz.
  }

  return 1.0;
#endif  // !ABSL_INTERNAL_UNSCALED_CYCLECLOCK_FREQUENCY_IS_CPU_FREQUENCY
}

#endif

ABSL_CONST_INIT static once_flag init_num_cpus_once;
ABSL_CONST_INIT static int num_cpus = 0;

// NumCPUs() may be called before main() and before malloc is properly
// initialized, therefore this must not allocate memory.
int NumCPUs() {
  base_internal::LowLevelCallOnce(
      &init_num_cpus_once, []() { num_cpus = GetNumCPUs(); });
  return num_cpus;
}

// A default frequency of 0.0 might be dangerous if it is used in division.
ABSL_CONST_INIT static once_flag init_nominal_cpu_frequency_once;
ABSL_CONST_INIT static double nominal_cpu_frequency = 1.0;

// NominalCPUFrequency() may be called before main() and before malloc is
// properly initialized, therefore this must not allocate memory.
double NominalCPUFrequency() {
  base_internal::LowLevelCallOnce(
      &init_nominal_cpu_frequency_once,
      []() { nominal_cpu_frequency = GetNominalCPUFrequency(); });
  return nominal_cpu_frequency;
}

#if defined(_WIN32)

pid_t GetTID() {
  return pid_t{GetCurrentThreadId()};
}

#elif defined(__linux__)

#ifndef SYS_gettid
#define SYS_gettid __NR_gettid
#endif

pid_t GetTID() {
  return static_cast<pid_t>(syscall(SYS_gettid));
}

#elif defined(__akaros__)

pid_t GetTID() {
  // Akaros has a concept of "vcore context", which is the state the program
  // is forced into when we need to make a user-level scheduling decision, or
  // run a signal handler.  This is analogous to the interrupt context that a
  // CPU might enter if it encounters some kind of exception.
  //
  // There is no current thread context in vcore context, but we need to give
  // a reasonable answer if asked for a thread ID (e.g., in a signal handler).
  // Thread 0 always exists, so if we are in vcore context, we return that.
  //
  // Otherwise, we know (since we are using pthreads) that the uthread struct
  // current_uthread is pointing to is the first element of a
  // struct pthread_tcb, so we extract and return the thread ID from that.
  //
  // TODO(dcross): Akaros anticipates moving the thread ID to the uthread
  // structure at some point. We should modify this code to remove the cast
  // when that happens.
  if (in_vcore_context())
    return 0;
  return reinterpret_cast<struct pthread_tcb *>(current_uthread)->id;
}

#elif defined(__myriad2__)

pid_t GetTID() {
  uint32_t tid;
  rtems_task_ident(RTEMS_SELF, 0, &tid);
  return tid;
}

#else

// Fallback implementation of GetTID using pthread_getspecific.
ABSL_CONST_INIT static once_flag tid_once;
ABSL_CONST_INIT static pthread_key_t tid_key;
ABSL_CONST_INIT static absl::base_internal::SpinLock tid_lock(
    absl::kConstInit, base_internal::SCHEDULE_KERNEL_ONLY);

// We set a bit per thread in this array to indicate that an ID is in
// use. ID 0 is unused because it is the default value returned by
// pthread_getspecific().
ABSL_CONST_INIT static std::vector<uint32_t> *tid_array
    ABSL_GUARDED_BY(tid_lock) = nullptr;
static constexpr int kBitsPerWord = 32;  // tid_array is uint32_t.

// Returns the TID to tid_array.
static void FreeTID(void *v) {
  intptr_t tid = reinterpret_cast<intptr_t>(v);
  intptr_t word = tid / kBitsPerWord;
  uint32_t mask = ~(1u << (tid % kBitsPerWord));
  absl::base_internal::SpinLockHolder lock(&tid_lock);
  assert(0 <= word && static_cast<size_t>(word) < tid_array->size());
  (*tid_array)[static_cast<size_t>(word)] &= mask;
}

static void InitGetTID() {
  if (pthread_key_create(&tid_key, FreeTID) != 0) {
    // The logging system calls GetTID() so it can't be used here.
    perror("pthread_key_create failed");
    abort();
  }

  // Initialize tid_array.
  absl::base_internal::SpinLockHolder lock(&tid_lock);
  tid_array = new std::vector<uint32_t>(1);
  (*tid_array)[0] = 1;  // ID 0 is never-allocated.
}

// Return a per-thread small integer ID from pthread's thread-specific data.
pid_t GetTID() {
  absl::call_once(tid_once, InitGetTID);

  intptr_t tid = reinterpret_cast<intptr_t>(pthread_getspecific(tid_key));
  if (tid != 0) {
    return static_cast<pid_t>(tid);
  }

  int bit;  // tid_array[word] = 1u << bit;
  size_t word;
  {
    // Search for the first unused ID.
    absl::base_internal::SpinLockHolder lock(&tid_lock);
    // First search for a word in the array that is not all ones.
    word = 0;
    while (word < tid_array->size() && ~(*tid_array)[word] == 0) {
      ++word;
    }
    if (word == tid_array->size()) {
      tid_array->push_back(0);  // No space left, add kBitsPerWord more IDs.
    }
    // Search for a zero bit in the word.
    bit = 0;
    while (bit < kBitsPerWord && (((*tid_array)[word] >> bit) & 1) != 0) {
      ++bit;
    }
    tid =
        static_cast<intptr_t>((word * kBitsPerWord) + static_cast<size_t>(bit));
    (*tid_array)[word] |= 1u << bit;  // Mark the TID as allocated.
  }

  if (pthread_setspecific(tid_key, reinterpret_cast<void *>(tid)) != 0) {
    perror("pthread_setspecific failed");
    abort();
  }

  return static_cast<pid_t>(tid);
}

#endif

// GetCachedTID() caches the thread ID in thread-local storage (which is a
// userspace construct) to avoid unnecessary system calls. Without this caching,
// it can take roughly 98ns, while it takes roughly 1ns with this caching.
pid_t GetCachedTID() {
#ifdef ABSL_HAVE_THREAD_LOCAL
  static thread_local pid_t thread_id = GetTID();
  return thread_id;
#else
  return GetTID();
#endif  // ABSL_HAVE_THREAD_LOCAL
}

}  // namespace base_internal
ABSL_NAMESPACE_END
}  // namespace absl
