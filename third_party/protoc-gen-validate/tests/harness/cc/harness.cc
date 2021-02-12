#include <functional>
#include <iostream>

#if defined(WIN32)
#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#endif

#include "validate/validate.h"

#include "tests/harness/cases/bool.pb.h"
#include "tests/harness/cases/bool.pb.validate.h"
#include "tests/harness/cases/bytes.pb.h"
#include "tests/harness/cases/bytes.pb.validate.h"
#include "tests/harness/cases/enums.pb.h"
#include "tests/harness/cases/enums.pb.validate.h"
#include "tests/harness/cases/filename-with-dash.pb.h"
#include "tests/harness/cases/filename-with-dash.pb.validate.h"
#include "tests/harness/cases/maps.pb.h"
#include "tests/harness/cases/maps.pb.validate.h"
#include "tests/harness/cases/messages.pb.h"
#include "tests/harness/cases/messages.pb.validate.h"
#include "tests/harness/cases/numbers.pb.h"
#include "tests/harness/cases/numbers.pb.validate.h"
#include "tests/harness/cases/oneofs.pb.h"
#include "tests/harness/cases/oneofs.pb.validate.h"
#include "tests/harness/cases/repeated.pb.h"
#include "tests/harness/cases/repeated.pb.validate.h"
#include "tests/harness/cases/strings.pb.h"
#include "tests/harness/cases/strings.pb.validate.h"
#include "tests/harness/cases/wkt_any.pb.h"
#include "tests/harness/cases/wkt_any.pb.validate.h"
#include "tests/harness/cases/wkt_duration.pb.h"
#include "tests/harness/cases/wkt_duration.pb.validate.h"
#include "tests/harness/cases/wkt_timestamp.pb.h"
#include "tests/harness/cases/wkt_timestamp.pb.validate.h"
#include "tests/harness/cases/wkt_wrappers.pb.h"
#include "tests/harness/cases/wkt_wrappers.pb.validate.h"
#include "tests/harness/cases/kitchen_sink.pb.h"
#include "tests/harness/cases/kitchen_sink.pb.validate.h"

#include "tests/harness/harness.pb.h"

namespace {

using tests::harness::TestCase;
using tests::harness::TestResult;
using google::protobuf::Any;

std::ostream& operator<<(std::ostream& out, const TestResult& result) {
  out << "valid: " << result.valid() << " reason: '" << result.reason() << "'"
      << std::endl;
  return out;
}

void WriteTestResultAndExit(const TestResult& result) {
  if (!result.SerializeToOstream(&std::cout)) {
    std::cerr << "could not martial response: ";
    std::cerr << result << std::endl;
    exit(EXIT_FAILURE);
  }
  exit(EXIT_SUCCESS);
}

void ExitIfFailed(bool succeeded, const pgv::ValidationMsg& err_msg) {
  if (succeeded) {
    return;
  }

  TestResult result;
  result.set_error(true);
  result.set_reason(pgv::String(err_msg));
  WriteTestResultAndExit(result);
}

std::function<TestResult()> GetValidationCheck(const Any& msg) {
  // This macro is intended to be called once for each message type with the
  // fully-qualified class name passed in as the only argument CLS. It checks
  // whether the msg argument above can be unpacked as a CLS. If so, it returns
  // a lambda that, when called, unpacks the message and validates it as a CLS.
  // This is here to work around the lack of duck-typing in C++, and because the
  // validation function can't be specified as a virtual method on the
  // google::protobuf::Message class.
#define TRY_RETURN_VALIDATE_CALLABLE(CLS) \
  if (msg.Is<CLS>() && !msg.Is<::tests::harness::cases::MessageIgnored>()) { \
    return [msg] () {                                      \
      pgv::ValidationMsg err_msg;                          \
      TestResult result;                                   \
      CLS unpacked;                                        \
      msg.UnpackTo(&unpacked);                             \
      try {                                                \
        result.set_valid(Validate(unpacked, &err_msg));    \
        result.set_reason(std::move(err_msg));             \
      } catch (pgv::UnimplementedException& e) {           \
        /* don't fail for unimplemented validations */     \
        result.set_valid(false);                           \
        result.set_allowfailure(true);                     \
        result.set_reason(e.what());                       \
      }                                                    \
      return result;                                       \
    };                                                     \
  }

  // These macros are defined in the various validation headers and call the
  // above macro once for each message class in the header.
  X_TESTS_HARNESS_CASES_BOOL(TRY_RETURN_VALIDATE_CALLABLE)
  X_TESTS_HARNESS_CASES_BYTES(TRY_RETURN_VALIDATE_CALLABLE)
  X_TESTS_HARNESS_CASES_ENUMS(TRY_RETURN_VALIDATE_CALLABLE)
  X_TESTS_HARNESS_CASES_MAPS(TRY_RETURN_VALIDATE_CALLABLE)
  X_TESTS_HARNESS_CASES_MESSAGES(TRY_RETURN_VALIDATE_CALLABLE)
  X_TESTS_HARNESS_CASES_NUMBERS(TRY_RETURN_VALIDATE_CALLABLE)
  X_TESTS_HARNESS_CASES_ONEOFS(TRY_RETURN_VALIDATE_CALLABLE)
  X_TESTS_HARNESS_CASES_REPEATED(TRY_RETURN_VALIDATE_CALLABLE)
  X_TESTS_HARNESS_CASES_STRINGS(TRY_RETURN_VALIDATE_CALLABLE)
  X_TESTS_HARNESS_CASES_WKT_ANY(TRY_RETURN_VALIDATE_CALLABLE)
  X_TESTS_HARNESS_CASES_WKT_DURATION(TRY_RETURN_VALIDATE_CALLABLE)
  X_TESTS_HARNESS_CASES_WKT_TIMESTAMP(TRY_RETURN_VALIDATE_CALLABLE)
  X_TESTS_HARNESS_CASES_WKT_WRAPPERS(TRY_RETURN_VALIDATE_CALLABLE)
  X_TESTS_HARNESS_CASES_KITCHEN_SINK(TRY_RETURN_VALIDATE_CALLABLE)
  // TODO(akonradi) add macros as the C++ validation code gets fleshed out for
  // more field types.

#undef TRY_RETURN_VALIDATE_CALLABLE

  // TODO(akonradi) remove this once all C++ validation code is done
  return []() {
    TestResult result;
    result.set_valid(false);
    result.set_allowfailure(true);
    result.set_reason("not implemented");
    return result;
  };
}

}  // namespace

int main() {
  TestCase test_case;

#if defined(WIN32)
  // need to explicitly set the stdin file mode to binary on Windows
  ExitIfFailed(_setmode(_fileno(stdin), _O_BINARY) != -1, "failed to set stdin to binary mode");
#endif

  ExitIfFailed(test_case.ParseFromIstream(&std::cin), "failed to parse TestCase");

  auto validate_fn = GetValidationCheck(test_case.message());
  WriteTestResultAndExit(validate_fn());

  return 0;
}
