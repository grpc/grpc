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

#include "src/core/ext/transport/chttp2/transport/huffsyms.h"

static const int kFirstBits = 10;

class BitQueue {
 public:
  BitQueue(unsigned mask, int len) : mask_(mask), len_(len) {}
  BitQueue() : BitQueue(0, 0) {}

  int Top() const { return (mask_ >> (len_ - 1)) & 1; }
  void Pop() { len_--; }
  bool Empty() const { return len_ == 0; }
  int length() const { return len_; }
  unsigned mask() const { return mask_; }

  std::string ToString() const {
    return absl::StrCat(absl::Hex(mask_), "/", len_);
  }

 private:
  unsigned mask_;
  int len_;
};

struct Sym {
  BitQueue bits;
  int symbol;
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

SymSet BigSyms() {
  SymSet syms;
  for (int i = 0; i < GRPC_CHTTP2_NUM_HUFFSYMS; i++) {
    if (grpc_chttp2_huffsyms[i].length <= kFirstBits) continue;
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
};

SymbolActions ActionsFor(BitQueue index) {
  std::vector<int> emit;
  SymSet pending = AllSyms();
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
        pending = AllSyms();
        break;
      default:
        pending = std::move(next_pending);
        break;
    }
    index.Pop();
  }
  return SymbolActions{std::move(emit), consumed};
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

class EmitBuffer : public Item {
 public:
  explicit EmitBuffer(std::string name) : name_(std::move(name)) {}

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

int main(void) {
  auto out = absl::make_unique<Sink>();
  out->Add("#include <cstddef>");
  out->Add("#include <cstdint>");
  auto emit_buffer = out->Add<EmitBuffer>("g_emit_buffer");
  auto emit_op = out->Add<EmitBuffer>("g_emit_op");
  out->Add("template <typename F>");
  out->Add(
      "bool DecodeHuff(F sink, const uint8_t* begin, const uint8_t* end) {");
  auto init = out->Add<Indent>();
  init->Add("uint64_t buffer = 0;");
  init->Add("uint64_t index;");
  init->Add("size_t emit_ofs;");
  init->Add("int buffer_len = 0;");
  init->Add("uint8_t emit_slow;");
  out->Add("refill:");
  RefillTo(out->Add<Indent>(), kFirstBits, "buffer_len == 0");
  out->Add<Indent>()->Add(
      absl::StrCat("index = buffer >> (buffer_len - ", kFirstBits, ");"));
  std::map<std::pair<int, int>, int> consumed_cases{{{0, 0}, 0}};
  struct Index {
    int consumed_case;
    int emit_offset;
  };
  std::vector<Index> indices;
  for (int i = 0; i < (1 << kFirstBits); i++) {
    const auto actions = ActionsFor(BitQueue(i, kFirstBits));
    if (actions.consumed == 0) {
      indices.push_back({0, 0});
      continue;
    }
    auto consumed_case_index =
        std::pair<int, int>(actions.consumed, actions.emit.size());
    if (consumed_cases.find(consumed_case_index) == consumed_cases.end()) {
      consumed_cases.emplace(consumed_case_index, consumed_cases.size());
    }
    indices.push_back(Index{
        consumed_cases.find(consumed_case_index)->second,
        emit_buffer->OffsetOf(actions.emit),
    });
  }
  for (auto idx : indices) {
    emit_op->Append(idx.consumed_case +
                    idx.emit_offset * consumed_cases.size());
  }
  out->Add<Indent>()->Add(absl::StrCat("emit_ofs = g_emit_op[index] / ",
                                       consumed_cases.size(), ";"));
  auto s = out->Add<Indent>()->Add<Switch>(
      absl::StrCat("g_emit_op[index] % ", consumed_cases.size()));
  for (auto kv : consumed_cases) {
    auto c = s->Case(absl::StrCat(kv.second));
    if (kv.second == 0) {
      c->Add("break;");
      continue;
    }
    for (int i = 0; i < kv.first.second; i++) {
      c->Add(absl::StrCat("sink(g_emit_buffer[emit_ofs + ", i, "]);"));
    }
    c->Add(absl::StrCat("buffer_len -= ", kv.first.first, ";"));
    c->Add("goto refill;");
  }
  std::map<int, SymSet> symbols_by_length;
  for (Sym sym : BigSyms()) symbols_by_length[sym.bits.length()].push_back(sym);
  auto slow = out->Add<Indent>();
  for (const auto& kv : symbols_by_length) {
    RefillTo(slow, kv.first, "false");
    auto s = slow->Add<Switch>(
        absl::StrCat("buffer >> (buffer_len - ", kv.first, ")"));
    for (Sym sym : kv.second) {
      auto c = s->Case(
          absl::StrCat("0x", absl::Hex(sym.bits.mask(), absl::kZeroPad2)));
      if (sym.symbol == 256) {
        c->Add(absl::StrCat("return buffer_len == ", kv.first, ";"));
      } else {
        c->Add(absl::StrCat("emit_slow = ", sym.symbol, ";"));
        c->Add(absl::StrCat("goto slow_", kv.first, ";"));
      }
    }
  }
  slow->Add("return false;");
  for (const auto& kv : symbols_by_length) {
    out->Add(absl::StrCat("slow_", kv.first, ":"));
    auto s = out->Add<Indent>();
    s->Add(absl::StrCat("sink(emit_slow);"));
    s->Add(absl::StrCat("buffer_len -= ", kv.first, ";"));
    s->Add("goto refill;");
  }
  out->Add("}");
  puts(out->ToString().c_str());
  return 0;
}
