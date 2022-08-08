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

struct Hash {
  uint8_t bytes[SHA256_DIGEST_LENGTH];
  bool operator==(const Hash& other) const {
    return memcmp(bytes, other.bytes, SHA256_DIGEST_LENGTH) == 0;
  }
  bool operator<(const Hash& other) const {
    return memcmp(bytes, other.bytes, SHA256_DIGEST_LENGTH) < 0;
  }
};

template <typename T>
Hash HashVec(const std::vector<T>& v) {
  Hash h;
  SHA1(reinterpret_cast<const uint8_t*>(v.data()), v.size() * sizeof(T),
       h.bytes);
  return h;
}

class BitQueue {
 public:
  BitQueue(unsigned mask, int len) : mask_(mask), len_(len) {}
  BitQueue() : BitQueue(0, 0) {}

  int Top() const { return (mask_ >> (len_ - 1)) & 1; }
  void Pop() {
    mask_ &= ~(1 << (len_ - 1));
    len_--;
  }
  bool Empty() const { return len_ == 0; }
  int length() const { return len_; }
  unsigned mask() const { return mask_; }

  std::string ToString() const {
    return absl::StrCat(absl::Hex(mask_), "/", len_);
  }

  bool operator<(const BitQueue& other) const {
    return std::tie(mask_, len_) < std::tie(other.mask_, other.len_);
  }

 private:
  unsigned mask_;
  int len_;
};

struct Sym {
  BitQueue bits;
  int symbol;

  bool operator<(const Sym& other) const {
    return std::tie(bits, symbol) < std::tie(other.bits, other.symbol);
  }
};

using SymSet = std::vector<Sym>;

std::string SymSetString(const SymSet& syms) {
  std::vector<std::string> parts;
  for (const Sym& sym : syms) {
    parts.push_back(absl::StrCat(sym.symbol, ":", sym.bits.ToString()));
  }
  return absl::StrJoin(parts, ",");
}

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

struct SymbolActions {
  std::vector<int> emit;
  int consumed;
  SymSet remaining;
};

SymbolActions ActionsFor(BitQueue index, SymSet pending, bool allow_multiple) {
  std::vector<int> emit;
  int len_start = index.length();
  int len_consume = len_start;

  while (!index.Empty()) {
    SymSet next_pending;
    for (auto sym : pending) {
      if (sym.bits.Top() != index.Top()) continue;
      sym.bits.Pop();
      next_pending.push_back(sym);
    }
    switch (next_pending.size()) {
      case 0:
        abort();
      case 1:
        if (!next_pending[0].bits.Empty()) abort();
        emit.push_back(next_pending[0].symbol);
        len_consume = index.length() - 1;
        if (!allow_multiple) goto done;
        pending = AllSyms();
        break;
      default:
        pending = std::move(next_pending);
        break;
    }
    index.Pop();
  }
done:
  return SymbolActions{std::move(emit), len_start - len_consume, pending};
}

class Item {
 public:
  virtual ~Item() = default;
  virtual std::vector<std::string> ToLines() const = 0;
  std::string ToString() const { return absl::StrJoin(ToLines(), "\n"); }
};
using ItemPtr = std::unique_ptr<Item>;

class Copyright final : public Item {
 public:
  std::vector<std::string> ToLines() const {
    return {
        "// Copyright 2022 gRPC authors.",
        "//",
        "// Licensed under the Apache License, Version 2.0 (the \"License\");",
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
    };
  }
};

class Switch;

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

  void Add(std::string s);
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

class TableBuilder;
class FunMaker;

struct Matched {
  int emits;

  bool operator<(const Matched& other) const { return emits < other.emits; }
};

struct Unmatched {
  SymSet syms;

  bool operator<(const Unmatched& other) const { return syms < other.syms; }
};

struct End {
  bool operator<(End) const { return false; }
};

using MatchCase = absl::variant<Matched, Unmatched, End>;

class BuildCtx {
 public:
  BuildCtx(std::vector<int> max_bits_for_depth, Sink* globals,
           FunMaker* fun_maker)
      : max_bits_for_depth_(std::move(max_bits_for_depth)),
        globals_(globals),
        fun_maker_(fun_maker) {}

  void AddStep(SymSet start_syms, int num_bits, bool is_top, bool refill,
               int depth, Sink* out);
  void AddMatchBody(TableBuilder* table_builder, std::string index,
                    std::string ofs, const MatchCase& match_case, bool is_top,
                    bool refill, int depth, Sink* out);

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

 private:
  const std::vector<int> max_bits_for_depth_;
  std::map<Hash, std::string> arrays_;
  int next_id_ = 1;
  Sink* const globals_;
  FunMaker* const fun_maker_;
};

class String : public Item {
 public:
  explicit String(std::string s) : s_(std::move(s)) {}
  std::vector<std::string> ToLines() const override { return {s_}; }

 private:
  std::string s_;
};

std::vector<std::string> IndentLines(std::vector<std::string> lines,
                                     int n = 1) {
  std::string indent(2 * n, ' ');
  for (auto& line : lines) {
    line = absl::StrCat(indent, line);
  }
  return lines;
}

class Indent : public Sink {
 public:
  std::vector<std::string> ToLines() const override {
    return IndentLines(Sink::ToLines());
  }
};

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

class Switch : public Item {
 public:
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

std::string ToCamelCase(const std::string& in) {
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

std::string Uint(int bits) { return absl::StrCat("uint", bits, "_t"); }

int TypeBitsForMax(int max) {
  if (max <= 255) {
    return 8;
  } else if (max <= 65535) {
    return 16;
  } else {
    return 32;
  }
}

std::string TypeForMax(int max) { return Uint(TypeBitsForMax(max)); }

int BitsForMaxValue(int x) {
  int n = 0;
  while (x >= (1 << n)) n++;
  return n;
}

class TableBuilder : public Item {
 public:
  explicit TableBuilder(BuildCtx* ctx) : ctx_(ctx), id_(ctx->NewId()) {}

  void Add(int match_case, std::vector<uint8_t> emit, int consumed_bits) {
    elems_.push_back({match_case, std::move(emit), consumed_bits});
    max_consumed_bits_ = std::max(max_consumed_bits_, consumed_bits);
    max_match_case_ = std::max(max_match_case_, match_case);
  }

  std::vector<std::string> ToLines() const override {
    return Choose()->ToLines(this, BitsForMaxValue(elems_.size() - 1));
  }

  std::string EmitAccessor(std::string index, std::string offset) {
    return absl::StrCat("GetEmit", id_, "(", index, ", ", offset, ")");
  }

  std::string OpAccessor(std::string index) {
    return absl::StrCat("GetOp", id_, "(", index, ")");
  }

  int ConsumeBits() const { return BitsForMaxValue(max_consumed_bits_); }
  int MatchBits() const { return BitsForMaxValue(max_match_case_); }

 private:
  struct Elem {
    int match_case;
    std::vector<uint8_t> emit;
    int consumed_bits;
  };

  struct NestedSlice {
    std::vector<uint8_t> emit;
    std::vector<uint64_t> inner;
    std::vector<int> outer;

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

  struct Slice {
    std::vector<uint8_t> emit;
    std::vector<uint64_t> ops;

    size_t OpsSize() const {
      return ops.size() *
             TypeBitsForMax(*std::max_element(ops.begin(), ops.end()));
    }

    size_t EmitSize() const { return emit.size() * 8; }

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

  struct EncodeOption {
    virtual size_t Size() const = 0;
    virtual std::vector<std::string> ToLines(const TableBuilder* builder,
                                             int op_bits) const = 0;
    virtual ~EncodeOption() {}
  };

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
    std::vector<std::string> ToLines(const TableBuilder* builder,
                                     int op_bits) const override {
      const int id = builder->id_;
      std::vector<std::string> lines;
      const uint64_t max_inner = MaxInner();
      const uint64_t max_outer = MaxOuter();
      for (size_t i = 0; i < slices.size(); i++) {
        builder->GenArray(absl::StrCat("table", id, "_", i, "_emit"), "uint8_t",
                          slices[i].emit, true, &lines);
        builder->GenArray(absl::StrCat("table", id, "_", i, "_inner"),
                          TypeForMax(max_inner), slices[i].inner, true, &lines);
        builder->GenArray(absl::StrCat("table", id, "_", i, "_outer"),
                          TypeForMax(max_outer), slices[i].outer, false,
                          &lines);
      }
      if (slice_bits == 0) {
        lines.push_back(absl::StrCat(
            "inline uint64_t GetOp", id, "(size_t i) { return g_table", id,
            "_0_inner[g_table", id, "_0_outer[i]]; }"));
        lines.push_back(absl::StrCat("inline uint64_t GetEmit", id,
                                     "(size_t, size_t emit) { return g_table",
                                     id, "_0_emit[emit]; }"));
      } else {
        GenCompound(id, slices.size(), "emit", "uint8_t", &lines);
        GenCompound(id, slices.size(), "inner", TypeForMax(max_inner), &lines);
        GenCompound(id, slices.size(), "outer", TypeForMax(max_outer), &lines);
        lines.push_back(absl::StrCat(
            "inline uint64_t GetOp", id, "(size_t i) { return g_table", id,
            "_inner[i >> ", op_bits - slice_bits, "][g_table", id,
            "_outer[i >> ", op_bits - slice_bits, "][i & 0x",
            absl::Hex((1 << (op_bits - slice_bits)) - 1), "]]; }"));
        lines.push_back(absl::StrCat("inline uint64_t GetEmit", id,
                                     "(size_t i, size_t emit) { return g_table",
                                     id, "_emit[i >> ", op_bits - slice_bits,
                                     "][emit]; }"));
      }
      return lines;
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
    std::vector<std::string> ToLines(const TableBuilder* builder,
                                     int op_bits) const override {
      std::vector<std::string> lines;
      uint64_t max_op = MaxOp();
      const int id = builder->id_;
      for (size_t i = 0; i < slices.size(); i++) {
        builder->GenArray(absl::StrCat("table", id, "_", i, "_emit"), "uint8_t",
                          slices[i].emit, true, &lines);
        builder->GenArray(absl::StrCat("table", id, "_", i, "_ops"),
                          TypeForMax(max_op), slices[i].ops, true, &lines);
      }
      if (slice_bits == 0) {
        lines.push_back(absl::StrCat("inline uint64_t GetOp", id,
                                     "(size_t i) { return g_table", id,
                                     "_0_ops[i]; }"));
        lines.push_back(absl::StrCat("inline uint64_t GetEmit", id,
                                     "(size_t, size_t emit) { return g_table",
                                     id, "_0_emit[emit]; }"));
      } else {
        GenCompound(id, slices.size(), "emit", "uint8_t", &lines);
        GenCompound(id, slices.size(), "ops", TypeForMax(max_op), &lines);
        lines.push_back(absl::StrCat(
            "inline uint64_t GetOp", id, "(size_t i) { return g_table", id,
            "_ops[i >> ", op_bits - slice_bits, "][i & 0x",
            absl::Hex((1 << (op_bits - slice_bits)) - 1), "]; }"));
        lines.push_back(absl::StrCat("inline uint64_t GetEmit", id,
                                     "(size_t i, size_t emit) { return g_table",
                                     id, "_emit[i >> ", op_bits - slice_bits,
                                     "][emit]; }"));
      }
      return lines;
    }
    uint64_t MaxOp() const {
      uint64_t max_op = 0;
      for (size_t i = 0; i < slices.size(); i++) {
        max_op = std::max(max_op, *std::max_element(slices[i].ops.begin(),
                                                    slices[i].ops.end()));
      }
      return max_op;
    }
    std::unique_ptr<NestedTable> MakeNestedTable() {
      std::unique_ptr<NestedTable> result(new NestedTable);
      result->slice_bits = slice_bits;
      for (const auto& slice : slices) {
        result->slices.push_back(slice.MakeNestedSlice());
      }
      return result;
    }
  };

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

  static void GenCompound(int id, int slices, std::string ext, std::string type,
                          std::vector<std::string>* lines) {
    lines->push_back(absl::StrCat("static const ", type, "* const g_table", id,
                                  "_", ext, "[] = {"));
    for (int i = 0; i < slices; i++) {
      lines->push_back(absl::StrCat("  g_table", id, "_", i, "_", ext, ","));
    }
    lines->push_back("};");
  }

  template <typename T>
  void GenArray(std::string name, std::string type,
                const std::vector<T>& values, bool hex,
                std::vector<std::string>* lines) const {
    auto previous_name = ctx_->PreviousNameForArtifact(name, HashVec(values));
    if (previous_name.has_value()) {
      lines->push_back(absl::StrCat("#define g_", name, " g_", *previous_name));
      return;
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
    lines->push_back(absl::StrCat("static const ", type, " g_", name, "[",
                                  values.size(), "] = {"));
    lines->push_back(absl::StrCat("  ", data));
    lines->push_back("};");
  }

  std::unique_ptr<EncodeOption> Choose() const {
    std::unique_ptr<EncodeOption> chosen;
    struct Measure {
      std::unique_ptr<EncodeOption> best;
      size_t best_size;
      std::thread thread;
      Measure(const TableBuilder* builder, size_t slice_bits)
          : thread{[this, builder, slice_bits] {
              auto raw = builder->MakeTable(slice_bits);
              std::unique_ptr<NestedTable> nested;
              size_t nested_size;
              std::thread measure_nested([&raw, &nested, &nested_size] {
                nested = raw->MakeNestedTable();
                nested_size = nested->Size();
              });
              size_t raw_size = raw->Size();
              measure_nested.join();
              if (raw_size < best_size) {
                best = std::move(raw);
                best_size = raw_size;
              } else {
                best = std::move(nested);
                best_size = nested_size;
              }
            }} {};
    };
    std::vector<std::unique_ptr<Measure>> options;
    for (size_t slice_bits = 0; (1 << slice_bits) < elems_.size();
         slice_bits++) {
      options.emplace_back(absl::make_unique<Measure>(this, slice_bits));
    }
    size_t best_size = std::numeric_limits<size_t>::max();
    for (auto& option : options) {
      option->thread.join();
      if (option->best_size < best_size) {
        chosen = std::move(option->best);
        best_size = option->best_size;
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

void Sink::Add(std::string s) {
  children_.push_back(absl::make_unique<String>(std::move(s)));
}

class FunMaker {
 public:
  explicit FunMaker(Sink* sink) : sink_(sink) {}

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

  Sink* AssignNewFun(std::string base_name, std::string returns,
                     std::string var, Sink* callsite) {
    std::string name = absl::StrCat(base_name, have_funs_[base_name]++);
    callsite->Add(absl::StrCat("const auto ", var, "=", name, "();"));
    return NewFun(name, returns);
  }

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

  std::string ReadBytes(int bytes_needed) {
    if (have_reads_.count(bytes_needed) == 0) {
      have_reads_.insert(bytes_needed);
      auto fn = NewFun(absl::StrCat("Read", bytes_needed), "bool");
      fn->Add(
          absl::StrCat("if (begin_+", bytes_needed, " > end_) return false;"));
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

void BuildCtx::AddStep(SymSet start_syms, int num_bits, bool is_top,
                       bool refill, int depth, Sink* out) {
  auto table_builder = globals_->Add<TableBuilder>(this);
  if (refill) {
    out->Add(absl::StrCat("if (!", fun_maker_->RefillTo(num_bits), ") {"));
    auto ifblk = out->Add<Indent>();
    if (!is_top) {
      ifblk->Add("ok_ = false;");
      ifblk->Add("return;");
    } else {
      ifblk->Add("Done();");
      ifblk->Add("return ok_;");
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
      table_builder->Add(add_case(End{}), {}, actions.consumed);
    } else if (actions.consumed == 0) {
      table_builder->Add(add_case(Unmatched{std::move(actions.remaining)}), {},
                         num_bits);
    } else {
      std::vector<uint8_t> emit;
      for (auto sym : actions.emit) emit.push_back(sym);
      table_builder->Add(
          add_case(Matched{static_cast<int>(actions.emit.size())}),
          std::move(emit), actions.consumed);
    }
  }
  out->Add(absl::StrCat("const auto op = ", table_builder->OpAccessor("index"),
                        ";"));
  out->Add(absl::StrCat("buffer_len_ -= op & ",
                        (1 << table_builder->ConsumeBits()) - 1, ";"));
  out->Add(absl::StrCat(
      "const auto emit_ofs = op >> ",
      table_builder->ConsumeBits() + table_builder->MatchBits(), ";"));
  if (match_cases.size() == 1) {
    AddMatchBody(table_builder, "index", "emit_ofs", match_cases.begin()->first,
                 is_top, refill, depth, out);
  } else {
    auto s = out->Add<Switch>(
        absl::StrCat("(op >> ", table_builder->ConsumeBits(), ") & ",
                     (1 << table_builder->MatchBits()) - 1));
    for (auto kv : match_cases) {
      auto c = s->Case(absl::StrCat(kv.second));
      AddMatchBody(table_builder, "index", "emit_ofs", kv.first, is_top, refill,
                   depth, c);
      c->Add("break;");
    }
  }
}

std::string Build(std::vector<int> max_bits_for_depth) {
  auto out = absl::make_unique<Sink>();
  out->Add<Copyright>();
  out->Add(
      "// This file is autogenerated: see "
      "tools/codegen/core/gen_huffman_decompressor.h");
  out->Add("#ifndef GRPC_THIS_IS_INCLUDED_FROM_THE_HPACK_PARSER");
  out->Add("#error This file should only be included from the hpack parser");
  out->Add("#endif");
  out->Add(
      absl::StrCat("// GEOMETRY: ", absl::StrJoin(max_bits_for_depth, ",")));
  out->Add("#include <cstddef>");
  out->Add("#include <cstdint>");
  out->Add("#include <stdlib.h>");
  auto* globals = out->Add<Sink>();
  out->Add("template<typename F> class HuffDecoder {");
  out->Add(" public:");
  auto pub = out->Add<Indent>();
  out->Add(" private:");
  auto prv = out->Add<Indent>();
  FunMaker fun_maker(prv->Add<Sink>());
  out->Add("};");
  BuildCtx ctx(std::move(max_bits_for_depth), globals, &fun_maker);
  // constructor
  pub->Add(
      "HuffDecoder(F sink, const uint8_t* begin, const uint8_t* end) : "
      "sink_(sink), begin_(begin), end_(end) {}");
  // finalizer
  prv->Add("void Done() {");
  auto done = prv->Add<Indent>();
  done->Add(absl::StrCat("if (buffer_len_ < ", ctx.MaxBitsForTop() - 1, ") {"));
  auto fix = done->Add<Indent>();
  fix->Add(absl::StrCat("buffer_ = (buffer_ << (", ctx.MaxBitsForTop() - 1,
                        "-buffer_len_)) | "
                        "((uint64_t(1) << (",
                        ctx.MaxBitsForTop() - 1, " - buffer_len_)) - 1);"));
  fix->Add(absl::StrCat("buffer_len_ = ", ctx.MaxBitsForTop() - 1, ";"));
  done->Add("}");
  ctx.AddStep(AllSyms(), ctx.MaxBitsForTop() - 1, false, false, 1, done);
  done->Add("if (buffer_len_ == 0) return;");
  done->Add("const uint64_t mask = (1 << buffer_len_) - 1;");
  done->Add(absl::StrCat("if ((buffer_ & mask) != mask) ok_ = false;"));
  prv->Add("}");
  // members
  prv->Add("F sink_;");
  prv->Add("const uint8_t* begin_;");
  prv->Add("const uint8_t* const end_;");
  prv->Add("uint64_t buffer_ = 0;");
  prv->Add("int buffer_len_ = 0;");
  prv->Add("bool ok_ = true;");
  // main fn
  pub->Add("bool Run() {");
  auto body = pub->Add<Indent>();
  body->Add("while (ok_) {");
  ctx.AddStep(AllSyms(), ctx.MaxBitsForTop(), true, true, 0,
              body->Add<Indent>());
  body->Add("}");
  body->Add("return ok_;");
  pub->Add("}");
  return out->ToString();
}

class PermBuilder {
 public:
  explicit PermBuilder(int max_depth) : max_depth_(max_depth) {}
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

int main(void) {
  std::string best;
  size_t best_len = std::numeric_limits<size_t>::max();
  std::vector<std::unique_ptr<std::string>> results;
  std::queue<std::thread> threads;
  for (auto perm : PermBuilder(3).Run()) {
    while (threads.size() > 200) {
      threads.front().join();
      threads.pop();
    }
    results.emplace_back(absl::make_unique<std::string>());
    threads.emplace([perm, r = results.back().get()] { *r = Build(perm); });
  }
  while (!threads.empty()) {
    threads.front().join();
    threads.pop();
  }
  for (auto& r : results) {
    size_t l = r->length();
    if (l < best_len) {
      best_len = l;
      best = std::move(*r);
    }
  }
  puts(best.c_str());
  return 0;
}
