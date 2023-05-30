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
#include <memory>
#include <numeric>
#include <queue>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <openssl/sha.h>

#include "absl/memory/memory.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
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
  std::string ToString() const {
    std::string result;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
      absl::StrAppend(&result, absl::Hex(bytes[i], absl::kZeroPad2));
    }
    return result;
  }
};

// Given a vector of ints (T), return a Hash object with the sha256
template <typename T>
Hash HashVec(absl::string_view type, const std::vector<T>& v) {
  Hash h;
  std::string text = absl::StrCat(type, ":", absl::StrJoin(v, ","));
  SHA256(reinterpret_cast<const uint8_t*>(text.data()), text.size(), h.bytes);
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
  explicit Prelude(absl::string_view comment_prefix)
      : comment_prefix_(comment_prefix) {}
  std::vector<std::string> ToLines() const override {
    auto line = [this](absl::string_view text) {
      return absl::StrCat(comment_prefix_, " ", text);
    };
    return {
        line("Copyright 2023 gRPC authors."),
        line(""),
        line("Licensed under the Apache License, Version 2.0 (the "
             "\"License\");"),
        line(
            "you may not use this file except in compliance with the License."),
        line("You may obtain a copy of the License at"),
        line(""),
        line("    http://www.apache.org/licenses/LICENSE-2.0"),
        line(""),
        line("Unless required by applicable law or agreed to in writing, "
             "software"),
        line("distributed under the License is distributed on an \"AS IS\" "
             "BASIS,"),
        line("WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or "
             "implied."),
        line("See the License for the specific language governing permissions "
             "and"),
        line("limitations under the License."),
        "",
        line("This file is autogenerated: see "
             "tools/codegen/core/gen_huffman_decompressor.cc"),
        ""};
  }

 private:
  absl::string_view comment_prefix_;
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
    children_.push_back(std::make_unique<String>(std::move(s)));
  }

  // Add an item of type T to our output (constructing it with args).
  template <typename T, typename... Args>
  T* Add(Args&&... args) {
    auto v = std::make_unique<T>(std::forward<Args>(args)...);
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
        if (cond == "") {
          lines.push_back("  default:");
        } else {
          lines.push_back(absl::StrCat("  case ", cond, ":"));
        }
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
        h_emit.push_back(HashVec("uint8_t", slices[i].emit));
        h_inner.push_back(HashVec(TypeForMax(MaxInner()), slices[i].inner));
        h_outer.push_back(HashVec(TypeForMax(MaxOuter()), slices[i].outer));
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
      std::vector<std::unique_ptr<Array>> emit_names;
      std::vector<std::unique_ptr<Array>> inner_names;
      std::vector<std::unique_ptr<Array>> outer_names;
      for (size_t i = 0; i < slices.size(); i++) {
        emit_names.push_back(builder->GenArray(
            slice_bits != 0, absl::StrCat("table", id, "_", i, "_emit"),
            "uint8_t", slices[i].emit, true, global_decls, global_values));
        inner_names.push_back(builder->GenArray(
            slice_bits != 0, absl::StrCat("table", id, "_", i, "_inner"),
            TypeForMax(max_inner), slices[i].inner, true, global_decls,
            global_values));
        outer_names.push_back(builder->GenArray(
            slice_bits != 0, absl::StrCat("table", id, "_", i, "_outer"),
            TypeForMax(max_outer), slices[i].outer, false, global_decls,
            global_values));
      }
      if (slice_bits == 0) {
        global_fns->Add(absl::StrCat(
            "static inline uint64_t GetOp", id, "(size_t i) { return ",
            inner_names[0]->Index(outer_names[0]->Index("i")), "; }"));
        global_fns->Add(absl::StrCat("static inline uint64_t GetEmit", id,
                                     "(size_t, size_t emit) { return ",
                                     emit_names[0]->Index("emit"), "; }"));
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
      if (max_inner == 0) {
        for (size_t i = 0; i < slices.size(); i++) {
          max_inner =
              std::max(max_inner, *std::max_element(slices[i].inner.begin(),
                                                    slices[i].inner.end()));
        }
      }
      return max_inner;
    }
    int MaxOuter() const {
      if (max_outer == 0) {
        for (size_t i = 0; i < slices.size(); i++) {
          max_outer =
              std::max(max_outer, *std::max_element(slices[i].outer.begin(),
                                                    slices[i].outer.end()));
        }
      }
      return max_outer;
    }
    mutable uint64_t max_inner = 0;
    mutable int max_outer = 0;
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
        h_emit.push_back(HashVec("uint8_t", slices[i].emit));
        h_ops.push_back(HashVec(TypeForMax(MaxOp()), slices[i].ops));
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
      std::vector<std::unique_ptr<Array>> emit_names;
      std::vector<std::unique_ptr<Array>> ops_names;
      for (size_t i = 0; i < slices.size(); i++) {
        emit_names.push_back(builder->GenArray(
            slice_bits != 0, absl::StrCat("table", id, "_", i, "_emit"),
            "uint8_t", slices[i].emit, true, global_decls, global_values));
        ops_names.push_back(builder->GenArray(
            slice_bits != 0, absl::StrCat("table", id, "_", i, "_ops"),
            TypeForMax(max_op), slices[i].ops, true, global_decls,
            global_values));
      }
      if (slice_bits == 0) {
        global_fns->Add(absl::StrCat("static inline uint64_t GetOp", id,
                                     "(size_t i) { return ",
                                     ops_names[0]->Index("i"), "; }"));
        global_fns->Add(absl::StrCat("static inline uint64_t GetEmit", id,
                                     "(size_t, size_t emit) { return ",
                                     emit_names[0]->Index("emit"), "; }"));
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
      if (max_op == 0) {
        for (size_t i = 0; i < slices.size(); i++) {
          max_op = std::max(max_op, *std::max_element(slices[i].ops.begin(),
                                                      slices[i].ops.end()));
        }
      }
      return max_op;
    }
    mutable uint64_t max_op = 0;
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
    std::unique_ptr<Table> table = std::make_unique<Table>();
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

  class Array {
   public:
    virtual ~Array() = default;
    virtual std::string Index(absl::string_view value) = 0;
    virtual std::string ArrayName() = 0;
    virtual int Cost() = 0;
  };

  class NamedArray : public Array {
   public:
    explicit NamedArray(std::string name) : name_(std::move(name)) {}
    std::string Index(absl::string_view value) override {
      return absl::StrCat(name_, "[", value, "]");
    }
    std::string ArrayName() override { return name_; }
    int Cost() override { abort(); }

   private:
    std::string name_;
  };

  class IdentityArray : public Array {
   public:
    std::string Index(absl::string_view value) override {
      return std::string(value);
    }
    std::string ArrayName() override { abort(); }
    int Cost() override { return 0; }
  };

  class ConstantArray : public Array {
   public:
    explicit ConstantArray(std::string value) : value_(std::move(value)) {}
    std::string Index(absl::string_view index) override {
      return absl::StrCat("((void)", index, ", ", value_, ")");
    }
    std::string ArrayName() override { abort(); }
    int Cost() override { return 0; }

   private:
    std::string value_;
  };

  class OffsetArray : public Array {
   public:
    explicit OffsetArray(int offset) : offset_(offset) {}
    std::string Index(absl::string_view value) override {
      return absl::StrCat(value, " + ", offset_);
    }
    std::string ArrayName() override { abort(); }
    int Cost() override { return 10; }

   private:
    int offset_;
  };

  class LinearDivideArray : public Array {
   public:
    LinearDivideArray(int offset, int divisor)
        : offset_(offset), divisor_(divisor) {}
    std::string Index(absl::string_view value) override {
      return absl::StrCat(value, "/", divisor_, " + ", offset_);
    }
    std::string ArrayName() override { abort(); }
    int Cost() override { return 20 + (offset_ != 0 ? 10 : 0); }

   private:
    int offset_;
    int divisor_;
  };

  class TwoElemArray : public Array {
   public:
    TwoElemArray(std::string value0, std::string value1)
        : value0_(std::move(value0)), value1_(std::move(value1)) {}
    std::string Index(absl::string_view value) override {
      return absl::StrCat(value, " ? ", value1_, " : ", value0_);
    }
    std::string ArrayName() override { abort(); }
    int Cost() override { return 40; }

   private:
    std::string value0_;
    std::string value1_;
  };

  class Composite2Array : public Array {
   public:
    Composite2Array(std::unique_ptr<Array> a, std::unique_ptr<Array> b,
                    int split)
        : a_(std::move(a)), b_(std::move(b)), split_(split) {}
    std::string Index(absl::string_view value) override {
      return absl::StrCat(
          "(", value, " < ", split_, " ? (", a_->Index(value), ") : (",
          b_->Index(absl::StrCat("(", value, "-", split_, ")")), "))");
    }
    std::string ArrayName() override { abort(); }
    int Cost() override { return 40 + a_->Cost() + b_->Cost(); }

   private:
    std::unique_ptr<Array> a_;
    std::unique_ptr<Array> b_;
    int split_;
  };

  // Helper to generate a compound table (an array of arrays)
  static void GenCompound(int id,
                          const std::vector<std::unique_ptr<Array>>& arrays,
                          std::string ext, std::string type, Sink* global_decls,
                          Sink* global_values) {
    global_decls->Add(absl::StrCat("static const ", type, "* const table", id,
                                   "_", ext, "_[", arrays.size(), "];"));
    global_values->Add(absl::StrCat("const ", type,
                                    "* const HuffDecoderCommon::table", id, "_",
                                    ext, "_[", arrays.size(), "] = {"));
    for (const std::unique_ptr<Array>& array : arrays) {
      global_values->Add(absl::StrCat("  ", array->ArrayName(), ","));
    }
    global_values->Add("};");
  }

  // Try to create a simple function equivalent to a mapping implied by a set of
  // values.
  static const int kMaxArrayToFunctionRecursions = 1;
  template <typename T>
  static std::unique_ptr<Array> ArrayToFunction(
      const std::vector<T>& values,
      int recurse = kMaxArrayToFunctionRecursions) {
    std::unique_ptr<Array> best = nullptr;
    auto note_solution = [&best](std::unique_ptr<Array> a) {
      if (best != nullptr && best->Cost() <= a->Cost()) return;
      best = std::move(a);
    };
    // constant => k,k,k,k,...
    bool is_constant = true;
    for (size_t i = 1; i < values.size(); i++) {
      if (values[i] != values[0]) {
        is_constant = false;
        break;
      }
    }
    if (is_constant) {
      note_solution(std::make_unique<ConstantArray>(absl::StrCat(values[0])));
    }
    // identity => 0,1,2,3,...
    bool is_identity = true;
    for (size_t i = 0; i < values.size(); i++) {
      if (values[i] != i) {
        is_identity = false;
        break;
      }
    }
    if (is_identity) {
      note_solution(std::make_unique<IdentityArray>());
    }
    // offset => k,k+1,k+2,k+3,...
    bool is_offset = true;
    for (size_t i = 1; i < values.size(); i++) {
      if (values[i] - values[0] != i) {
        is_offset = false;
        break;
      }
    }
    if (is_offset) {
      note_solution(std::make_unique<OffsetArray>(values[0]));
    }
    // offset => k,k,k+1,k+1,...
    for (int d = 2; d < 32; d++) {
      bool is_linear = true;
      for (size_t i = 1; i < values.size(); i++) {
        if (values[i] - values[0] != (i / d)) {
          is_linear = false;
          break;
        }
      }
      if (is_linear) {
        note_solution(std::make_unique<LinearDivideArray>(values[0], d));
      }
    }
    // Two items can be resolved with a conditional
    if (values.size() == 2) {
      note_solution(std::make_unique<TwoElemArray>(absl::StrCat(values[0]),
                                                   absl::StrCat(values[1])));
    }
    if ((recurse > 0 && values.size() >= 6) ||
        (recurse == kMaxArrayToFunctionRecursions)) {
      for (size_t i = 1; i < values.size() - 1; i++) {
        std::vector<T> left(values.begin(), values.begin() + i);
        std::vector<T> right(values.begin() + i, values.end());
        std::unique_ptr<Array> left_array = ArrayToFunction(left, recurse - 1);
        std::unique_ptr<Array> right_array =
            ArrayToFunction(right, recurse - 1);
        if (left_array && right_array) {
          note_solution(std::make_unique<Composite2Array>(
              std::move(left_array), std::move(right_array), i));
        }
      }
    }
    return best;
  }

  // Helper to generate an array of values
  template <typename T>
  std::unique_ptr<Array> GenArray(bool force_array, std::string name,
                                  std::string type,
                                  const std::vector<T>& values, bool hex,
                                  Sink* global_decls,
                                  Sink* global_values) const {
    if (!force_array) {
      auto fn = ArrayToFunction(values);
      if (fn != nullptr) return fn;
    }
    auto previous_name =
        ctx_->PreviousNameForArtifact(name, HashVec(type, values));
    if (previous_name.has_value()) {
      return std::make_unique<NamedArray>(absl::StrCat(*previous_name, "_"));
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
    return std::make_unique<NamedArray>(absl::StrCat(name, "_"));
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
        const int bytes_allowed = (64 - i) / 8;
        c->Add(absl::StrCat("return ", ReadBytes(bytes_needed, bytes_allowed),
                            ";"));
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
  std::string ReadBytes(int bytes_needed, int bytes_allowed) {
    auto fn_name =
        absl::StrCat("Read", bytes_needed, "to", bytes_allowed, "Bytes");
    if (have_reads_.count(std::make_pair(bytes_needed, bytes_allowed)) == 0) {
      have_reads_.insert(std::make_pair(bytes_needed, bytes_allowed));
      auto fn = NewFun(fn_name, "bool");
      auto s = fn->Add<Switch>("end_ - begin_");
      for (int i = 0; i <= bytes_allowed; i++) {
        auto c = i == bytes_allowed ? s->Case("") : s->Case(absl::StrCat(i));
        if (i < bytes_needed) {
          c->Add(absl::StrCat("return false;"));
        } else {
          c->Add(absl::StrCat(FillFromInput(i), "();"));
          c->Add("return true;");
        }
      }
    }
    return absl::StrCat(fn_name, "()");
  }

  std::string FillFromInput(int bytes_needed) {
    auto fn_name = absl::StrCat("Fill", bytes_needed);
    if (have_fill_from_input_.count(bytes_needed) == 0) {
      have_fill_from_input_.insert(bytes_needed);
      auto fn = NewFun(fn_name, "void");
      std::string new_value;
      if (bytes_needed == 8) {
        new_value = "0";
      } else {
        new_value = absl::StrCat("(buffer_ << ", 8 * bytes_needed, ")");
      }
      for (int i = 0; i < bytes_needed; i++) {
        absl::StrAppend(&new_value, "| (static_cast<uint64_t>(begin_[", i,
                        "]) << ", 8 * (bytes_needed - i - 1), ")");
      }
      fn->Add(absl::StrCat("buffer_ = ", new_value, ";"));
      fn->Add(absl::StrCat("begin_ += ", bytes_needed, ";"));
      fn->Add(absl::StrCat("buffer_len_ += ", 8 * bytes_needed, ";"));
    }
    return fn_name;
  }

  std::set<int> have_refills_;
  std::set<std::pair<int, int>> have_reads_;
  std::set<int> have_fill_from_input_;
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
  std::string header_name;
  std::string source_name;
  std::string ns;
};

// Given max_bits_for_depth = {n1,n2,n3,...}
// Build a decoder that first considers n1 bits, then n2, then n3, ...
BuildOutput Build(std::vector<int> max_bits_for_depth, bool selected_version) {
  std::string base_name =
      selected_version
          ? "src/core/ext/transport/chttp2/transport/decode_huff"
          : absl::StrCat(
                "test/cpp/microbenchmarks/huffman_geometries/decode_huff_",
                absl::StrJoin(max_bits_for_depth, "_"));
  std::string guard = absl::StrCat(
      "GRPC_",
      absl::AsciiStrToUpper(absl::StrReplaceAll(base_name, {{"/", "_"}})),
      "_H");
  auto hdr = std::make_unique<Sink>();
  auto src = std::make_unique<Sink>();
  hdr->Add<Prelude>("//");
  src->Add<Prelude>("//");
  hdr->Add(absl::StrCat("#ifndef ", guard));
  hdr->Add(absl::StrCat("#define ", guard));
  src->Add(absl::StrCat("#include \"", base_name, ".h\""));
  hdr->Add("#include <cstddef>");
  hdr->Add("#include <grpc/support/port_platform.h>");
  src->Add("#include <grpc/support/port_platform.h>");
  hdr->Add("#include <cstdint>");
  hdr->Add("namespace grpc_core {");
  src->Add("namespace grpc_core {");
  std::string ns;
  if (!selected_version) {
    ns = absl::StrCat("geometry_", absl::StrJoin(max_bits_for_depth, "_"));
    hdr->Add(absl::StrCat("namespace ", ns, " {"));
    src->Add(absl::StrCat("namespace ", ns, " {"));
  }
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
  if (!ns.empty()) {
    hdr->Add("}  // namespace geometry");
  }
  hdr->Add("}  // namespace grpc_core");
  hdr->Add(absl::StrCat("#endif  // ", guard));
  auto global_values = src->Add<Indent>();
  if (!ns.empty()) {
    src->Add("}  // namespace geometry");
  }
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
  return {hdr->ToString(), src->ToString(), absl::StrCat(base_name, ".h"),
          absl::StrCat(base_name, ".cc"), std::move(ns)};
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
    // Restrict first step to 7 bits - smaller is known to generate simply
    // terrible code.
    const int min_step = so_far.empty() ? 7 : 5;
    int sum_so_far = std::accumulate(so_far.begin(), so_far.end(), 0);
    if (so_far.size() > max_depth_ ||
        (so_far.size() == max_depth_ && sum_so_far != 30)) {
      return;
    }
    if (sum_so_far + 5 > 30) {
      perms_.emplace_back(std::move(so_far));
      return;
    }
    for (int i = min_step; i <= std::min(30 - sum_so_far, 16); i++) {
      auto p = so_far;
      p.push_back(i);
      Step(std::move(p));
    }
  }

  const int max_depth_;
  std::vector<std::vector<int>> perms_;
};

// Split after c
std::string SplitAfter(absl::string_view input, char c) {
  return std::vector<std::string>(absl::StrSplit(input, c)).back();
}
std::string SplitBefore(absl::string_view input, char c) {
  return std::vector<std::string>(absl::StrSplit(input, c)).front();
}

// Does what it says.
void WriteFile(std::string filename, std::string content) {
  std::ofstream ofs(filename);
  ofs << content;
}

int main(void) {
  std::vector<std::unique_ptr<BuildOutput>> results;
  std::queue<std::thread> threads;
  // Generate all permutations of max_bits_for_depth for the Build function.
  // Then generate all variations of the code.
  for (const auto& perm : PermutationBuilder(3).Run()) {
    while (threads.size() > 200) {
      threads.front().join();
      threads.pop();
    }
    results.emplace_back(std::make_unique<BuildOutput>());
    threads.emplace(
        [perm, r = results.back().get()] { *r = Build(perm, false); });
  }
  while (!threads.empty()) {
    threads.front().join();
    threads.pop();
  }
  auto index_hdr = std::make_unique<Sink>();
  index_hdr->Add<Prelude>("//");
  index_hdr->Add(
      "#ifndef GRPC_TEST_CPP_MICROBENCHMARKS_HUFFMAN_GEOMETRIES_INDEX_H");
  index_hdr->Add(
      "#define GRPC_TEST_CPP_MICROBENCHMARKS_HUFFMAN_GEOMETRIES_INDEX_H");
  auto index_includes = index_hdr->Add<Sink>();
  index_hdr->Add("#define DECL_HUFFMAN_VARIANTS() \\");
  auto index_decls = index_hdr->Add<Sink>();
  index_hdr->Add("  DECL_BENCHMARK(grpc_core::HuffDecoder, Selected)");
  index_hdr->Add(
      "#endif  // GRPC_TEST_CPP_MICROBENCHMARKS_HUFFMAN_GEOMETRIES_INDEX_H");

  auto index_bzl = std::make_unique<Sink>();
  index_bzl->Add<Prelude>("#");
  index_bzl->Add(
      "load(\"//bazel:grpc_build_system.bzl\", \"grpc_cc_library\", "
      "\"grpc_package\")");
  index_bzl->Add("licenses([\"notice\"])");
  index_bzl->Add(
      "grpc_package(name = \"test/cpp/microbenchmarks/huffman_geometries\", "
      "visibility = \"public\")");

  index_bzl->Add("grpc_cc_library(");
  index_bzl->Add("  name = \"huffman_geometries\",");
  index_bzl->Add("  srcs = [");
  auto index_srcs = index_bzl->Add<Sink>();
  index_bzl->Add("  ],");
  index_bzl->Add("  hdrs = [");
  index_bzl->Add("    \"index.h\",");
  auto index_hdrs = index_bzl->Add<Sink>();
  index_bzl->Add("  ],");
  index_bzl->Add("  deps = [\"//:gpr_platform\"],");
  index_bzl->Add(")");

  for (auto& r : results) {
    index_includes->Add(absl::StrCat("#include \"", r->header_name, "\""));
    index_decls->Add(absl::StrCat("  DECL_BENCHMARK(grpc_core::", r->ns,
                                  "::HuffDecoder, ", r->ns, "); \\"));
    index_hdrs->Add(
        absl::StrCat("    \"", SplitAfter(r->header_name, '/'), "\","));
    index_srcs->Add(
        absl::StrCat("    \"", SplitAfter(r->source_name, '/'), "\","));
    WriteFile(r->header_name, r->header);
    WriteFile(r->source_name, r->source);
  }
  WriteFile("test/cpp/microbenchmarks/huffman_geometries/index.h",
            index_hdr->ToString());
  WriteFile("test/cpp/microbenchmarks/huffman_geometries/BUILD",
            index_bzl->ToString());

  auto selected = Build(std::vector<int>({15, 7, 8}), true);
  WriteFile(selected.header_name, selected.header);
  WriteFile(selected.source_name, selected.source);
  return 0;
}
