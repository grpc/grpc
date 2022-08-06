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

static const int kFirstBits = 9;

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

bool IsMonotonicIncreasing(const std::vector<int>& v) {
  int max = 0;
  for (int i : v) {
    if (i < max) return false;
    max = i;
  }
  return true;
}

std::map<std::string, int> g_emit_buffer_idx;

class EmitBuffer : public Item {
 public:
  explicit EmitBuffer(std::string name)
      : name_(absl::StrCat(name, "_", g_emit_buffer_idx[name]++)) {}

  std::vector<std::string> ToLines() const override {
    // decide whether to do a flat array or a one-level deep thing
    auto vi = ValueInfo();
    const int flat_array_size = emit_.size() * TypeBitsForMax(vi.max);
    const int nested_array_size =
        emit_.size() * TypeBitsForMax(vi.values.size()) +
        vi.values.size() * TypeBitsForMax(vi.max);

    std::vector<std::string> lines;
    lines.push_back(
        absl::StrCat("// max=", vi.max, " unique=", vi.values.size(),
                     " flat=", flat_array_size, " nested=", nested_array_size));
    if (flat_array_size < nested_array_size) {
      GenArray(name_, TypeForMax(vi.max), emit_, &lines);
      lines.push_back(absl::StrCat("inline ", TypeForMax(vi.max), " Get",
                                   ToCamelCase(name_), "(size_t i) { return g_",
                                   name_, "[i]; }"));
    } else {
      std::vector<int> inner;
      std::vector<int> outer;
      std::map<int, int> value_to_inner;
      for (auto v : emit_) {
        auto it = value_to_inner.find(v);
        if (it == value_to_inner.end()) {
          it = value_to_inner.emplace(v, inner.size()).first;
          inner.push_back(v);
        }
        outer.push_back(it->second);
      }
      GenArray(absl::StrCat(name_, "_outer"), TypeForMax(vi.values.size()),
               outer, &lines);
      GenArray(absl::StrCat(name_, "_inner"), TypeForMax(vi.max), inner,
               &lines);
      lines.push_back(absl::StrCat("inline ", TypeForMax(vi.max), " Get",
                                   ToCamelCase(name_), "(size_t i) { return g_",
                                   name_, "_inner[g_", name_, "_outer[i]]; }"));
    }
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

  struct ValueInfoResult {
    int max = 0;
    std::map<int, int> values;
  };

  ValueInfoResult ValueInfo() const {
    ValueInfoResult result;
    for (auto v : emit_) {
      result.max = std::max(result.max, v);
      result.values[v]++;
    }
    return result;
  }

  std::string Accessor(std::string index) {
    return absl::StrCat("Get", ToCamelCase(name_), "(", index, ")");
  }

 private:
  static void GenArray(std::string name, std::string type,
                       const std::vector<int>& values,
                       std::vector<std::string>* lines) {
    if (IsMonotonicIncreasing(values)) {
      lines->push_back("// monotonic increasing");
    }
    lines->push_back(absl::StrCat("static const ", type, " g_", name, "[",
                                  values.size(), "] = {"));
    std::vector<std::string> elems;
    elems.reserve(values.size());
    for (const auto& elem : values) {
      elems.push_back(absl::StrCat(elem));
    }
    lines->push_back(absl::StrCat("  ", absl::StrJoin(elems, ", ")));
    lines->push_back("};");
  }

  std::string name_;
  std::vector<int> emit_;
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

void AddStep(Sink* globals, Sink* out, FunMaker* fun_maker, SymSet start_syms,
             int num_bits, bool is_top, bool refill);

void AddMatchBody(Sink* globals, Sink* out, FunMaker* fun_maker,
                  EmitBuffer* emit_buffer, std::string ofs,
                  const MatchCase& match_case, bool is_top, bool refill) {
  if (absl::holds_alternative<End>(match_case)) {
    out->Add("begin_ = end_;");
    out->Add("buffer_len_ = 0;");
    return;
  }
  if (auto* p = absl::get_if<Unmatched>(&match_case)) {
    if (refill) {
      int max_bits = 0;
      for (auto sym : p->syms) max_bits = std::max(max_bits, sym.bits.length());
      AddStep(globals, fun_maker->CallNewFun("DecodeStep", out), fun_maker,
              p->syms, std::min(max_bits, kFirstBits), false, true);
    }
    return;
  }
  const auto& matched = absl::get<Matched>(match_case);
  for (int i = 0; i < matched.emits; i++) {
    out->Add(absl::StrCat(
        "sink_(", emit_buffer->Accessor(absl::StrCat(ofs, " + ", i)), ");"));
  }
}

void AddStep(Sink* globals, Sink* out, FunMaker* fun_maker, SymSet start_syms,
             int num_bits, bool is_top, bool refill) {
  auto emit_buffer = globals->Add<EmitBuffer>("emit_buffer");
  auto emit_op = globals->Add<EmitBuffer>("emit_op");
  if (refill) {
    out->Add(absl::StrCat("if (!", fun_maker->RefillTo(num_bits), ") {"));
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
  out->Add(absl::StrCat("const auto index = buffer_ >> (buffer_len_ - ",
                        num_bits, ");"));
  std::map<MatchCase, int> match_cases;
  struct Index {
    int match_case;
    int emit_offset;
    int consumed_bits;
  };
  std::vector<Index> indices;
  for (int i = 0; i < (1 << num_bits); i++) {
    auto actions = ActionsFor(BitQueue(i, num_bits), start_syms, is_top);
    Index idx;
    auto add_case = [&match_cases](MatchCase match_case) {
      if (match_cases.find(match_case) == match_cases.end()) {
        match_cases[match_case] = match_cases.size();
      }
      return match_cases[match_case];
    };
    if (actions.emit.size() == 1 && actions.emit[0] == 256) {
      idx = Index{add_case(End{}), 0, actions.consumed};
    } else if (actions.consumed == 0) {
      idx =
          Index{add_case(Unmatched{std::move(actions.remaining)}), 0, num_bits};
    } else {
      idx = Index{add_case(Matched{static_cast<int>(actions.emit.size())}),
                  emit_buffer->OffsetOf(actions.emit), actions.consumed};
    }
    indices.push_back(idx);
  }
  for (auto idx : indices) {
    emit_op->Append((idx.match_case + idx.emit_offset * match_cases.size()) *
                        num_bits +
                    idx.consumed_bits);
  }
  out->Add(absl::StrCat("auto op = ", emit_op->Accessor("index"), ";"));
  out->Add(absl::StrCat("buffer_len_ -= op % ", num_bits, ";"));
  if (match_cases.size() == 1) {
    AddMatchBody(globals, out, fun_maker, emit_buffer, "op",
                 match_cases.begin()->first, is_top, refill);
  } else {
    out->Add(absl::StrCat("op /= ", num_bits, ";"));
    out->Add(
        absl::StrCat("const auto emit_ofs = op / ", match_cases.size(), ";"));
    auto s = out->Add<Switch>(absl::StrCat("op % ", match_cases.size()));
    for (auto kv : match_cases) {
      auto c = s->Case(absl::StrCat(kv.second));
      AddMatchBody(globals, c, fun_maker, emit_buffer, "emit_ofs", kv.first,
                   is_top, refill);
      c->Add("break;");
    }
  }
}

int main(void) {
  auto out = absl::make_unique<Sink>();
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

  // constructor
  pub->Add(
      "HuffDecoder(F sink, const uint8_t* begin, const uint8_t* end) : "
      "sink_(sink), begin_(begin), end_(end) {}");
  // finalizer
  prv->Add("void Done() {");
  auto done = prv->Add<Indent>();
  done->Add(absl::StrCat("if (buffer_len_ < ", kFirstBits - 1, ") {"));
  auto fix = done->Add<Indent>();
  fix->Add(absl::StrCat("buffer_ = (buffer_ << (", kFirstBits - 1,
                        "-buffer_len_)) | "
                        "((uint64_t(1) << (",
                        kFirstBits - 1, " - buffer_len_)) - 1);"));
  fix->Add(absl::StrCat("buffer_len_ = ", kFirstBits - 1, ";"));
  done->Add("}");
  AddStep(globals, done, &fun_maker, AllSyms(), kFirstBits - 1, false, false);
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
  AddStep(globals, body->Add<Indent>(), &fun_maker, AllSyms(), kFirstBits, true,
          true);
  body->Add("}");
  body->Add("return ok_;");
  pub->Add("}");

  puts(out->ToString().c_str());
  return 0;
}
