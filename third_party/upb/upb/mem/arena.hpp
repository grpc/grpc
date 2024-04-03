// Protocol Buffers - Google's data interchange format
// Copyright 2023 Google LLC.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef UPB_MEM_ARENA_HPP_
#define UPB_MEM_ARENA_HPP_

#include <memory>

#include "upb/mem/arena.h"

namespace upb {

class Arena {
 public:
  // A simple arena with no initial memory block and the default allocator.
  Arena() : ptr_(upb_Arena_New(), upb_Arena_Free) {}
  Arena(char* initial_block, size_t size)
      : ptr_(upb_Arena_Init(initial_block, size, &upb_alloc_global),
             upb_Arena_Free) {}

  upb_Arena* ptr() const { return ptr_.get(); }

  void Fuse(Arena& other) { upb_Arena_Fuse(ptr(), other.ptr()); }

 protected:
  std::unique_ptr<upb_Arena, decltype(&upb_Arena_Free)> ptr_;
};

// InlinedArena seeds the arenas with a predefined amount of memory.  No
// heap memory will be allocated until the initial block is exceeded.
template <int N>
class InlinedArena : public Arena {
 public:
  InlinedArena() : Arena(initial_block_, N) {}
  ~InlinedArena() {
    // Explicitly destroy the arena now so that it does not outlive
    // initial_block_.
    ptr_.reset();
  }

 private:
  InlinedArena(const InlinedArena*) = delete;
  InlinedArena& operator=(const InlinedArena*) = delete;

  char initial_block_[N];
};

}  // namespace upb

#endif  // UPB_MEM_ARENA_HPP_
