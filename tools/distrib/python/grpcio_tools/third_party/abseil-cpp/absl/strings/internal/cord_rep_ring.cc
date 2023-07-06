// Copyright 2020 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "absl/strings/internal/cord_rep_ring.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <string>

#include "absl/base/internal/raw_logging.h"
#include "absl/base/internal/throw_delegate.h"
#include "absl/base/macros.h"
#include "absl/container/inlined_vector.h"
#include "absl/strings/internal/cord_internal.h"
#include "absl/strings/internal/cord_rep_consume.h"
#include "absl/strings/internal/cord_rep_flat.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {

namespace {

using index_type = CordRepRing::index_type;

enum class Direction { kForward, kReversed };

inline bool IsFlatOrExternal(CordRep* rep) {
  return rep->IsFlat() || rep->IsExternal();
}

// Verifies that n + extra <= kMaxCapacity: throws std::length_error otherwise.
inline void CheckCapacity(size_t n, size_t extra) {
  if (ABSL_PREDICT_FALSE(extra > CordRepRing::kMaxCapacity - n)) {
    base_internal::ThrowStdLengthError("Maximum capacity exceeded");
  }
}

// Creates a flat from the provided string data, allocating up to `extra`
// capacity in the returned flat depending on kMaxFlatLength limitations.
// Requires `len` to be less or equal to `kMaxFlatLength`
CordRepFlat* CreateFlat(const char* s, size_t n, size_t extra = 0) {  // NOLINT
  assert(n <= kMaxFlatLength);
  auto* rep = CordRepFlat::New(n + extra);
  rep->length = n;
  memcpy(rep->Data(), s, n);
  return rep;
}

// Unrefs the entries in `[head, tail)`.
// Requires all entries to be a FLAT or EXTERNAL node.
void UnrefEntries(const CordRepRing* rep, index_type head, index_type tail) {
  rep->ForEach(head, tail, [rep](index_type ix) {
    CordRep* child = rep->entry_child(ix);
    if (!child->refcount.Decrement()) {
      if (child->tag >= FLAT) {
        CordRepFlat::Delete(child->flat());
      } else {
        CordRepExternal::Delete(child->external());
      }
    }
  });
}

}  // namespace

std::ostream& operator<<(std::ostream& s, const CordRepRing& rep) {
  // Note: 'pos' values are defined as size_t (for overflow reasons), but that
  // prints really awkward for small prepended values such as -5. ssize_t is not
  // portable (POSIX), so we use ptrdiff_t instead to cast to signed values.
  s << "  CordRepRing(" << &rep << ", length = " << rep.length
    << ", head = " << rep.head_ << ", tail = " << rep.tail_
    << ", cap = " << rep.capacity_ << ", rc = " << rep.refcount.Get()
    << ", begin_pos_ = " << static_cast<ptrdiff_t>(rep.begin_pos_) << ") {\n";
  CordRepRing::index_type head = rep.head();
  do {
    CordRep* child = rep.entry_child(head);
    s << " entry[" << head << "] length = " << rep.entry_length(head)
      << ", child " << child << ", clen = " << child->length
      << ", tag = " << static_cast<int>(child->tag)
      << ", rc = " << child->refcount.Get()
      << ", offset = " << rep.entry_data_offset(head)
      << ", end_pos = " << static_cast<ptrdiff_t>(rep.entry_end_pos(head))
      << "\n";
    head = rep.advance(head);
  } while (head != rep.tail());
  return s << "}\n";
}

void CordRepRing::AddDataOffset(index_type index, size_t n) {
  entry_data_offset()[index] += static_cast<offset_type>(n);
}

void CordRepRing::SubLength(index_type index, size_t n) {
  entry_end_pos()[index] -= n;
}

class CordRepRing::Filler {
 public:
  Filler(CordRepRing* rep, index_type pos) : rep_(rep), head_(pos), pos_(pos) {}

  index_type head() const { return head_; }
  index_type pos() const { return pos_; }

  void Add(CordRep* child, size_t offset, pos_type end_pos) {
    rep_->entry_end_pos()[pos_] = end_pos;
    rep_->entry_child()[pos_] = child;
    rep_->entry_data_offset()[pos_] = static_cast<offset_type>(offset);
    pos_ = rep_->advance(pos_);
  }

 private:
  CordRepRing* rep_;
  index_type head_;
  index_type pos_;
};

#ifdef ABSL_INTERNAL_NEED_REDUNDANT_CONSTEXPR_DECL
constexpr size_t CordRepRing::kMaxCapacity;
#endif

bool CordRepRing::IsValid(std::ostream& output) const {
  if (capacity_ == 0) {
    output << "capacity == 0";
    return false;
  }

  if (head_ >= capacity_ || tail_ >= capacity_) {
    output << "head " << head_ << " and/or tail " << tail_ << "exceed capacity "
           << capacity_;
    return false;
  }

  const index_type back = retreat(tail_);
  size_t pos_length = Distance(begin_pos_, entry_end_pos(back));
  if (pos_length != length) {
    output << "length " << length << " does not match positional length "
           << pos_length << " from begin_pos " << begin_pos_ << " and entry["
           << back << "].end_pos " << entry_end_pos(back);
    return false;
  }

  index_type head = head_;
  pos_type begin_pos = begin_pos_;
  do {
    pos_type end_pos = entry_end_pos(head);
    size_t entry_length = Distance(begin_pos, end_pos);
    if (entry_length == 0) {
      output << "entry[" << head << "] has an invalid length " << entry_length
             << " from begin_pos " << begin_pos << " and end_pos " << end_pos;
      return false;
    }

    CordRep* child = entry_child(head);
    if (child == nullptr) {
      output << "entry[" << head << "].child == nullptr";
      return false;
    }
    if (child->tag < FLAT && child->tag != EXTERNAL) {
      output << "entry[" << head << "].child has an invalid tag "
             << static_cast<int>(child->tag);
      return false;
    }

    size_t offset = entry_data_offset(head);
    if (offset >= child->length || entry_length > child->length - offset) {
      output << "entry[" << head << "] has offset " << offset
             << " and entry length " << entry_length
             << " which are outside of the child's length of " << child->length;
      return false;
    }

    begin_pos = end_pos;
    head = advance(head);
  } while (head != tail_);

  return true;
}

#ifdef EXTRA_CORD_RING_VALIDATION
CordRepRing* CordRepRing::Validate(CordRepRing* rep, const char* file,
                                   int line) {
  if (!rep->IsValid(std::cerr)) {
    std::cerr << "\nERROR: CordRepRing corrupted";
    if (line) std::cerr << " at line " << line;
    if (file) std::cerr << " in file " << file;
    std::cerr << "\nContent = " << *rep;
    abort();
  }
  return rep;
}
#endif  // EXTRA_CORD_RING_VALIDATION

CordRepRing* CordRepRing::New(size_t capacity, size_t extra) {
  CheckCapacity(capacity, extra);

  size_t size = AllocSize(capacity += extra);
  void* mem = ::operator new(size);
  auto* rep = new (mem) CordRepRing(static_cast<index_type>(capacity));
  rep->tag = RING;
  rep->capacity_ = static_cast<index_type>(capacity);
  rep->begin_pos_ = 0;
  return rep;
}

void CordRepRing::SetCapacityForTesting(size_t capacity) {
  // Adjust for the changed layout
  assert(capacity <= capacity_);
  assert(head() == 0 || head() < tail());
  memmove(Layout::Partial(capacity).Pointer<1>(data_) + head(),
          Layout::Partial(capacity_).Pointer<1>(data_) + head(),
          entries() * sizeof(Layout::ElementType<1>));
  memmove(Layout::Partial(capacity, capacity).Pointer<2>(data_) + head(),
          Layout::Partial(capacity_, capacity_).Pointer<2>(data_) + head(),
          entries() * sizeof(Layout::ElementType<2>));
  capacity_ = static_cast<index_type>(capacity);
}

void CordRepRing::Delete(CordRepRing* rep) {
  assert(rep != nullptr && rep->IsRing());
#if defined(__cpp_sized_deallocation)
  size_t size = AllocSize(rep->capacity_);
  rep->~CordRepRing();
  ::operator delete(rep, size);
#else
  rep->~CordRepRing();
  ::operator delete(rep);
#endif
}

void CordRepRing::Destroy(CordRepRing* rep) {
  UnrefEntries(rep, rep->head(), rep->tail());
  Delete(rep);
}

template <bool ref>
void CordRepRing::Fill(const CordRepRing* src, index_type head,
                       index_type tail) {
  this->length = src->length;
  head_ = 0;
  tail_ = advance(0, src->entries(head, tail));
  begin_pos_ = src->begin_pos_;

  // TODO(mvels): there may be opportunities here for large buffers.
  auto* dst_pos = entry_end_pos();
  auto* dst_child = entry_child();
  auto* dst_offset = entry_data_offset();
  src->ForEach(head, tail, [&](index_type index) {
    *dst_pos++ = src->entry_end_pos(index);
    CordRep* child = src->entry_child(index);
    *dst_child++ = ref ? CordRep::Ref(child) : child;
    *dst_offset++ = src->entry_data_offset(index);
  });
}

CordRepRing* CordRepRing::Copy(CordRepRing* rep, index_type head,
                               index_type tail, size_t extra) {
  CordRepRing* newrep = CordRepRing::New(rep->entries(head, tail), extra);
  newrep->Fill<true>(rep, head, tail);
  CordRep::Unref(rep);
  return newrep;
}

CordRepRing* CordRepRing::Mutable(CordRepRing* rep, size_t extra) {
  // Get current number of entries, and check for max capacity.
  size_t entries = rep->entries();

  if (!rep->refcount.IsOne()) {
    return Copy(rep, rep->head(), rep->tail(), extra);
  } else if (entries + extra > rep->capacity()) {
    const size_t min_grow = rep->capacity() + rep->capacity() / 2;
    const size_t min_extra = (std::max)(extra, min_grow - entries);
    CordRepRing* newrep = CordRepRing::New(entries, min_extra);
    newrep->Fill<false>(rep, rep->head(), rep->tail());
    CordRepRing::Delete(rep);
    return newrep;
  } else {
    return rep;
  }
}

Span<char> CordRepRing::GetAppendBuffer(size_t size) {
  assert(refcount.IsOne());
  index_type back = retreat(tail_);
  CordRep* child = entry_child(back);
  if (child->tag >= FLAT && child->refcount.IsOne()) {
    size_t capacity = child->flat()->Capacity();
    pos_type end_pos = entry_end_pos(back);
    size_t data_offset = entry_data_offset(back);
    size_t entry_length = Distance(entry_begin_pos(back), end_pos);
    size_t used = data_offset + entry_length;
    if (size_t n = (std::min)(capacity - used, size)) {
      child->length = data_offset + entry_length + n;
      entry_end_pos()[back] = end_pos + n;
      this->length += n;
      return {child->flat()->Data() + used, n};
    }
  }
  return {nullptr, 0};
}

Span<char> CordRepRing::GetPrependBuffer(size_t size) {
  assert(refcount.IsOne());
  CordRep* child = entry_child(head_);
  size_t data_offset = entry_data_offset(head_);
  if (data_offset && child->refcount.IsOne() && child->tag >= FLAT) {
    size_t n = (std::min)(data_offset, size);
    this->length += n;
    begin_pos_ -= n;
    data_offset -= n;
    entry_data_offset()[head_] = static_cast<offset_type>(data_offset);
    return {child->flat()->Data() + data_offset, n};
  }
  return {nullptr, 0};
}

CordRepRing* CordRepRing::CreateFromLeaf(CordRep* child, size_t offset,
                                         size_t len, size_t extra) {
  CordRepRing* rep = CordRepRing::New(1, extra);
  rep->head_ = 0;
  rep->tail_ = rep->advance(0);
  rep->length = len;
  rep->entry_end_pos()[0] = len;
  rep->entry_child()[0] = child;
  rep->entry_data_offset()[0] = static_cast<offset_type>(offset);
  return Validate(rep);
}

CordRepRing* CordRepRing::CreateSlow(CordRep* child, size_t extra) {
  CordRepRing* rep = nullptr;
  Consume(child, [&](CordRep* child_arg, size_t offset, size_t len) {
    if (IsFlatOrExternal(child_arg)) {
      rep = rep ? AppendLeaf(rep, child_arg, offset, len)
                : CreateFromLeaf(child_arg, offset, len, extra);
    } else if (rep) {
      rep = AddRing<AddMode::kAppend>(rep, child_arg->ring(), offset, len);
    } else if (offset == 0 && child_arg->length == len) {
      rep = Mutable(child_arg->ring(), extra);
    } else {
      rep = SubRing(child_arg->ring(), offset, len, extra);
    }
  });
  return Validate(rep, nullptr, __LINE__);
}

CordRepRing* CordRepRing::Create(CordRep* child, size_t extra) {
  size_t length = child->length;
  if (IsFlatOrExternal(child)) {
    return CreateFromLeaf(child, 0, length, extra);
  }
  if (child->IsRing()) {
    return Mutable(child->ring(), extra);
  }
  return CreateSlow(child, extra);
}

template <CordRepRing::AddMode mode>
CordRepRing* CordRepRing::AddRing(CordRepRing* rep, CordRepRing* ring,
                                  size_t offset, size_t len) {
  assert(offset < ring->length);
  constexpr bool append = mode == AddMode::kAppend;
  Position head = ring->Find(offset);
  Position tail = ring->FindTail(head.index, offset + len);
  const index_type entries = ring->entries(head.index, tail.index);

  rep = Mutable(rep, entries);

  // The delta for making ring[head].end_pos into 'len - offset'
  const pos_type delta_length =
      (append ? rep->begin_pos_ + rep->length : rep->begin_pos_ - len) -
      ring->entry_begin_pos(head.index) - head.offset;

  // Start filling at `tail`, or `entries` before `head`
  Filler filler(rep, append ? rep->tail_ : rep->retreat(rep->head_, entries));

  if (ring->refcount.IsOne()) {
    // Copy entries from source stealing the ref and adjusting the end position.
    // Commit the filler as this is no-op.
    ring->ForEach(head.index, tail.index, [&](index_type ix) {
      filler.Add(ring->entry_child(ix), ring->entry_data_offset(ix),
                 ring->entry_end_pos(ix) + delta_length);
    });

    // Unref entries we did not copy over, and delete source.
    if (head.index != ring->head_) UnrefEntries(ring, ring->head_, head.index);
    if (tail.index != ring->tail_) UnrefEntries(ring, tail.index, ring->tail_);
    CordRepRing::Delete(ring);
  } else {
    ring->ForEach(head.index, tail.index, [&](index_type ix) {
      CordRep* child = ring->entry_child(ix);
      filler.Add(child, ring->entry_data_offset(ix),
                 ring->entry_end_pos(ix) + delta_length);
      CordRep::Ref(child);
    });
    CordRepRing::Unref(ring);
  }

  if (head.offset) {
    // Increase offset of first 'source' entry appended or prepended.
    // This is always the entry in `filler.head()`
    rep->AddDataOffset(filler.head(), head.offset);
  }

  if (tail.offset) {
    // Reduce length of last 'source' entry appended or prepended.
    // This is always the entry tailed by `filler.pos()`
    rep->SubLength(rep->retreat(filler.pos()), tail.offset);
  }

  // Commit changes
  rep->length += len;
  if (append) {
    rep->tail_ = filler.pos();
  } else {
    rep->head_ = filler.head();
    rep->begin_pos_ -= len;
  }

  return Validate(rep);
}

CordRepRing* CordRepRing::AppendSlow(CordRepRing* rep, CordRep* child) {
  Consume(child, [&rep](CordRep* child_arg, size_t offset, size_t len) {
    if (child_arg->IsRing()) {
      rep = AddRing<AddMode::kAppend>(rep, child_arg->ring(), offset, len);
    } else {
      rep = AppendLeaf(rep, child_arg, offset, len);
    }
  });
  return rep;
}

CordRepRing* CordRepRing::AppendLeaf(CordRepRing* rep, CordRep* child,
                                     size_t offset, size_t len) {
  rep = Mutable(rep, 1);
  index_type back = rep->tail_;
  const pos_type begin_pos = rep->begin_pos_ + rep->length;
  rep->tail_ = rep->advance(rep->tail_);
  rep->length += len;
  rep->entry_end_pos()[back] = begin_pos + len;
  rep->entry_child()[back] = child;
  rep->entry_data_offset()[back] = static_cast<offset_type>(offset);
  return Validate(rep, nullptr, __LINE__);
}

CordRepRing* CordRepRing::Append(CordRepRing* rep, CordRep* child) {
  size_t length = child->length;
  if (IsFlatOrExternal(child)) {
    return AppendLeaf(rep, child, 0, length);
  }
  if (child->IsRing()) {
    return AddRing<AddMode::kAppend>(rep, child->ring(), 0, length);
  }
  return AppendSlow(rep, child);
}

CordRepRing* CordRepRing::PrependSlow(CordRepRing* rep, CordRep* child) {
  ReverseConsume(child, [&](CordRep* child_arg, size_t offset, size_t len) {
    if (IsFlatOrExternal(child_arg)) {
      rep = PrependLeaf(rep, child_arg, offset, len);
    } else {
      rep = AddRing<AddMode::kPrepend>(rep, child_arg->ring(), offset, len);
    }
  });
  return Validate(rep);
}

CordRepRing* CordRepRing::PrependLeaf(CordRepRing* rep, CordRep* child,
                                      size_t offset, size_t len) {
  rep = Mutable(rep, 1);
  index_type head = rep->retreat(rep->head_);
  pos_type end_pos = rep->begin_pos_;
  rep->head_ = head;
  rep->length += len;
  rep->begin_pos_ -= len;
  rep->entry_end_pos()[head] = end_pos;
  rep->entry_child()[head] = child;
  rep->entry_data_offset()[head] = static_cast<offset_type>(offset);
  return Validate(rep);
}

CordRepRing* CordRepRing::Prepend(CordRepRing* rep, CordRep* child) {
  size_t length = child->length;
  if (IsFlatOrExternal(child)) {
    return PrependLeaf(rep, child, 0, length);
  }
  if (child->IsRing()) {
    return AddRing<AddMode::kPrepend>(rep, child->ring(), 0, length);
  }
  return PrependSlow(rep, child);
}

CordRepRing* CordRepRing::Append(CordRepRing* rep, absl::string_view data,
                                 size_t extra) {
  if (rep->refcount.IsOne()) {
    Span<char> avail = rep->GetAppendBuffer(data.length());
    if (!avail.empty()) {
      memcpy(avail.data(), data.data(), avail.length());
      data.remove_prefix(avail.length());
    }
  }
  if (data.empty()) return Validate(rep);

  const size_t flats = (data.length() - 1) / kMaxFlatLength + 1;
  rep = Mutable(rep, flats);

  Filler filler(rep, rep->tail_);
  pos_type pos = rep->begin_pos_ + rep->length;

  while (data.length() >= kMaxFlatLength) {
    auto* flat = CreateFlat(data.data(), kMaxFlatLength);
    filler.Add(flat, 0, pos += kMaxFlatLength);
    data.remove_prefix(kMaxFlatLength);
  }

  if (data.length()) {
    auto* flat = CreateFlat(data.data(), data.length(), extra);
    filler.Add(flat, 0, pos += data.length());
  }

  rep->length = pos - rep->begin_pos_;
  rep->tail_ = filler.pos();

  return Validate(rep);
}

CordRepRing* CordRepRing::Prepend(CordRepRing* rep, absl::string_view data,
                                  size_t extra) {
  if (rep->refcount.IsOne()) {
    Span<char> avail = rep->GetPrependBuffer(data.length());
    if (!avail.empty()) {
      const char* tail = data.data() + data.length() - avail.length();
      memcpy(avail.data(), tail, avail.length());
      data.remove_suffix(avail.length());
    }
  }
  if (data.empty()) return rep;

  const size_t flats = (data.length() - 1) / kMaxFlatLength + 1;
  rep = Mutable(rep, flats);
  pos_type pos = rep->begin_pos_;
  Filler filler(rep, rep->retreat(rep->head_, static_cast<index_type>(flats)));

  size_t first_size = data.size() - (flats - 1) * kMaxFlatLength;
  CordRepFlat* flat = CordRepFlat::New(first_size + extra);
  flat->length = first_size + extra;
  memcpy(flat->Data() + extra, data.data(), first_size);
  data.remove_prefix(first_size);
  filler.Add(flat, extra, pos);
  pos -= first_size;

  while (!data.empty()) {
    assert(data.size() >= kMaxFlatLength);
    flat = CreateFlat(data.data(), kMaxFlatLength);
    filler.Add(flat, 0, pos);
    pos -= kMaxFlatLength;
    data.remove_prefix(kMaxFlatLength);
  }

  rep->head_ = filler.head();
  rep->length += rep->begin_pos_ - pos;
  rep->begin_pos_ = pos;

  return Validate(rep);
}

// 32 entries is 32 * sizeof(pos_type) = 4 cache lines on x86
static constexpr index_type kBinarySearchThreshold = 32;
static constexpr index_type kBinarySearchEndCount = 8;

template <bool wrap>
CordRepRing::index_type CordRepRing::FindBinary(index_type head,
                                                index_type tail,
                                                size_t offset) const {
  index_type count = tail + (wrap ? capacity_ : 0) - head;
  do {
    count = (count - 1) / 2;
    assert(count < entries(head, tail_));
    index_type mid = wrap ? advance(head, count) : head + count;
    index_type after_mid = wrap ? advance(mid) : mid + 1;
    bool larger = (offset >= entry_end_offset(mid));
    head = larger ? after_mid : head;
    tail = larger ? tail : mid;
    assert(head != tail);
  } while (ABSL_PREDICT_TRUE(count > kBinarySearchEndCount));
  return head;
}

CordRepRing::Position CordRepRing::FindSlow(index_type head,
                                            size_t offset) const {
  index_type tail = tail_;

  // Binary search until we are good for linear search
  // Optimize for branchless / non wrapping ops
  if (tail > head) {
    index_type count = tail - head;
    if (count > kBinarySearchThreshold) {
      head = FindBinary<false>(head, tail, offset);
    }
  } else {
    index_type count = capacity_ + tail - head;
    if (count > kBinarySearchThreshold) {
      head = FindBinary<true>(head, tail, offset);
    }
  }

  pos_type pos = entry_begin_pos(head);
  pos_type end_pos = entry_end_pos(head);
  while (offset >= Distance(begin_pos_, end_pos)) {
    head = advance(head);
    pos = end_pos;
    end_pos = entry_end_pos(head);
  }

  return {head, offset - Distance(begin_pos_, pos)};
}

CordRepRing::Position CordRepRing::FindTailSlow(index_type head,
                                                size_t offset) const {
  index_type tail = tail_;
  const size_t tail_offset = offset - 1;

  // Binary search until we are good for linear search
  // Optimize for branchless / non wrapping ops
  if (tail > head) {
    index_type count = tail - head;
    if (count > kBinarySearchThreshold) {
      head = FindBinary<false>(head, tail, tail_offset);
    }
  } else {
    index_type count = capacity_ + tail - head;
    if (count > kBinarySearchThreshold) {
      head = FindBinary<true>(head, tail, tail_offset);
    }
  }

  size_t end_offset = entry_end_offset(head);
  while (tail_offset >= end_offset) {
    head = advance(head);
    end_offset = entry_end_offset(head);
  }

  return {advance(head), end_offset - offset};
}

char CordRepRing::GetCharacter(size_t offset) const {
  assert(offset < length);

  Position pos = Find(offset);
  size_t data_offset = entry_data_offset(pos.index) + pos.offset;
  return GetRepData(entry_child(pos.index))[data_offset];
}

CordRepRing* CordRepRing::SubRing(CordRepRing* rep, size_t offset,
                                  size_t len, size_t extra) {
  assert(offset <= rep->length);
  assert(offset <= rep->length - len);

  if (len == 0) {
    CordRep::Unref(rep);
    return nullptr;
  }

  // Find position of first byte
  Position head = rep->Find(offset);
  Position tail = rep->FindTail(head.index, offset + len);
  const size_t new_entries = rep->entries(head.index, tail.index);

  if (rep->refcount.IsOne() && extra <= (rep->capacity() - new_entries)) {
    // We adopt a privately owned rep and no extra entries needed.
    if (head.index != rep->head_) UnrefEntries(rep, rep->head_, head.index);
    if (tail.index != rep->tail_) UnrefEntries(rep, tail.index, rep->tail_);
    rep->head_ = head.index;
    rep->tail_ = tail.index;
  } else {
    // Copy subset to new rep
    rep = Copy(rep, head.index, tail.index, extra);
    head.index = rep->head_;
    tail.index = rep->tail_;
  }

  // Adjust begin_pos and length
  rep->length = len;
  rep->begin_pos_ += offset;

  // Adjust head and tail blocks
  if (head.offset) {
    rep->AddDataOffset(head.index, head.offset);
  }
  if (tail.offset) {
    rep->SubLength(rep->retreat(tail.index), tail.offset);
  }

  return Validate(rep);
}

CordRepRing* CordRepRing::RemovePrefix(CordRepRing* rep, size_t len,
                                       size_t extra) {
  assert(len <= rep->length);
  if (len == rep->length) {
    CordRep::Unref(rep);
    return nullptr;
  }

  Position head = rep->Find(len);
  if (rep->refcount.IsOne()) {
    if (head.index != rep->head_) UnrefEntries(rep, rep->head_, head.index);
    rep->head_ = head.index;
  } else {
    rep = Copy(rep, head.index, rep->tail_, extra);
    head.index = rep->head_;
  }

  // Adjust begin_pos and length
  rep->length -= len;
  rep->begin_pos_ += len;

  // Adjust head block
  if (head.offset) {
    rep->AddDataOffset(head.index, head.offset);
  }

  return Validate(rep);
}

CordRepRing* CordRepRing::RemoveSuffix(CordRepRing* rep, size_t len,
                                       size_t extra) {
  assert(len <= rep->length);

  if (len == rep->length) {
    CordRep::Unref(rep);
    return nullptr;
  }

  Position tail = rep->FindTail(rep->length - len);
  if (rep->refcount.IsOne()) {
    // We adopt a privately owned rep, scrub.
    if (tail.index != rep->tail_) UnrefEntries(rep, tail.index, rep->tail_);
    rep->tail_ = tail.index;
  } else {
    // Copy subset to new rep
    rep = Copy(rep, rep->head_, tail.index, extra);
    tail.index = rep->tail_;
  }

  // Adjust length
  rep->length -= len;

  // Adjust tail block
  if (tail.offset) {
    rep->SubLength(rep->retreat(tail.index), tail.offset);
  }

  return Validate(rep);
}

}  // namespace cord_internal
ABSL_NAMESPACE_END
}  // namespace absl
