//
// Copyright 2026 gRPC authors.
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
//

#include "src/core/xds/grpc/xds_cel.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cel-c/alloc.h"
#include "cel-c/arena.h"
#include "cel-c/ast.h"
#include "cel-c/ast_proto.h"
#include "cel-c/runtime.h"
#include "cel-c/status.h"
#include "cel-c/status_absl.h"
#include "cel-c/string_view_absl.h"
#include "cel-c/value.h"
#include "cel/expr/syntax.upb.h"
#include "google/protobuf/any.upbdefs.h"
#include "google/protobuf/duration.upbdefs.h"
#include "google/protobuf/struct.upbdefs.h"
#include "google/protobuf/timestamp.upbdefs.h"
#include "google/protobuf/wrappers.upbdefs.h"
#include "re2/re2.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/no_destruct.h"
#include "src/core/util/upb_utils.h"
#include "upb/base/string_view.h"
#include "upb/mem/arena.hpp"
#include "upb/reflection/def.hpp"
#include "upb/wire/encode.h"
#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

//
// CelProgram
//

namespace {

// Matches Envoy's regex_max_program_size CEL runtime option.
constexpr int kMaxRegexProgramSize = 100;

// Size of the stack-allocated initial block of the arena used to evaluate a
// program.
constexpr size_t kEvaluationArenaInitialBlockSize = 512;

absl::Status ValidateResultType(const cel_expr_CheckedExpr* checked_expr,
                                int64_t root_id,
                                CelProgram::ResultType result_type) {
  cel_expr_Type* type;
  if (!cel_expr_CheckedExpr_type_map_get(checked_expr, root_id, &type)) {
    return absl::InternalError("CEL expression has no type for its result");
  }
  switch (result_type) {
    case CelProgram::ResultType::kBool:
      if (!cel_expr_Type_has_primitive(type) ||
          cel_expr_Type_primitive(type) != cel_expr_Type_BOOL) {
        return absl::InternalError("CEL expression must evaluate to bool");
      }
      break;
    case CelProgram::ResultType::kString:
      // A string extractor may also be typed dyn, e.g. `request['path']` when
      // the control plane declares `request` as dyn.  Its result is then
      // checked at runtime: gRFC A106 says that CelExtractString.default_value
      // is used if the expression "evaluates to an error or a non-string type".
      if (cel_expr_Type_has_dyn(type)) break;
      if (!cel_expr_Type_has_primitive(type) ||
          cel_expr_Type_primitive(type) != cel_expr_Type_STRING) {
        return absl::InternalError("CEL expression must evaluate to string");
      }
      break;
  }
  return absl::OkStatus();
}

absl::Status ValidateRegex(const cel_expr_Expr* pattern) {
  if (!cel_expr_Expr_has_const_expr(pattern) ||
      !cel_expr_Constant_has_string_value(cel_expr_Expr_const_expr(pattern))) {
    return absl::InternalError(
        "CEL matches() pattern must be a constant string");
  }
  RE2 re(UpbStringToAbsl(
             cel_expr_Constant_string_value(cel_expr_Expr_const_expr(pattern))),
         RE2::Quiet);
  if (!re.ok()) {
    return absl::InternalError(
        absl::StrCat("CEL matches() pattern is invalid: ", re.error()));
  }
  if (re.ProgramSize() > kMaxRegexProgramSize) {
    return absl::InternalError(
        absl::StrCat("CEL matches() pattern program size ", re.ProgramSize(),
                     " exceeds the limit of ", kMaxRegexProgramSize));
  }
  return absl::OkStatus();
}

absl::Status ValidateCall(const cel_expr_CheckedExpr* checked_expr, int64_t id,
                          const cel_expr_Expr_Call* call) {
  absl::string_view function =
      UpbStringToAbsl(cel_expr_Expr_Call_function(call));
  size_t num_args;
  const cel_expr_Expr* const* args = cel_expr_Expr_Call_args(call, &num_args);
  if (function == "string") {
    return absl::InternalError("CEL string() conversions are not supported");
  }
  if (function == "_+_") {
    // Which overload of + this is can only be told from the type-checker's
    // reference map.  Reject if it might be string, bytes or list
    // concatenation.  Envoy disables these via cel-cpp's enable_string_concat
    // (which covers both string and bytes) and enable_list_concat options.
    cel_expr_Reference* reference;
    size_t num_overloads = 0;
    const upb_StringView* overloads = nullptr;
    if (cel_expr_CheckedExpr_reference_map_get(checked_expr, id, &reference)) {
      overloads = cel_expr_Reference_overload_id(reference, &num_overloads);
    }
    if (num_overloads == 0) {
      return absl::InternalError("CEL expression has no overload for +");
    }
    for (size_t i = 0; i < num_overloads; ++i) {
      absl::string_view overload = UpbStringToAbsl(overloads[i]);
      if (overload == "add_string" || overload == "add_bytes" ||
          overload == "add_list") {
        return absl::InternalError(
            "CEL string, bytes and list concatenation are not supported");
      }
    }
  }
  if (function == "matches") {
    // Receiver style x.matches(re) or global style matches(x, re).  Calls
    // with other arities fail to compile.
    const size_t pattern_index = cel_expr_Expr_Call_has_target(call) ? 0 : 1;
    if (num_args == pattern_index + 1) {
      return ValidateRegex(args[pattern_index]);
    }
  }
  return absl::OkStatus();
}

// Checks that a type-checked CEL expression conforms to gRFC A106.
absl::Status ValidateCelExpr(const cel_expr_CheckedExpr* checked_expr,
                             CelProgram::ResultType result_type) {
  if (checked_expr == nullptr) {
    return absl::InternalError("CEL expression not set");
  }
  const cel_expr_Expr* root = cel_expr_CheckedExpr_expr(checked_expr);
  if (root == nullptr) {
    return absl::InternalError("CEL expression not set");
  }
  absl::Status status =
      ValidateResultType(checked_expr, cel_expr_Expr_id(root), result_type);
  if (!status.ok()) return status;
  // Walk the AST iteratively, since it comes from the control plane and
  // may be arbitrarily deep.
  std::vector<const cel_expr_Expr*> stack = {root};
  while (!stack.empty()) {
    const cel_expr_Expr* expr = stack.back();
    stack.pop_back();
    switch (cel_expr_Expr_expr_kind_case(expr)) {
      case cel_expr_Expr_expr_kind_ident_expr: {
        absl::string_view name = UpbStringToAbsl(
            cel_expr_Expr_Ident_name(cel_expr_Expr_ident_expr(expr)));
        // gRFC A106 only supports the `request` variable; gRPC does not provide
        // Envoy's other attributes (e.g. `response`, `connection`). Reject them
        // here so the resource is NACKed, rather than failing on every
        // evaluation.
        if (name != "request") {
          return absl::InternalError(
              absl::StrCat("CEL variable \"", name, "\" is not supported"));
        }
        break;
      }
      case cel_expr_Expr_expr_kind_select_expr: {
        const cel_expr_Expr* operand =
            cel_expr_Expr_Select_operand(cel_expr_Expr_select_expr(expr));
        if (operand != nullptr) stack.push_back(operand);
        break;
      }
      case cel_expr_Expr_expr_kind_call_expr: {
        const cel_expr_Expr_Call* call = cel_expr_Expr_call_expr(expr);
        status = ValidateCall(checked_expr, cel_expr_Expr_id(expr), call);
        if (!status.ok()) return status;
        if (cel_expr_Expr_Call_has_target(call)) {
          stack.push_back(cel_expr_Expr_Call_target(call));
        }
        size_t num_args;
        const cel_expr_Expr* const* args =
            cel_expr_Expr_Call_args(call, &num_args);
        stack.insert(stack.end(), args, args + num_args);
        break;
      }
      case cel_expr_Expr_expr_kind_list_expr: {
        size_t num_elements;
        const cel_expr_Expr* const* elements =
            cel_expr_Expr_CreateList_elements(cel_expr_Expr_list_expr(expr),
                                              &num_elements);
        stack.insert(stack.end(), elements, elements + num_elements);
        break;
      }
      case cel_expr_Expr_expr_kind_struct_expr: {
        size_t num_entries;
        const cel_expr_Expr_CreateStruct_Entry* const* entries =
            cel_expr_Expr_CreateStruct_entries(cel_expr_Expr_struct_expr(expr),
                                               &num_entries);
        for (size_t i = 0; i < num_entries; ++i) {
          const cel_expr_Expr* key =
              cel_expr_Expr_CreateStruct_Entry_map_key(entries[i]);
          const cel_expr_Expr* value =
              cel_expr_Expr_CreateStruct_Entry_value(entries[i]);
          if (key != nullptr) stack.push_back(key);
          if (value != nullptr) stack.push_back(value);
        }
        break;
      }
      case cel_expr_Expr_expr_kind_comprehension_expr:
        return absl::InternalError("CEL comprehensions are not supported");
      case cel_expr_Expr_expr_kind_const_expr:
      case cel_expr_Expr_expr_kind_NOT_SET:
        break;
    }
  }
  return absl::OkStatus();
}

// Deterministically serializes checked_expr for use in equality
// comparisons. Omits source_info (source positions, used only in error
// messages), so that expressions differing only in formatting compare equal.
// Returns nullopt if the expression exceeds upb's encoding depth limit (it
// comes from the control plane, so may be arbitrarily deep) or if allocation
// fails.
std::optional<std::string> SerializeForEquality(
    const cel_expr_CheckedExpr* checked_expr, upb_Arena* arena) {
  // checked_expr is const, so clear source_info on a copy.
  size_t size;
  const char* serialized =
      cel_expr_CheckedExpr_serialize(checked_expr, arena, &size);
  if (serialized == nullptr) return std::nullopt;
  cel_expr_CheckedExpr* copy =
      cel_expr_CheckedExpr_parse(serialized, size, arena);
  if (copy == nullptr) return std::nullopt;
  cel_expr_CheckedExpr_clear_source_info(copy);
  serialized = cel_expr_CheckedExpr_serialize_ex(
      copy, kUpb_EncodeOption_Deterministic, arena, &size);
  if (serialized == nullptr) return std::nullopt;
  return std::string(serialized, size);
}

// RAII wrapper for cel_Status (cel-c doesn't provide one).
class CelStatus final {
 public:
  CelStatus() { cel_Status_Construct(&status_); }
  ~CelStatus() { cel_Status_Destruct(&status_); }
  CelStatus(const CelStatus&) = delete;
  CelStatus& operator=(const CelStatus&) = delete;

  cel_Status* get() { return &status_; }
  bool ok() const { return cel_Status_Ok(&status_); }
  absl::Status ToAbsl() const { return cel_Status_ToAbsl(&status_); }

 private:
  cel_Status status_;
};

struct AstDeleter {
  void operator()(cel_Ast* ast) const { cel_Ast_Delete(ast); }
};
using AstPtr = std::unique_ptr<cel_Ast, AstDeleter>;

struct ActivationDeleter {
  void operator()(cel_Activation* activation) const {
    cel_Activation_Delete(activation);
  }
};
using ActivationPtr = std::unique_ptr<cel_Activation, ActivationDeleter>;

// Process-wide CEL runtime, shared by all programs.  cel_Runtime is
// immutable after creation and ref-counted by the programs compiled from
// it, so it is safe to share across threads.
class SharedCelRuntime final {
 public:
  SharedCelRuntime() {
    // Load the well-known types that cel-c requires into the def pool.
    // Loading any message from a file loads the whole file.
    google_protobuf_Any_getmsgdef(def_pool_.ptr());
    google_protobuf_Duration_getmsgdef(def_pool_.ptr());
    google_protobuf_Struct_getmsgdef(def_pool_.ptr());
    google_protobuf_Timestamp_getmsgdef(def_pool_.ptr());
    google_protobuf_BoolValue_getmsgdef(def_pool_.ptr());
    CelStatus status;
    runtime_ = cel_Runtime_New(cel_DefaultAllocator, def_pool_.ptr(),
                               /*opts=*/nullptr, status.get());
    GRPC_CHECK(runtime_ != nullptr)
        << "failed to create CEL runtime: " << status.ToAbsl();
  }

  const cel_Runtime* runtime() const { return runtime_; }

 private:
  upb::DefPool def_pool_;
  cel_Runtime* runtime_ = nullptr;
};

const cel_Runtime* GetCelRuntime() {
  static NoDestruct<SharedCelRuntime> runtime;
  return runtime->runtime();
}

// Activates and runs program, then passes the result to on_result.
// on_result is not called if evaluation failed or the result is a CEL
// error or unknown.
void Evaluate(const cel_Program* program, const cel_VariableResolver& resolver,
              absl::FunctionRef<void(const cel_Value&)> on_result) {
  // An activation binds program to resolver, which is per-RPC, so it can't
  // be reused across evaluations.  It fails only if allocation fails.
  ActivationPtr activation(
      cel_Program_Activate(program, &resolver, /*opts=*/nullptr));
  GRPC_CHECK_NE(activation.get(), nullptr);
  // Holds values created during evaluation.  Its initial block is on the
  // stack, so that evaluations needing little memory don't allocate.
  char initial_block[kEvaluationArenaInitialBlockSize];
  upb::Arena arena(initial_block, sizeof(initial_block));
  cel_Value result;
  CelStatus status;
  if (!cel_Activation_Execute(activation.get(), &result, arena.ptr(),
                              status.get()) ||
      !status.ok()) {
    return;
  }
  if (cel_Value_IsError(&result) || cel_Value_IsUnknown(&result)) return;
  // The result may reference memory in arena, so it must be consumed
  // before arena is destroyed.
  on_result(result);
}

}  // namespace

absl::StatusOr<std::unique_ptr<CelProgram>> CelProgram::Create(
    const cel_expr_CheckedExpr* checked_expr, ResultType result_type) {
  absl::Status validation = ValidateCelExpr(checked_expr, result_type);
  if (!validation.ok()) return validation;
  upb::Arena arena;
  std::optional<std::string> serialized =
      SerializeForEquality(checked_expr, arena.ptr());
  if (!serialized.has_value()) {
    return absl::InternalError("failed to serialize CEL expression");
  }
  // Convert to a cel-c AST (cel_Arena is upb_Arena) and compile it.  The
  // compiled program copies everything it needs, so the AST and its arena
  // may be freed afterwards.
  CelStatus status;
  AstPtr ast(cel_Ast_FromProto(checked_expr, arena.ptr(), status.get()));
  if (ast == nullptr || !status.ok()) {
    return absl::InternalError(absl::StrCat(
        "failed to convert CEL expression: ", status.ToAbsl().message()));
  }
  ProgramPtr program(cel_Runtime_Compile(GetCelRuntime(), ast.get(),
                                         /*opts=*/nullptr, status.get()));
  if (program == nullptr || !status.ok()) {
    return absl::InternalError(absl::StrCat(
        "failed to compile CEL expression: ", status.ToAbsl().message()));
  }
  return std::unique_ptr<CelProgram>(
      new CelProgram(std::move(*serialized), std::move(program)));
}

std::optional<bool> CelProgram::EvaluateBool(
    const cel_VariableResolver& resolver) const {
  std::optional<bool> result;
  Evaluate(program_.get(), resolver, [&](const cel_Value& value) {
    if (cel_Value_IsBool(&value)) result = cel_Value_GetBool(&value);
  });
  return result;
}

std::optional<std::string> CelProgram::EvaluateString(
    const cel_VariableResolver& resolver) const {
  std::optional<std::string> result;
  Evaluate(program_.get(), resolver, [&](const cel_Value& value) {
    if (cel_Value_IsString(&value)) {
      result = std::string(cel_StringView_ToAbsl(cel_Value_GetString(&value)));
    }
  });
  return result;
}

std::string CelProgram::ToString() const {
  return absl::StrCat("CelProgram{checked_expr_size=", serialized_expr_.size(),
                      "}");
}

//
// CelStringExtractor
//

std::string CelStringExtractor::ToString() const {
  return absl::StrCat("CelStringExtractor{", program_->ToString(),
                      default_value_.has_value()
                          ? absl::StrCat(", default_value=", *default_value_)
                          : "",
                      "}");
}

}  // namespace grpc_core
