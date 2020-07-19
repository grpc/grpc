//
// Created by dohyunc on 7/6/20.
//

#ifndef GRPC_CEL_STUB_H
#define GRPC_CEL_STUB_H
//#include "google/api/expr/v1alpha1/syntax.pb.h"
#include "../../../../../../../../usr/include/c++/8/bits/unique_ptr.h"
#include "../../../../../../../../usr/include/c++/8/memory"
//namespace google {
//namespace api {
//namespace expr {
//namespace runtime {

namespace cel_base {
template <typename T>
class StatusOr {
 public:
  using element_type = T;
  StatusOr(const T& value);
};

class Status {
 public:
  Status();
};
}

using CelError = cel_base::Status;
class CelValue {
 public:
  CelValue();
};

class Activation {
 public:
  Activation();
//  void InsertValue(absl::string_view name, const CelValue& value);
};


class CelExpression {
 public:
  CelExpression() {}
//TODO implement a StatusOr<CelValue> substitute
//    virtual cel_base::StatusOr<CelValue> Evaluate(const Activation& activation,
//    google::protobuf::Arena* arena) const = 0;
//   virtual ~CelExpression() {}

};

class CelExpressionFlatImpl : public CelExpression {
 public:
  CelExpressionFlatImpl() {}
};

struct CelExpressionBuilder {
 public:
  CelExpressionBuilder() {}
//   virtual ~CelExpressionBuilder() {}
//TODO:: Change the return object to the one down below?
//  virtual cel_base::StatusOr<std::unique_ptr<CelExpression>> CreateExpression(
//      const google::api::expr::v1alpha1::Expr* expr,
//      const google::api::expr::v1alpha1::SourceInfo* source_info) const = 0;
//}
  CelExpression CreateExpression() {
    CelExpression* expr = new CelExpression();
    return *expr;
  }
};

struct cel_stub {
 public:
  cel_stub() {
  };
  CelExpressionBuilder create_cell_expresion_builder() {
    CelExpressionBuilder* builder = new CelExpressionBuilder();
    return *builder;
  }

};


//}  // namespace runtime
//}  // namespace expr
//}  // namespace api
//}  // namespace google

#endif  // GRPC_CEL_STUB_H
