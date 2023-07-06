#ifndef ABSL_DEBUGGING_INTERNAL_STACKTRACE_AARCH64_INL_H_
#define ABSL_DEBUGGING_INTERNAL_STACKTRACE_AARCH64_INL_H_

// Generate stack tracer for aarch64

#if defined(__linux__)
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>
#endif

#include <atomic>
#include <cassert>
#include <cstdint>
#include <iostream>

#include "absl/base/attributes.h"
#include "absl/debugging/internal/address_is_readable.h"
#include "absl/debugging/internal/vdso_support.h"  // a no-op on non-elf or non-glibc systems
#include "absl/debugging/stacktrace.h"

static const size_t kUnknownFrameSize = 0;

#if defined(__linux__)
// Returns the address of the VDSO __kernel_rt_sigreturn function, if present.
static const unsigned char* GetKernelRtSigreturnAddress() {
  constexpr uintptr_t kImpossibleAddress = 1;
  ABSL_CONST_INIT static std::atomic<uintptr_t> memoized{kImpossibleAddress};
  uintptr_t address = memoized.load(std::memory_order_relaxed);
  if (address != kImpossibleAddress) {
    return reinterpret_cast<const unsigned char*>(address);
  }

  address = reinterpret_cast<uintptr_t>(nullptr);

#ifdef ABSL_HAVE_VDSO_SUPPORT
  absl::debugging_internal::VDSOSupport vdso;
  if (vdso.IsPresent()) {
    absl::debugging_internal::VDSOSupport::SymbolInfo symbol_info;
    auto lookup = [&](int type) {
      return vdso.LookupSymbol("__kernel_rt_sigreturn", "LINUX_2.6.39", type,
                               &symbol_info);
    };
    if ((!lookup(STT_FUNC) && !lookup(STT_NOTYPE)) ||
        symbol_info.address == nullptr) {
      // Unexpected: VDSO is present, yet the expected symbol is missing
      // or null.
      assert(false && "VDSO is present, but doesn't have expected symbol");
    } else {
      if (reinterpret_cast<uintptr_t>(symbol_info.address) !=
          kImpossibleAddress) {
        address = reinterpret_cast<uintptr_t>(symbol_info.address);
      } else {
        assert(false && "VDSO returned invalid address");
      }
    }
  }
#endif

  memoized.store(address, std::memory_order_relaxed);
  return reinterpret_cast<const unsigned char*>(address);
}
#endif  // __linux__

// Compute the size of a stack frame in [low..high).  We assume that
// low < high.  Return size of kUnknownFrameSize.
template<typename T>
static inline size_t ComputeStackFrameSize(const T* low,
                                           const T* high) {
  const char* low_char_ptr = reinterpret_cast<const char *>(low);
  const char* high_char_ptr = reinterpret_cast<const char *>(high);
  return low < high ? static_cast<size_t>(high_char_ptr - low_char_ptr)
                    : kUnknownFrameSize;
}

// Given a pointer to a stack frame, locate and return the calling
// stackframe, or return null if no stackframe can be found. Perform sanity
// checks (the strictness of which is controlled by the boolean parameter
// "STRICT_UNWINDING") to reduce the chance that a bad pointer is returned.
template<bool STRICT_UNWINDING, bool WITH_CONTEXT>
ABSL_ATTRIBUTE_NO_SANITIZE_ADDRESS  // May read random elements from stack.
ABSL_ATTRIBUTE_NO_SANITIZE_MEMORY   // May read random elements from stack.
static void **NextStackFrame(void **old_frame_pointer, const void *uc) {
  void **new_frame_pointer = reinterpret_cast<void**>(*old_frame_pointer);
  bool check_frame_size = true;

#if defined(__linux__)
  if (WITH_CONTEXT && uc != nullptr) {
    // Check to see if next frame's return address is __kernel_rt_sigreturn.
    if (old_frame_pointer[1] == GetKernelRtSigreturnAddress()) {
      const ucontext_t *ucv = static_cast<const ucontext_t *>(uc);
      // old_frame_pointer[0] is not suitable for unwinding, look at
      // ucontext to discover frame pointer before signal.
      void **const pre_signal_frame_pointer =
          reinterpret_cast<void **>(ucv->uc_mcontext.regs[29]);

      // Check that alleged frame pointer is actually readable. This is to
      // prevent "double fault" in case we hit the first fault due to e.g.
      // stack corruption.
      if (!absl::debugging_internal::AddressIsReadable(
              pre_signal_frame_pointer))
        return nullptr;

      // Alleged frame pointer is readable, use it for further unwinding.
      new_frame_pointer = pre_signal_frame_pointer;

      // Skip frame size check if we return from a signal. We may be using a
      // an alternate stack for signals.
      check_frame_size = false;
    }
  }
#endif

  // The frame pointer should be 8-byte aligned.
  if ((reinterpret_cast<uintptr_t>(new_frame_pointer) & 7) != 0)
    return nullptr;

  // Check frame size.  In strict mode, we assume frames to be under
  // 100,000 bytes.  In non-strict mode, we relax the limit to 1MB.
  if (check_frame_size) {
    const size_t max_size = STRICT_UNWINDING ? 100000 : 1000000;
    const size_t frame_size =
        ComputeStackFrameSize(old_frame_pointer, new_frame_pointer);
    if (frame_size == kUnknownFrameSize || frame_size > max_size)
      return nullptr;
  }

  return new_frame_pointer;
}

template <bool IS_STACK_FRAMES, bool IS_WITH_CONTEXT>
ABSL_ATTRIBUTE_NO_SANITIZE_ADDRESS  // May read random elements from stack.
ABSL_ATTRIBUTE_NO_SANITIZE_MEMORY   // May read random elements from stack.
static int UnwindImpl(void** result, int* sizes, int max_depth, int skip_count,
                      const void *ucp, int *min_dropped_frames) {
#ifdef __GNUC__
  void **frame_pointer = reinterpret_cast<void**>(__builtin_frame_address(0));
#else
# error reading stack point not yet supported on this platform.
#endif

  skip_count++;    // Skip the frame for this function.
  int n = 0;

  // The frame pointer points to low address of a frame.  The first 64-bit
  // word of a frame points to the next frame up the call chain, which normally
  // is just after the high address of the current frame.  The second word of
  // a frame contains return adress of to the caller.   To find a pc value
  // associated with the current frame, we need to go down a level in the call
  // chain.  So we remember return the address of the last frame seen.  This
  // does not work for the first stack frame, which belongs to UnwindImp() but
  // we skip the frame for UnwindImp() anyway.
  void* prev_return_address = nullptr;

  while (frame_pointer && n < max_depth) {
    // The absl::GetStackFrames routine is called when we are in some
    // informational context (the failure signal handler for example).
    // Use the non-strict unwinding rules to produce a stack trace
    // that is as complete as possible (even if it contains a few bogus
    // entries in some rare cases).
    void **next_frame_pointer =
        NextStackFrame<!IS_STACK_FRAMES, IS_WITH_CONTEXT>(frame_pointer, ucp);

    if (skip_count > 0) {
      skip_count--;
    } else {
      result[n] = prev_return_address;
      if (IS_STACK_FRAMES) {
        sizes[n] = static_cast<int>(
            ComputeStackFrameSize(frame_pointer, next_frame_pointer));
      }
      n++;
    }
    prev_return_address = frame_pointer[1];
    frame_pointer = next_frame_pointer;
  }
  if (min_dropped_frames != nullptr) {
    // Implementation detail: we clamp the max of frames we are willing to
    // count, so as not to spend too much time in the loop below.
    const int kMaxUnwind = 200;
    int num_dropped_frames = 0;
    for (int j = 0; frame_pointer != nullptr && j < kMaxUnwind; j++) {
      if (skip_count > 0) {
        skip_count--;
      } else {
        num_dropped_frames++;
      }
      frame_pointer =
          NextStackFrame<!IS_STACK_FRAMES, IS_WITH_CONTEXT>(frame_pointer, ucp);
    }
    *min_dropped_frames = num_dropped_frames;
  }
  return n;
}

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace debugging_internal {
bool StackTraceWorksForTest() {
  return true;
}
}  // namespace debugging_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_DEBUGGING_INTERNAL_STACKTRACE_AARCH64_INL_H_
