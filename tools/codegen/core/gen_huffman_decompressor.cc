// Copyright 2022 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <atomic>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <numeric>
#include <queue>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <openssl/sha.h>

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"

#include "src/core/ext/transport/chttp2/transport/huffsyms.h"

///////////////////////////////////////////////////////////////////////////////
// SHA256 hash handling
// We need strong uniqueness checks of some very long strings - so we hash
// them with SHA256 and compare.
struct Hash {
  uint8_t bytes[SHA256_DIGEST_LENGTH];
  bool operator==(const Hash& other) const {
    return memcmp(bytes, other.bytes, SHA256_DIGEST_LENGTH) == 0;
  }
  bool operator<(const Hash& other) const {
    return memcmp(bytes, other.bytes, SHA256_DIGEST_LENGTH) < 0;
  }
};

// Given a vector of ints (T), return a Hash object with the sha256
template <typename T>
Hash HashVec(const std::vector<T>& v) {
  Hash h;
  SHA1(reinterpret_cast<const uint8_t*>(v.data()), v.size() * sizeof(T),
       h.bytes);
  return h;
}

///////////////////////////////////////////////////////////////////////////////
// BitQueue
// A utility that treats a sequence of bits like a queue
class BitQueue {
 public:
  BitQueue(unsigned mask, int len) : mask_(mask), len_(len) {}
  BitQueue() : BitQueue(0, 0) {}

  // Return the most significant bit (the front of the queue)
  int Front() const { return (mask_ >> (len_ - 1)) & 1; }
  // Pop one bit off the queue
  void Pop() {
    mask_ &= ~(1 << (len_ - 1));
    len_--;
  }
  bool Empty() const { return len_ == 0; }
  int length() const { return len_; }
  unsigned mask() const { return mask_; }

  // Text representation of the queue
  std::string ToString() const {
    return absl::StrCat(absl::Hex(mask_), "/", len_);
  }

  // Comparisons so that we can use BitQueue as a key in a std::map
  bool operator<(const BitQueue& other) const {
    return std::tie(mask_, len_) < std::tie(other.mask_, other.len_);
  }

 private:
  // The bits
  unsigned mask_;
  // How many bits have we
  int len_;
};

///////////////////////////////////////////////////////////////////////////////
// Symbol sets for the huffman tree

// A Sym is one symbol in the tree, and the bits that we need to read to decode
// that symbol. As we progress through decoding we remove bits from the symbol,
// but also condense the number of symbols we're considering.
struct Sym {
  BitQueue bits;
  int symbol;

  bool operator<(const Sym& other) const {
    return std::tie(bits, symbol) < std::tie(other.bits, other.symbol);
  }
};

// A SymSet is all the symbols we're considering at some time
using SymSet = std::vector<Sym>;

// Debug utility to turn a SymSet into a string
std::string SymSetString(const SymSet& syms) {
  std::vector<std::string> parts;
  for (const Sym& sym : syms) {
    parts.push_back(absl::StrCat(sym.symbol, ":", sym.bits.ToString()));
  }
  return absl::StrJoin(parts, ",");
}

// Initial SymSet - all the symbols [0..256] with their bits initialized from
// the http2 static huffman tree.
SymSet AllSyms() {
  SymSet syms;
  for (int i = 0; i < GRPC_CHTTP2_NUM_HUFFSYMS; i++) {
    Sym sym;
    sym.bits =
        BitQueue(grpc_chttp2_huffsyms[i].bits, grpc_chttp2_huffsyms[i].length);
    sym.symbol = i;
    syms.push_back(sym);
  }
  return syms;
}

// What whould we do after reading a set of bits?
struct ReadActions {
  // Emit these symbols
  std::vector<int> emit;
  // Number of bits that were consumed by the read
  int consumed;
  // Remaining SymSet that we need to consider on the next read action
  SymSet remaining;
};

// Given a SymSet \a pending, read through the bits in \a index and determine
// what actions the decoder should take.
// allow_multiple controls the behavior should we get to the last bit in pending
// and hence know which symbol to emit, but we still have bits in index.
// We could either start decoding the next symbol (allow_multiple == true), or
// we could stop (allow_multiple == false).
// If allow_multiple is true we tend to emit more per read op, but generate
// bigger tables.
ReadActions ActionsFor(BitQueue index, SymSet pending, bool allow_multiple) {
  std::vector<int> emit;
  int len_start = index.length();
  int len_consume = len_start;

  // We read one bit in index at a time, so whilst we have bits...
  while (!index.Empty()) {
    SymSet next_pending;
    // For each symbol in the pending set
    for (auto sym : pending) {
      // If the first bit doesn't match, then that symbol is not part of our
      // remaining set.
      if (sym.bits.Front() != index.Front()) continue;
      sym.bits.Pop();
      next_pending.push_back(sym);
    }
    switch (next_pending.size()) {
      case 0:
        // There should be no bit patterns that are undecodable.
        abort();
      case 1:
        // If we have one symbol left, we need to have decoded all of it.
        if (!next_pending[0].bits.Empty()) abort();
        // Emit that symbol
        emit.push_back(next_pending[0].symbol);
        // Track how many bits we've read.
        len_consume = index.length() - 1;
        // If we allow multiple, reprime pending and continue, otherwise stop.
        if (!allow_multiple) goto done;
        pending = AllSyms();
        break;
      default:
        pending = std::move(next_pending);
        break;
    }
    // Finished with this bit, continue with next
    index.Pop();
  }
done:
  return ReadActions{std::move(emit), len_start - len_consume, pending};
}

///////////////////////////////////////////////////////////////////////////////
// MatchCase
// A variant that helps us bunch together related ReadActions

// A Matched in a MatchCase indicates that we need to emit some number of
// symbols
struct Matched {
  // number of symbols to emit
  int emits;

  bool operator<(const Matched& other) const { return emits < other.emits; }
};

// Unmatched says we didn't emit anything and we need to keep decoding
struct Unmatched {
  SymSet syms;

  bool operator<(const Unmatched& other) const { return syms < other.syms; }
};

// Emit end of stream
struct End {
  bool operator<(End) const { return false; }
};

using MatchCase = absl::variant<Matched, Unmatched, End>;

///////////////////////////////////////////////////////////////////////////////
// Text & numeric helper functions

// Given a vector of lines, indent those lines by some number of indents
// (2 spaces) and return that.
std::vector<std::string> IndentLines(std::vector<std::string> lines,
                                     int n = 1) {
  std::string indent(2 * n, ' ');
  for (auto& line : lines) {
    line = absl::StrCat(indent, line);
  }
  return lines;
}

// Given a snake_case_name return a PascalCaseName
std::string ToPascalCase(const std::string& in) {
  std::string out;
  bool next_upper = true;
  for (char c : in) {
    if (c == '_') {
      next_upper = true;
    } else {
      if (next_upper) {
        out.push_back(toupper(c));
        next_upper = false;
      } else {
        out.push_back(c);
      }
    }
  }
  return out;
}

// Return a uint type for some number of bits (16 -> uint16_t, 32 -> uint32_t)
std::string Uint(int bits) { return absl::StrCat("uint", bits, "_t"); }

// Given a maximum value, how many bits to store it in a uint
int TypeBitsForMax(int max) {
  if (max <= 255) {
    return 8;
  } else if (max <= 65535) {
    return 16;
  } else {
    return 32;
  }
}

// Combine Uint & TypeBitsForMax to make for more concise code
std::string TypeForMax(int max) { return Uint(TypeBitsForMax(max)); }

// How many bits are needed to encode a value
int BitsForMaxValue(int x) {
  int n = 0;
  while (x >= (1 << n)) n++;
  return n;
}

///////////////////////////////////////////////////////////////////////////////
// Codegen framework
// Some helpers so we don't need to generate all the code linearly, which helps
// organize this a little more nicely.

// An Item is our primitive for code generation, it can generate some lines
// that it would like to emit - those lines are fed to a parent item that might
// generate more lines or mutate the ones we return, and so on until codegen
// is complete.
class Item {
 public:
  virtual ~Item() = default;
  virtual std::vector<std::string> ToLines() const = 0;
  std::string ToString() const {
    return absl::StrCat(absl::StrJoin(ToLines(), "\n"), "\n");
  }
};
using ItemPtr = std::unique_ptr<Item>;

// An item that emits one line (the one given as an argument!)
class String : public Item {
 public:
  explicit String(std::string s) : s_(std::move(s)) {}
  std::vector<std::string> ToLines() const override { return {s_}; }

 private:
  std::string s_;
};

// An item that returns a fixed copyright notice and autogenerated note text.
class Prelude final : public Item {
 public:
  std::vector<std::string> ToLines() const {
    return {
        "// Copyright 2022 gRPC authors.",
        "//",
        "// Licensed under the Apache License, Version 2.0 (the "
        "\"License\");",
        "// you may not use this file except in compliance with the License.",
        "// You may obtain a copy of the License at",
        "//",
        "//     http://www.apache.org/licenses/LICENSE-2.0",
        "//",
        "// Unless required by applicable law or agreed to in writing, "
        "software",
        "// distributed under the License is distributed on an \"AS IS\" "
        "BASIS,",
        "// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or "
        "implied.",
        "// See the License for the specific language governing permissions "
        "and",
        "// limitations under the License.",
        "",
        std::string(80, '/'),
        "// This file is autogenerated: see "
        "tools/codegen/core/gen_huffman_decompressor.cc",
        ""};
  }
};

class Switch;

// A Sink is an Item that we can add more Items to.
// At codegen time it calls each of its children in turn and concatenates
// their results together.
class Sink : public Item {
 public:
  std::vector<std::string> ToLines() const override {
    std::vector<std::string> lines;
    for (const auto& item : children_) {
      for (const auto& line : item->ToLines()) {
        lines.push_back(line);
      }
    }
    return lines;
  }

  // Add one string to our output.
  void Add(std::string s) {
    children_.push_back(absl::make_unique<String>(std::move(s)));
  }

  // Add an item of type T to our output (constructing it with args).
  template <typename T, typename... Args>
  T* Add(Args&&... args) {
    auto v = absl::make_unique<T>(std::forward<Args>(args)...);
    auto* r = v.get();
    children_.push_back(std::move(v));
    return r;
  }

 private:
  std::vector<ItemPtr> children_;
};

// A sink that indents its lines by one indent (2 spaces)
class Indent : public Sink {
 public:
  std::vector<std::string> ToLines() const override {
    return IndentLines(Sink::ToLines());
  }
};

// A Sink that wraps its lines in a while block
class While : public Sink {
 public:
  explicit While(std::string cond) : cond_(std::move(cond)) {}
  std::vector<std::string> ToLines() const override {
    std::vector<std::string> lines;
    lines.push_back(absl::StrCat("while (", cond_, ") {"));
    for (const auto& line : IndentLines(Sink::ToLines())) {
      lines.push_back(line);
    }
    lines.push_back("}");
    return lines;
  }

 private:
  std::string cond_;
};

// A switch statement.
// Cases can be modified by calling the Case member.
// Identical cases are collapsed into 'case X: case Y:' type blocks.
class Switch : public Item {
 public:
  // \a cond is the condition to place at the head of the switch statement.
  // eg. "switch (cond) {".
  explicit Switch(std::string cond) : cond_(std::move(cond)) {}
  std::vector<std::string> ToLines() const override {
    std::map<std::string, std::vector<std::string>> reverse_map;
    for (const auto& kv : cases_) {
      reverse_map[kv.second.ToString()].push_back(kv.first);
    }
    std::vector<std::string> lines;
    lines.push_back(absl::StrCat("switch (", cond_, ") {"));
    for (const auto& kv : reverse_map) {
      for (const auto& cond : kv.second) {
        lines.push_back(absl::StrCat("  case ", cond, ":"));
      }
      lines.back().append(" {");
      for (const auto& case_line :
           IndentLines(cases_.find(kv.second[0])->second.ToLines(), 2)) {
        lines.push_back(case_line);
      }
      lines.push_back("  }");
    }
    lines.push_back("}");
    return lines;
  }

  Sink* Case(std::string cond) { return &cases_[cond]; }

 private:
  std::string cond_;
  std::map<std::string, Sink> cases_;
};

///////////////////////////////////////////////////////////////////////////////
// BuildCtx declaration
// Shared state for one code gen attempt

class TableBuilder;
class FunMaker;

class BuildCtx {
 public:
  BuildCtx(std::vector<int> max_bits_for_depth, Sink* global_fns,
           Sink* global_decls, Sink* global_values, FunMaker* fun_maker)
      : max_bits_for_depth_(std::move(max_bits_for_depth)),
        global_fns_(global_fns),
        global_decls_(global_decls),
        global_values_(global_values),
        fun_maker_(fun_maker) {}

  void AddStep(SymSet start_syms, int num_bits, bool is_top, bool refill,
               int depth, Sink* out);
  void AddMatchBody(TableBuilder* table_builder, std::string index,
                    std::string ofs, const MatchCase& match_case, bool is_top,
                    bool refill, int depth, Sink* out);
  void AddDone(SymSet start_syms, int num_bits, bool all_ones_so_far,
               Sink* out);

  int NewId() { return next_id_++; }
  int MaxBitsForTop() const { return max_bits_for_depth_[0]; }

  absl::optional<std::string> PreviousNameForArtifact(std::string proposed_name,
                                                      Hash hash) {
    auto it = arrays_.find(hash);
    if (it == arrays_.end()) {
      arrays_.emplace(hash, proposed_name);
      return absl::nullopt;
    }
    return it->second;
  }

  Sink* global_fns() const { return global_fns_; }
  Sink* global_decls() const { return global_decls_; }
  Sink* global_values() const { return global_values_; }

 private:
  const std::vector<int> max_bits_for_depth_;
  std::map<Hash, std::string> arrays_;
  int next_id_ = 1;
  Sink* const global_fns_;
  Sink* const global_decls_;
  Sink* const global_values_;
  FunMaker* const fun_maker_;
};

///////////////////////////////////////////////////////////////////////////////
// TableBuilder
// All our magic for building decode tables.
// We have three kinds of tables to generate:
// 1. op tables that translate a bit sequence to which decode case we should
//    execute (and arguments to it), and
// 2. emit tables that translate an index given by the op table and tell us
//    which symbols to emit
// Op table format
// Our opcodes contain an offset into an emit table, a number of bits consumed
// and an operation. The consumed bits are how many of the presented to us bits
// we actually took. The operation tells whether to emit some symbols (and how
// many) or to keep decoding.
// Optimization 1:
// op tables are essentially dense maps of bits -> opcode, and it turns out
// that *many* of the opcodes repeat across index bits for some of our tables
// so for those we split the table into two levels: first level indexes into
// a child table, and the child table contains the deduped opcodes.
// Optimization 2:
// Emit tables are a bit list of uint8_ts, and are indexed into by the op
// table (with an offset and length) - since many symbols get repeated, we try
// to overlay the symbols in the emit table to reduce the size.
// Optimization 3:
// We shard the table into some number of slices and use the top bits of the
// incoming lookup to select the shard. This tends to allow us to use smaller
// types to represent the table, saving on footprint.

class TableBuilder {
 public:
  explicit TableBuilder(BuildCtx* ctx) : ctx_(ctx), id_(ctx->NewId()) {}

  // Append one case to the table
  void Add(int match_case, std::vector<uint8_t> emit, int consumed_bits) {
    elems_.push_back({match_case, std::move(emit), consumed_bits});
    max_consumed_bits_ = std::max(max_consumed_bits_, consumed_bits);
    max_match_case_ = std::max(max_match_case_, match_case);
  }

  // Build the table
  void Build() const {
    Choose()->Build(this, BitsForMaxValue(elems_.size() - 1));
  }

  // Generate a call to the accessor function for the emit table
  std::string EmitAccessor(std::string index, std::string offset) {
    return absl::StrCat("GetEmit", id_, "(", index, ", ", offset, ")");
  }

  // Generate a call to the accessor function for the op table
  std::string OpAccessor(std::string index) {
    return absl::StrCat("GetOp", id_, "(", index, ")");
  }

  int ConsumeBits() const { return BitsForMaxValue(max_consumed_bits_); }
  int MatchBits() const { return BitsForMaxValue(max_match_case_); }

 private:
  // One element in the op table.
  struct Elem {
    int match_case;
    std::vector<uint8_t> emit;
    int consumed_bits;
  };

  // A nested slice is one slice of a table using two level lookup
  // - i.e. we look at an outer table to get an index into the inner table,
  //   and then fetch the result from there.
  struct NestedSlice {
    std::vector<uint8_t> emit;
    std::vector<uint64_t> inner;
    std::vector<int> outer;

    // Various sizes return number of bits to be generated

    size_t InnerSize() const {
      return inner.size() *
             TypeBitsForMax(*std::max_element(inner.begin(), inner.end()));
    }

    size_t OuterSize() const {
      return outer.size() *
             TypeBitsForMax(*std::max_element(outer.begin(), outer.end()));
    }

    size_t EmitSize() const { return emit.size() * 8; }
  };

  // A slice is one part of a larger table.
  struct Slice {
    std::vector<uint8_t> emit;
    std::vector<uint64_t> ops;

    // Various sizes return number of bits to be generated

    size_t OpsSize() const {
      return ops.size() *
             TypeBitsForMax(*std::max_element(ops.begin(), ops.end()));
    }

    size_t EmitSize() const { return emit.size() * 8; }

    // Given a vector of symbols to emit, return the offset into the emit table
    // that they're at (adding them to the emit table if necessary).
    int OffsetOf(const std::vector<uint8_t>& x) {
      if (x.empty()) return 0;
      auto r = std::search(emit.begin(), emit.end(), x.begin(), x.end());
      if (r == emit.end()) {
        // look for a partial match @ end
        for (size_t check_len = x.size() - 1; check_len > 0; check_len--) {
          if (emit.size() < check_len) continue;
          bool matches = true;
          for (size_t i = 0; matches && i < check_len; i++) {
            if (emit[emit.size() - check_len + i] != x[i]) matches = false;
          }
          if (matches) {
            int offset = emit.size() - check_len;
            for (size_t i = check_len; i < x.size(); i++) {
              emit.push_back(x[i]);
            }
            return offset;
          }
        }
        // add new
        int result = emit.size();
        for (auto v : x) emit.push_back(v);
        return result;
      }
      return r - emit.begin();
    }

    // Convert this slice to a nested slice.
    NestedSlice MakeNestedSlice() const {
      NestedSlice result;
      result.emit = emit;
      std::map<uint64_t, int> op_to_inner;
      for (auto v : ops) {
        auto it = op_to_inner.find(v);
        if (it == op_to_inner.end()) {
          it = op_to_inner.emplace(v, op_to_inner.size()).first;
          result.inner.push_back(v);
        }
        result.outer.push_back(it->second);
      }
      return result;
    }
  };

  // An EncodeOption is a potential way of encoding a table.
  struct EncodeOption {
    // Overall size (in bits) of the table encoding
    virtual size_t Size() const = 0;
    // Generate the code
    virtual void Build(const TableBuilder* builder, int op_bits) const = 0;
    virtual ~EncodeOption() {}
  };

  // NestedTable is a table that uses two level lookup for each slice
  struct NestedTable : public EncodeOption {
    std::vector<NestedSlice> slices;
    int slice_bits;
    size_t Size() const override {
      size_t sum = 0;
      std::vector<Hash> h_emit;
      std::vector<Hash> h_inner;
      std::vector<Hash> h_outer;
      for (size_t i = 0; i < slices.size(); i++) {
        h_emit.push_back(HashVec(slices[i].emit));
        h_inner.push_back(HashVec(slices[i].inner));
        h_outer.push_back(HashVec(slices[i].outer));
      }
      std::set<Hash> seen;
      for (size_t i = 0; i < slices.size(); i++) {
        // Try to account for deduplication in the size calculation.
        if (seen.count(h_emit[i]) == 0) sum += slices[i].EmitSize();
        if (seen.count(h_outer[i]) == 0) sum += slices[i].OuterSize();
        if (seen.count(h_inner[i]) == 0) sum += slices[i].OuterSize();
        seen.insert(h_emit[i]);
        seen.insert(h_outer[i]);
        seen.insert(h_inner[i]);
      }
      if (slice_bits != 0) sum += 3 * 64 * slices.size();
      return sum;
    }
    void Build(const TableBuilder* builder, int op_bits) const override {
      Sink* const global_fns = builder->ctx_->global_fns();
      Sink* const global_decls = builder->ctx_->global_decls();
      Sink* const global_values = builder->ctx_->global_values();
      const int id = builder->id_;
      std::vector<std::string> lines;
      const uint64_t max_inner = MaxInner();
      const uint64_t max_outer = MaxOuter();
      std::vector<std::string> emit_names;
      std::vector<std::string> inner_names;
      std::vector<std::string> outer_names;
      for (size_t i = 0; i < slices.size(); i++) {
        emit_names.push_back(builder->GenArray(
            absl::StrCat("table", id, "_", i, "_emit"), "uint8_t",
            slices[i].emit, true, global_decls, global_values));
        inner_names.push_back(builder->GenArray(
            absl::StrCat("table", id, "_", i, "_inner"), TypeForMax(max_inner),
            slices[i].inner, true, global_decls, global_values));
        outer_names.push_back(builder->GenArray(
            absl::StrCat("table", id, "_", i, "_outer"), TypeForMax(max_outer),
            slices[i].outer, false, global_decls, global_values));
      }
      if (slice_bits == 0) {
        global_fns->Add(absl::StrCat("static inline uint64_t GetOp", id,
                                     "(size_t i) { return ", inner_names[0],
                                     "[", outer_names[0], "[i]]; }"));
        global_fns->Add(absl::StrCat("static inline uint64_t GetEmit", id,
                                     "(size_t, size_t emit) { return ",
                                     emit_names[0], "[emit]; }"));
      } else {
        GenCompound(id, emit_names, "emit", "uint8_t", global_decls,
                    global_values);
        GenCompound(id, inner_names, "inner", TypeForMax(max_inner),
                    global_decls, global_values);
        GenCompound(id, outer_names, "outer", TypeForMax(max_outer),
                    global_decls, global_values);
        global_fns->Add(absl::StrCat(
            "static inline uint64_t GetOp", id, "(size_t i) { return table", id,
            "_inner_[i >> ", op_bits - slice_bits, "][table", id,
            "_outer_[i >> ", op_bits - slice_bits, "][i & 0x",
            absl::Hex((1 << (op_bits - slice_bits)) - 1), "]]; }"));
        global_fns->Add(absl::StrCat("static inline uint64_t GetEmit", id,
                                     "(size_t i, size_t emit) { return table",
                                     id, "_emit_[i >> ", op_bits - slice_bits,
                                     "][emit]; }"));
      }
    }
    uint64_t MaxInner() const {
      uint64_t max_inner = 0;
      for (size_t i = 0; i < slices.size(); i++) {
        max_inner = std::max(
            max_inner,
            *std::max_element(slices[i].inner.begin(), slices[i].inner.end()));
      }
      return max_inner;
    }
    int MaxOuter() const {
      int max_outer = 0;
      for (size_t i = 0; i < slices.size(); i++) {
        max_outer = std::max(
            max_outer,
            *std::max_element(slices[i].outer.begin(), slices[i].outer.end()));
      }
      return max_outer;
    }
  };

  // Encoding that uses single level lookup for each slice.
  struct Table : public EncodeOption {
    std::vector<Slice> slices;
    int slice_bits;
    size_t Size() const override {
      size_t sum = 0;
      std::vector<Hash> h_emit;
      std::vector<Hash> h_ops;
      for (size_t i = 0; i < slices.size(); i++) {
        h_emit.push_back(HashVec(slices[i].emit));
        h_ops.push_back(HashVec(slices[i].ops));
      }
      std::set<Hash> seen;
      for (size_t i = 0; i < slices.size(); i++) {
        if (seen.count(h_emit[i]) == 0) sum += slices[i].EmitSize();
        if (seen.count(h_ops[i]) == 0) sum += slices[i].OpsSize();
        seen.insert(h_emit[i]);
        seen.insert(h_ops[i]);
      }
      return sum + 3 * 64 * slices.size();
    }
    void Build(const TableBuilder* builder, int op_bits) const override {
      Sink* const global_fns = builder->ctx_->global_fns();
      Sink* const global_decls = builder->ctx_->global_decls();
      Sink* const global_values = builder->ctx_->global_values();
      uint64_t max_op = MaxOp();
      const int id = builder->id_;
      std::vector<std::string> emit_names;
      std::vector<std::string> ops_names;
      for (size_t i = 0; i < slices.size(); i++) {
        emit_names.push_back(builder->GenArray(
            absl::StrCat("table", id, "_", i, "_emit"), "uint8_t",
            slices[i].emit, true, global_decls, global_values));
        ops_names.push_back(builder->GenArray(
            absl::StrCat("table", id, "_", i, "_ops"), TypeForMax(max_op),
            slices[i].ops, true, global_decls, global_values));
      }
      if (slice_bits == 0) {
        global_fns->Add(absl::StrCat("static inline uint64_t GetOp", id,
                                     "(size_t i) { return ", ops_names[0],
                                     "[i]; }"));
        global_fns->Add(absl::StrCat("static inline uint64_t GetEmit", id,
                                     "(size_t, size_t emit) { return ",
                                     emit_names[0], "[emit]; }"));
      } else {
        GenCompound(id, emit_names, "emit", "uint8_t", global_decls,
                    global_values);
        GenCompound(id, ops_names, "ops", TypeForMax(max_op), global_decls,
                    global_values);
        global_fns->Add(absl::StrCat(
            "static inline uint64_t GetOp", id, "(size_t i) { return table", id,
            "_ops_[i >> ", op_bits - slice_bits, "][i & 0x",
            absl::Hex((1 << (op_bits - slice_bits)) - 1), "]; }"));
        global_fns->Add(absl::StrCat("static inline uint64_t GetEmit", id,
                                     "(size_t i, size_t emit) { return table",
                                     id, "_emit_[i >> ", op_bits - slice_bits,
                                     "][emit]; }"));
      }
    }
    uint64_t MaxOp() const {
      uint64_t max_op = 0;
      for (size_t i = 0; i < slices.size(); i++) {
        max_op = std::max(max_op, *std::max_element(slices[i].ops.begin(),
                                                    slices[i].ops.end()));
      }
      return max_op;
    }
    // Convert to a two-level lookup
    std::unique_ptr<NestedTable> MakeNestedTable() {
      std::unique_ptr<NestedTable> result(new NestedTable);
      result->slice_bits = slice_bits;
      for (const auto& slice : slices) {
        result->slices.push_back(slice.MakeNestedSlice());
      }
      return result;
    }
  };

  // Given a number of slices (2**slice_bits), generate a table that uses a
  // single level lookup for each slice based on our input.
  std::unique_ptr<Table> MakeTable(size_t slice_bits) const {
    std::unique_ptr<Table> table = absl::make_unique<Table>();
    int slices = 1 << slice_bits;
    table->slices.resize(slices);
    table->slice_bits = slice_bits;
    const int pack_consume_bits = ConsumeBits();
    const int pack_match_bits = MatchBits();
    for (size_t i = 0; i < slices; i++) {
      auto& slice = table->slices[i];
      for (size_t j = 0; j < elems_.size() / slices; j++) {
        const auto& elem = elems_[i * elems_.size() / slices + j];
        slice.ops.push_back(elem.consumed_bits |
                            (elem.match_case << pack_consume_bits) |
                            (slice.OffsetOf(elem.emit)
                             << (pack_consume_bits + pack_match_bits)));
      }
    }
    return table;
  }

  // Helper to generate a compound table (an array of arrays)
  static void GenCompound(int id, std::vector<std::string> names,
                          std::string ext, std::string type, Sink* global_decls,
                          Sink* global_values) {
    global_decls->Add(absl::StrCat("static const ", type, "* const table", id,
                                   "_", ext, "_[", names.size(), "];"));
    global_values->Add(absl::StrCat("const ", type,
                                    "* const HuffDecoderCommon::table", id, "_",
                                    ext, "_[", names.size(), "] = {"));
    for (const std::string& name : names) {
      global_values->Add(absl::StrCat("  ", name, ","));
    }
    global_values->Add("};");
  }

  // Helper to generate an array of values
  template <typename T>
  std::string GenArray(std::string name, std::string type,
                       const std::vector<T>& values, bool hex,
                       Sink* global_decls, Sink* global_values) const {
    auto previous_name = ctx_->PreviousNameForArtifact(name, HashVec(values));
    if (previous_name.has_value()) {
      return absl::StrCat(*previous_name, "_");
    }
    std::vector<std::string> elems;
    elems.reserve(values.size());
    for (const auto& elem : values) {
      if (hex) {
        if (type == "uint8_t") {
          elems.push_back(absl::StrCat("0x", absl::Hex(elem, absl::kZeroPad2)));
        } else if (type == "uint16_t") {
          elems.push_back(absl::StrCat("0x", absl::Hex(elem, absl::kZeroPad4)));
        } else {
          elems.push_back(absl::StrCat("0x", absl::Hex(elem, absl::kZeroPad8)));
        }
      } else {
        elems.push_back(absl::StrCat(elem));
      }
    }
    std::string data = absl::StrJoin(elems, ", ");
    global_decls->Add(absl::StrCat("static const ", type, " ", name, "_[",
                                   values.size(), "];"));
    global_values->Add(absl::StrCat("const ", type, " HuffDecoderCommon::",
                                    name, "_[", values.size(), "] = {"));
    global_values->Add(absl::StrCat("  ", data));
    global_values->Add("};");
    return absl::StrCat(name, "_");
  }

  // Choose an encoding for this set of tables.
  // We try all available values for slice count and choose the one that gives
  // the smallest footprint.
  std::unique_ptr<EncodeOption> Choose() const {
    std::unique_ptr<EncodeOption> chosen;
    size_t best_size = std::numeric_limits<size_t>::max();
    for (size_t slice_bits = 0; (1 << slice_bits) < elems_.size();
         slice_bits++) {
      auto raw = MakeTable(slice_bits);
      size_t raw_size = raw->Size();
      auto nested = raw->MakeNestedTable();
      size_t nested_size = nested->Size();
      if (raw_size < best_size) {
        chosen = std::move(raw);
        best_size = raw_size;
      }
      if (nested_size < best_size) {
        chosen = std::move(nested);
        best_size = nested_size;
      }
    }
    return chosen;
  }

  BuildCtx* const ctx_;
  std::vector<Elem> elems_;
  int max_consumed_bits_ = 0;
  int max_match_case_ = 0;
  const int id_;
};

///////////////////////////////////////////////////////////////////////////////
// FunMaker
// Handles generating the code for various functions.

class FunMaker {
 public:
  explicit FunMaker(Sink* sink) : sink_(sink) {}

  // Generate a refill function - that ensures the incoming bitmask has enough
  // bits for the next step.
  std::string RefillTo(int n) {
    if (have_refills_.count(n) == 0) {
      have_refills_.insert(n);
      auto fn = NewFun(absl::StrCat("RefillTo", n), "bool");
      auto s = fn->Add<Switch>("buffer_len_");
      for (int i = 0; i < n; i++) {
        auto c = s->Case(absl::StrCat(i));
        const int bytes_needed = (n - i + 7) / 8;
        c->Add(absl::StrCat("return ", ReadBytes(bytes_needed), ";"));
      }
      fn->Add("return true;");
    }
    return absl::StrCat("RefillTo", n, "()");
  }

  // At callsite, generate a call to a new function with base name
  // base_name (new functions get a suffix of how many instances of base_name
  // there have been).
  // Return a sink to fill in the body of the new function.
  Sink* CallNewFun(std::string base_name, Sink* callsite) {
    std::string name = absl::StrCat(base_name, have_funs_[base_name]++);
    callsite->Add(absl::StrCat(name, "();"));
    return NewFun(name, "void");
  }

 private:
  Sink* NewFun(std::string name, std::string returns) {
    sink_->Add(absl::StrCat(returns, " ", name, "() {"));
    auto fn = sink_->Add<Indent>();
    sink_->Add("}");
    return fn;
  }

  // Bring in some number of bytes from the input stream to our current read
  // bits.
  std::string ReadBytes(int bytes_needed) {
    if (have_reads_.count(bytes_needed) == 0) {
      have_reads_.insert(bytes_needed);
      auto fn = NewFun(absl::StrCat("Read", bytes_needed), "bool");
      fn->Add(absl::StrCat("if (end_ - begin_ < ", bytes_needed,
                           ") return false;"));
      fn->Add(absl::StrCat("buffer_ <<= ", 8 * bytes_needed, ";"));
      for (int i = 0; i < bytes_needed; i++) {
        fn->Add(absl::StrCat("buffer_ |= static_cast<uint64_t>(*begin_++) << ",
                             8 * (bytes_needed - i - 1), ";"));
      }
      fn->Add(absl::StrCat("buffer_len_ += ", 8 * bytes_needed, ";"));
      fn->Add("return true;");
    }
    return absl::StrCat("Read", bytes_needed, "()");
  }

  std::set<int> have_refills_;
  std::set<int> have_reads_;
  std::map<std::string, int> have_funs_;
  Sink* sink_;
};

///////////////////////////////////////////////////////////////////////////////
// BuildCtx implementation

void BuildCtx::AddDone(SymSet start_syms, int num_bits, bool all_ones_so_far,
                       Sink* out) {
  out->Add("done_ = true;");
  if (num_bits == 1) {
    if (!all_ones_so_far) out->Add("ok_ = false;");
    return;
  }
  // we must have 0 < buffer_len_ < num_bits
  auto s = out->Add<Switch>("buffer_len_");
  auto c0 = s->Case("0");
  if (!all_ones_so_far) c0->Add("ok_ = false;");
  c0->Add("return;");
  for (int i = 1; i < num_bits; i++) {
    auto c = s->Case(absl::StrCat(i));
    SymSet maybe;
    for (auto sym : start_syms) {
      if (sym.bits.length() > i) continue;
      maybe.push_back(sym);
    }
    if (maybe.empty()) {
      if (all_ones_so_far) {
        c->Add("ok_ = (buffer_ & ((1<<buffer_len_)-1)) == (1<<buffer_len_)-1;");
      } else {
        c->Add("ok_ = false;");
      }
      c->Add("return;");
      continue;
    }
    TableBuilder table_builder(this);
    enum Cases {
      kNoEmitOk,
      kFail,
      kEmitOk,
    };
    for (size_t n = 0; n < (1 << i); n++) {
      if (all_ones_so_far && n == (1 << i) - 1) {
        table_builder.Add(kNoEmitOk, {}, 0);
        goto next;
      }
      for (auto sym : maybe) {
        if ((n >> (i - sym.bits.length())) == sym.bits.mask()) {
          for (int j = 0; j < (i - sym.bits.length()); j++) {
            if ((n & (1 << j)) == 0) {
              table_builder.Add(kFail, {}, 0);
              goto next;
            }
          }
          table_builder.Add(kEmitOk, {static_cast<uint8_t>(sym.symbol)}, 0);
          goto next;
        }
      }
      table_builder.Add(kFail, {}, 0);
    next:;
    }
    table_builder.Build();
    c->Add(absl::StrCat("const auto index = buffer_ & ", (1 << i) - 1, ";"));
    c->Add(absl::StrCat("const auto op = ", table_builder.OpAccessor("index"),
                        ";"));
    if (table_builder.ConsumeBits() != 0) {
      fprintf(stderr, "consume bits = %d\n", table_builder.ConsumeBits());
      abort();
    }
    auto s_fin = c->Add<Switch>(
        absl::StrCat("op & ", (1 << table_builder.MatchBits()) - 1));
    auto emit_ok = s_fin->Case(absl::StrCat(kEmitOk));
    emit_ok->Add(absl::StrCat(
        "sink_(",
        table_builder.EmitAccessor(
            "index", absl::StrCat("op >>", table_builder.MatchBits())),
        ");"));
    emit_ok->Add("break;");
    auto fail = s_fin->Case(absl::StrCat(kFail));
    fail->Add("ok_ = false;");
    fail->Add("break;");
    c->Add("return;");
  }
}

void BuildCtx::AddStep(SymSet start_syms, int num_bits, bool is_top,
                       bool refill, int depth, Sink* out) {
  TableBuilder table_builder(this);
  if (refill) {
    out->Add(absl::StrCat("if (!", fun_maker_->RefillTo(num_bits), ") {"));
    auto ifblk = out->Add<Indent>();
    if (!is_top) {
      Sym some = start_syms[0];
      auto sym = grpc_chttp2_huffsyms[some.symbol];
      int consumed_len = (sym.length - some.bits.length());
      uint32_t consumed_mask = sym.bits >> some.bits.length();
      bool all_ones_so_far = consumed_mask == ((1 << consumed_len) - 1);
      AddDone(start_syms, num_bits, all_ones_so_far,
              fun_maker_->CallNewFun("Done", ifblk));
      ifblk->Add("return;");
    } else {
      AddDone(start_syms, num_bits, true,
              fun_maker_->CallNewFun("Done", ifblk));
      ifblk->Add("break;");
    }
    out->Add("}");
  }
  out->Add(absl::StrCat("const auto index = (buffer_ >> (buffer_len_ - ",
                        num_bits, ")) & 0x", absl::Hex((1 << num_bits) - 1),
                        ";"));
  std::map<MatchCase, int> match_cases;
  for (int i = 0; i < (1 << num_bits); i++) {
    auto actions = ActionsFor(BitQueue(i, num_bits), start_syms, is_top);
    auto add_case = [&match_cases](MatchCase match_case) {
      if (match_cases.find(match_case) == match_cases.end()) {
        match_cases[match_case] = match_cases.size();
      }
      return match_cases[match_case];
    };
    if (actions.emit.size() == 1 && actions.emit[0] == 256) {
      table_builder.Add(add_case(End{}), {}, actions.consumed);
    } else if (actions.consumed == 0) {
      table_builder.Add(add_case(Unmatched{std::move(actions.remaining)}), {},
                        num_bits);
    } else {
      std::vector<uint8_t> emit;
      for (auto sym : actions.emit) emit.push_back(sym);
      table_builder.Add(
          add_case(Matched{static_cast<int>(actions.emit.size())}),
          std::move(emit), actions.consumed);
    }
  }
  table_builder.Build();
  out->Add(
      absl::StrCat("const auto op = ", table_builder.OpAccessor("index"), ";"));
  out->Add(absl::StrCat("const int consumed = op & ",
                        (1 << table_builder.ConsumeBits()) - 1, ";"));
  out->Add("buffer_len_ -= consumed;");
  out->Add(absl::StrCat("const auto emit_ofs = op >> ",
                        table_builder.ConsumeBits() + table_builder.MatchBits(),
                        ";"));
  if (match_cases.size() == 1) {
    AddMatchBody(&table_builder, "index", "emit_ofs",
                 match_cases.begin()->first, is_top, refill, depth, out);
  } else {
    auto s = out->Add<Switch>(
        absl::StrCat("(op >> ", table_builder.ConsumeBits(), ") & ",
                     (1 << table_builder.MatchBits()) - 1));
    for (auto kv : match_cases) {
      auto c = s->Case(absl::StrCat(kv.second));
      AddMatchBody(&table_builder, "index", "emit_ofs", kv.first, is_top,
                   refill, depth, c);
      c->Add("break;");
    }
  }
}

void BuildCtx::AddMatchBody(TableBuilder* table_builder, std::string index,
                            std::string ofs, const MatchCase& match_case,
                            bool is_top, bool refill, int depth, Sink* out) {
  if (absl::holds_alternative<End>(match_case)) {
    out->Add("begin_ = end_;");
    out->Add("buffer_len_ = 0;");
    return;
  }
  if (auto* p = absl::get_if<Unmatched>(&match_case)) {
    if (refill) {
      int max_bits = 0;
      for (auto sym : p->syms) max_bits = std::max(max_bits, sym.bits.length());
      AddStep(p->syms,
              depth + 1 >= max_bits_for_depth_.size()
                  ? max_bits
                  : std::min(max_bits, max_bits_for_depth_[depth + 1]),
              false, true, depth + 1,
              fun_maker_->CallNewFun("DecodeStep", out));
    }
    return;
  }
  const auto& matched = absl::get<Matched>(match_case);
  for (int i = 0; i < matched.emits; i++) {
    out->Add(absl::StrCat(
        "sink_(",
        table_builder->EmitAccessor(index, absl::StrCat(ofs, " + ", i)), ");"));
  }
}

///////////////////////////////////////////////////////////////////////////////
// Driver code

// Generated header and source code
struct BuildOutput {
  std::string header;
  std::string source;
};

// Given max_bits_for_depth = {n1,n2,n3,...}
// Build a decoder that first considers n1 bits, then n2, then n3, ...
BuildOutput Build(std::vector<int> max_bits_for_depth) {
  auto hdr = absl::make_unique<Sink>();
  auto src = absl::make_unique<Sink>();
  hdr->Add<Prelude>();
  src->Add<Prelude>();
  hdr->Add("#ifndef GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_DECODE_HUFF_H");
  hdr->Add("#define GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_DECODE_HUFF_H");
  src->Add(
      "#include \"src/core/ext/transport/chttp2/transport/decode_huff.h\"");
  hdr->Add("#include <cstddef>");
  hdr->Add("#include <grpc/support/port_platform.h>");
  src->Add("#include <grpc/support/port_platform.h>");
  hdr->Add("#include <cstdint>");
  hdr->Add(
      absl::StrCat("// GEOMETRY: ", absl::StrJoin(max_bits_for_depth, ",")));
  hdr->Add("namespace grpc_core {");
  src->Add("namespace grpc_core {");
  hdr->Add("class HuffDecoderCommon {");
  hdr->Add(" protected:");
  auto global_fns = hdr->Add<Indent>();
  hdr->Add(" private:");
  auto global_decls = hdr->Add<Indent>();
  hdr->Add("};");
  hdr->Add(
      "template<typename F> class HuffDecoder : public HuffDecoderCommon {");
  hdr->Add(" public:");
  auto pub = hdr->Add<Indent>();
  hdr->Add(" private:");
  auto prv = hdr->Add<Indent>();
  FunMaker fun_maker(prv->Add<Sink>());
  hdr->Add("};");
  hdr->Add("}  // namespace grpc_core");
  hdr->Add("#endif  // GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_DECODE_HUFF_H");
  auto global_values = src->Add<Indent>();
  src->Add("}  // namespace grpc_core");
  BuildCtx ctx(std::move(max_bits_for_depth), global_fns, global_decls,
               global_values, &fun_maker);
  // constructor
  pub->Add(
      "HuffDecoder(F sink, const uint8_t* begin, const uint8_t* end) : "
      "sink_(sink), begin_(begin), end_(end) {}");
  // members
  prv->Add("F sink_;");
  prv->Add("const uint8_t* begin_;");
  prv->Add("const uint8_t* const end_;");
  prv->Add("uint64_t buffer_ = 0;");
  prv->Add("int buffer_len_ = 0;");
  prv->Add("bool ok_ = true;");
  prv->Add("bool done_ = false;");
  // main fn
  pub->Add("bool Run() {");
  auto body = pub->Add<Indent>();
  body->Add("while (!done_) {");
  ctx.AddStep(AllSyms(), ctx.MaxBitsForTop(), true, true, 0,
              body->Add<Indent>());
  body->Add("}");
  body->Add("return ok_;");
  pub->Add("}");
  return {hdr->ToString(), src->ToString()};
}

// Generate all permutations of max_bits_for_depth for the Build function,
// with a minimum step size of 5 bits (needed for http2 I think) and a
// configurable maximum step size.
class PermutationBuilder {
 public:
  explicit PermutationBuilder(int max_depth) : max_depth_(max_depth) {}
  std::vector<std::vector<int>> Run() {
    Step({});
    return std::move(perms_);
  }

 private:
  void Step(std::vector<int> so_far) {
    int sum_so_far = std::accumulate(so_far.begin(), so_far.end(), 0);
    if (so_far.size() > max_depth_ ||
        (so_far.size() == max_depth_ && sum_so_far != 30)) {
      return;
    }
    if (sum_so_far + 5 > 30) {
      perms_.emplace_back(std::move(so_far));
      return;
    }
    for (int i = 5; i <= std::min(30 - sum_so_far, 16); i++) {
      auto p = so_far;
      p.push_back(i);
      Step(std::move(p));
    }
  }

  const int max_depth_;
  std::vector<std::vector<int>> perms_;
};

// Does what it says.
void WriteFile(std::string filename, std::string content) {
  std::ofstream ofs(filename);
  ofs << content;
}

int main(void) {
  BuildOutput best;
  size_t best_len = std::numeric_limits<size_t>::max();
  std::vector<std::unique_ptr<BuildOutput>> results;
  std::queue<std::thread> threads;
  // Generate all permutations of max_bits_for_depth for the Build function.
  // Then generate all variations of the code.
  for (const auto& perm : PermutationBuilder(30).Run()) {
    while (threads.size() > 200) {
      threads.front().join();
      threads.pop();
    }
    results.emplace_back(absl::make_unique<BuildOutput>());
    threads.emplace([perm, r = results.back().get()] { *r = Build(perm); });
  }
  while (!threads.empty()) {
    threads.front().join();
    threads.pop();
  }
  // Choose the variation that generates the least code, weighted towards header
  // length
  for (auto& r : results) {
    size_t l = 5 * r->header.length() + r->source.length();
    if (l < best_len) {
      best_len = l;
      best = std::move(*r);
    }
  }
  WriteFile("src/core/ext/transport/chttp2/transport/decode_huff.h",
            best.header);
  WriteFile("src/core/ext/transport/chttp2/transport/decode_huff.cc",
            best.source);
  return 0;
}
