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

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/types/variant.h"

#include "src/core/ext/transport/chttp2/transport/huffsyms.h"

static const int kFirstBits = 5;

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
  int consumed = 0;

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
        consumed += grpc_chttp2_huffsyms[next_pending[0].symbol].length;
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
  return SymbolActions{std::move(emit), consumed, pending};
}

class Item {
 public:
  virtual ~Item() = default;
  virtual std::vector<std::string> ToLines() const = 0;
  std::string ToString() const { return absl::StrJoin(ToLines(), "\n"); }
};
using ItemPtr = std::unique_ptr<Item>;

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

std::map<std::string, int> g_emit_buffer_idx;

class EmitBuffer : public Item {
 public:
  explicit EmitBuffer(std::string name)
      : name_(absl::StrCat(name, "_", g_emit_buffer_idx[name]++)) {}

  std::vector<std::string> ToLines() const override {
    std::vector<std::string> lines;
    lines.push_back(
        absl::StrCat("static const ", Type(), " ", name_, "[] = {"));
    std::vector<std::string> elems;
    for (const auto& elem : emit_) {
      elems.push_back(absl::StrCat("0x", absl::Hex(elem, absl::kZeroPad2)));
    }
    lines.push_back(absl::StrCat("  ", absl::StrJoin(elems, ", ")));
    lines.push_back("};");
    return lines;
  }

  int OffsetOf(const std::vector<int>& x) {
    auto r = std::search(emit_.begin(), emit_.end(), x.begin(), x.end());
    if (r == emit_.end()) {
      int result = emit_.size();
      for (auto v : x) emit_.push_back(v);
      return result;
    }
    return r - emit_.begin();
  }

  void Append(int x) { emit_.push_back(x); }

  std::string Type() const {
    int max = 0;
    for (auto v : emit_) {
      max = std::max(max, v);
    }
    if (max <= 255) return "uint8_t";
    if (max <= 65535) return "uint16_t";
    return "uint32_t";
  }

  const std::string& name() const { return name_; }

 private:
  std::string name_;
  std::vector<int> emit_;
};

void Sink::Add(std::string s) {
  children_.push_back(absl::make_unique<String>(std::move(s)));
}

void RefillTo(Sink* out, int length, std::string ret) {
  auto w = out->Add<While>(absl::StrCat("buffer_len < ", length));
  w->Add(absl::StrCat("if (begin == end) return ", ret, ";"));
  w->Add("buffer <<= 8;");
  w->Add("buffer |= static_cast<uint64_t>(*begin++);");
  w->Add("buffer_len += 8;");
}

struct Matched {
  int emits;
  int bits_consumed;

  bool operator<(const Matched& other) const {
    return std::tie(emits, bits_consumed) <
           std::tie(other.emits, other.bits_consumed);
  }
};

struct Unmatched {
  SymSet syms;

  bool operator<(const Unmatched& other) const { return syms < other.syms; }
};

void AddStep(Sink* globals, Sink* out, SymSet start_syms, int num_bits,
             bool allow_multiple_emits) {
  auto emit_buffer = globals->Add<EmitBuffer>("g_emit_buffer");
  auto emit_op = globals->Add<EmitBuffer>("g_emit_op");
  RefillTo(out, num_bits, "buffer_len == 0");
  out->Add(absl::StrCat("index = buffer >> (buffer_len - ", num_bits, ");"));
  using MatchCase = absl::variant<Matched, Unmatched>;
  std::map<MatchCase, int> match_cases;
  struct Index {
    int match_case;
    int emit_offset;
  };
  std::vector<Index> indices;
  for (int i = 0; i < (1 << num_bits); i++) {
    auto actions =
        ActionsFor(BitQueue(i, num_bits), start_syms, allow_multiple_emits);
    Index idx;
    auto add_case = [&match_cases](MatchCase match_case) {
      if (match_cases.find(match_case) == match_cases.end()) {
        match_cases[match_case] = match_cases.size();
      }
      return match_cases[match_case];
    };
    if (actions.consumed == 0) {
      idx = Index{add_case(Unmatched{std::move(actions.remaining)}), 0};
      continue;
    } else {
      idx = Index{add_case(Matched{static_cast<int>(actions.emit.size()),
                                   actions.consumed}),
                  emit_buffer->OffsetOf(actions.emit)};
    }
    indices.push_back(idx);
  }
  for (auto idx : indices) {
    emit_op->Append(idx.match_case + idx.emit_offset * match_cases.size());
  }
  out->Add(absl::StrCat("emit_ofs = ", emit_op->name(), "[index] / ",
                        match_cases.size(), ";"));
  auto s = out->Add<Switch>(
      absl::StrCat(emit_op->name(), "[index] % ", match_cases.size()));
  for (auto kv : match_cases) {
    auto c = s->Case(absl::StrCat(kv.second));
    if (auto* p = absl::get_if<Unmatched>(&kv.first)) {
      c->Add(absl::StrCat("// ", SymSetString(p->syms)));
      c->Add(absl::StrCat("buffer_len -= ", num_bits, ";"));
      int max_bits = 0;
      for (auto sym : p->syms) max_bits = std::max(max_bits, sym.bits.length());
      AddStep(globals, c, p->syms, std::min(max_bits, kFirstBits), false);
      c->Add("break;");
      continue;
    }
    const auto& matched = absl::get<Matched>(kv.first);
    for (int i = 0; i < matched.emits; i++) {
      c->Add(
          absl::StrCat("sink(", emit_buffer->name(), "[emit_ofs + ", i, "]);"));
    }
    c->Add(absl::StrCat("buffer_len -= ", matched.bits_consumed, ";"));
    c->Add("goto refill;");
  }
}

int main(void) {
  auto out = absl::make_unique<Sink>();
  out->Add("#include <cstddef>");
  out->Add("#include <cstdint>");
  auto* globals = out->Add<Sink>();
  out->Add("template <typename F>");
  out->Add(
      "bool DecodeHuff(F sink, const uint8_t* begin, const uint8_t* end) {");
  auto init = out->Add<Indent>();
  init->Add("uint64_t buffer = 0;");
  init->Add("uint64_t index;");
  init->Add("size_t emit_ofs;");
  init->Add("int buffer_len = 0;");
  out->Add("refill:");
  AddStep(globals, out->Add<Indent>(), AllSyms(), kFirstBits, true);
  out->Add("  abort();");
  out->Add("}");
  puts(out->ToString().c_str());
  return 0;
}
