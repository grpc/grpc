// Copyright 2019 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "absl/status/status.h"

#include <errno.h>

#include <cassert>
#include <utility>

#include "absl/base/internal/raw_logging.h"
#include "absl/base/internal/strerror.h"
#include "absl/base/macros.h"
#include "absl/debugging/stacktrace.h"
#include "absl/debugging/symbolize.h"
#include "absl/status/status_payload_printer.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

std::string StatusCodeToString(StatusCode code) {
  switch (code) {
    case StatusCode::kOk:
      return "OK";
    case StatusCode::kCancelled:
      return "CANCELLED";
    case StatusCode::kUnknown:
      return "UNKNOWN";
    case StatusCode::kInvalidArgument:
      return "INVALID_ARGUMENT";
    case StatusCode::kDeadlineExceeded:
      return "DEADLINE_EXCEEDED";
    case StatusCode::kNotFound:
      return "NOT_FOUND";
    case StatusCode::kAlreadyExists:
      return "ALREADY_EXISTS";
    case StatusCode::kPermissionDenied:
      return "PERMISSION_DENIED";
    case StatusCode::kUnauthenticated:
      return "UNAUTHENTICATED";
    case StatusCode::kResourceExhausted:
      return "RESOURCE_EXHAUSTED";
    case StatusCode::kFailedPrecondition:
      return "FAILED_PRECONDITION";
    case StatusCode::kAborted:
      return "ABORTED";
    case StatusCode::kOutOfRange:
      return "OUT_OF_RANGE";
    case StatusCode::kUnimplemented:
      return "UNIMPLEMENTED";
    case StatusCode::kInternal:
      return "INTERNAL";
    case StatusCode::kUnavailable:
      return "UNAVAILABLE";
    case StatusCode::kDataLoss:
      return "DATA_LOSS";
    default:
      return "";
  }
}

std::ostream& operator<<(std::ostream& os, StatusCode code) {
  return os << StatusCodeToString(code);
}

namespace status_internal {

static absl::optional<size_t> FindPayloadIndexByUrl(
    const Payloads* payloads,
    absl::string_view type_url) {
  if (payloads == nullptr)
    return absl::nullopt;

  for (size_t i = 0; i < payloads->size(); ++i) {
    if ((*payloads)[i].type_url == type_url) return i;
  }

  return absl::nullopt;
}

// Convert canonical code to a value known to this binary.
absl::StatusCode MapToLocalCode(int value) {
  absl::StatusCode code = static_cast<absl::StatusCode>(value);
  switch (code) {
    case absl::StatusCode::kOk:
    case absl::StatusCode::kCancelled:
    case absl::StatusCode::kUnknown:
    case absl::StatusCode::kInvalidArgument:
    case absl::StatusCode::kDeadlineExceeded:
    case absl::StatusCode::kNotFound:
    case absl::StatusCode::kAlreadyExists:
    case absl::StatusCode::kPermissionDenied:
    case absl::StatusCode::kResourceExhausted:
    case absl::StatusCode::kFailedPrecondition:
    case absl::StatusCode::kAborted:
    case absl::StatusCode::kOutOfRange:
    case absl::StatusCode::kUnimplemented:
    case absl::StatusCode::kInternal:
    case absl::StatusCode::kUnavailable:
    case absl::StatusCode::kDataLoss:
    case absl::StatusCode::kUnauthenticated:
      return code;
    default:
      return absl::StatusCode::kUnknown;
  }
}
}  // namespace status_internal

absl::optional<absl::Cord> Status::GetPayload(
    absl::string_view type_url) const {
  const auto* payloads = GetPayloads();
  absl::optional<size_t> index =
      status_internal::FindPayloadIndexByUrl(payloads, type_url);
  if (index.has_value())
    return (*payloads)[index.value()].payload;

  return absl::nullopt;
}

void Status::SetPayload(absl::string_view type_url, absl::Cord payload) {
  if (ok()) return;

  PrepareToModify();

  status_internal::StatusRep* rep = RepToPointer(rep_);
  if (!rep->payloads) {
    rep->payloads = absl::make_unique<status_internal::Payloads>();
  }

  absl::optional<size_t> index =
      status_internal::FindPayloadIndexByUrl(rep->payloads.get(), type_url);
  if (index.has_value()) {
    (*rep->payloads)[index.value()].payload = std::move(payload);
    return;
  }

  rep->payloads->push_back({std::string(type_url), std::move(payload)});
}

bool Status::ErasePayload(absl::string_view type_url) {
  absl::optional<size_t> index =
      status_internal::FindPayloadIndexByUrl(GetPayloads(), type_url);
  if (index.has_value()) {
    PrepareToModify();
    GetPayloads()->erase(GetPayloads()->begin() + index.value());
    if (GetPayloads()->empty() && message().empty()) {
      // Special case: If this can be represented inlined, it MUST be
      // inlined (EqualsSlow depends on this behavior).
      StatusCode c = static_cast<StatusCode>(raw_code());
      Unref(rep_);
      rep_ = CodeToInlinedRep(c);
    }
    return true;
  }

  return false;
}

void Status::ForEachPayload(
    absl::FunctionRef<void(absl::string_view, const absl::Cord&)> visitor)
    const {
  if (auto* payloads = GetPayloads()) {
    bool in_reverse =
        payloads->size() > 1 && reinterpret_cast<uintptr_t>(payloads) % 13 > 6;

    for (size_t index = 0; index < payloads->size(); ++index) {
      const auto& elem =
          (*payloads)[in_reverse ? payloads->size() - 1 - index : index];

#ifdef NDEBUG
      visitor(elem.type_url, elem.payload);
#else
      // In debug mode invalidate the type url to prevent users from relying on
      // this string lifetime.

      // NOLINTNEXTLINE intentional extra conversion to force temporary.
      visitor(std::string(elem.type_url), elem.payload);
#endif  // NDEBUG
    }
  }
}

const std::string* Status::EmptyString() {
  static union EmptyString {
    std::string str;
    ~EmptyString() {}
  } empty = {{}};
  return &empty.str;
}

#ifdef ABSL_INTERNAL_NEED_REDUNDANT_CONSTEXPR_DECL
constexpr const char Status::kMovedFromString[];
#endif

const std::string* Status::MovedFromString() {
  static std::string* moved_from_string = new std::string(kMovedFromString);
  return moved_from_string;
}

void Status::UnrefNonInlined(uintptr_t rep) {
  status_internal::StatusRep* r = RepToPointer(rep);
  // Fast path: if ref==1, there is no need for a RefCountDec (since
  // this is the only reference and therefore no other thread is
  // allowed to be mucking with r).
  if (r->ref.load(std::memory_order_acquire) == 1 ||
      r->ref.fetch_sub(1, std::memory_order_acq_rel) - 1 == 0) {
    delete r;
  }
}

Status::Status(absl::StatusCode code, absl::string_view msg)
    : rep_(CodeToInlinedRep(code)) {
  if (code != absl::StatusCode::kOk && !msg.empty()) {
    rep_ = PointerToRep(new status_internal::StatusRep(code, msg, nullptr));
  }
}

int Status::raw_code() const {
  if (IsInlined(rep_)) {
    return static_cast<int>(InlinedRepToCode(rep_));
  }
  status_internal::StatusRep* rep = RepToPointer(rep_);
  return static_cast<int>(rep->code);
}

absl::StatusCode Status::code() const {
  return status_internal::MapToLocalCode(raw_code());
}

void Status::PrepareToModify() {
  ABSL_RAW_CHECK(!ok(), "PrepareToModify shouldn't be called on OK status.");
  if (IsInlined(rep_)) {
    rep_ = PointerToRep(new status_internal::StatusRep(
        static_cast<absl::StatusCode>(raw_code()), absl::string_view(),
        nullptr));
    return;
  }

  uintptr_t rep_i = rep_;
  status_internal::StatusRep* rep = RepToPointer(rep_);
  if (rep->ref.load(std::memory_order_acquire) != 1) {
    std::unique_ptr<status_internal::Payloads> payloads;
    if (rep->payloads) {
      payloads = absl::make_unique<status_internal::Payloads>(*rep->payloads);
    }
    status_internal::StatusRep* const new_rep = new status_internal::StatusRep(
        rep->code, message(), std::move(payloads));
    rep_ = PointerToRep(new_rep);
    UnrefNonInlined(rep_i);
  }
}

bool Status::EqualsSlow(const absl::Status& a, const absl::Status& b) {
  if (IsInlined(a.rep_) != IsInlined(b.rep_)) return false;
  if (a.message() != b.message()) return false;
  if (a.raw_code() != b.raw_code()) return false;
  if (a.GetPayloads() == b.GetPayloads()) return true;

  const status_internal::Payloads no_payloads;
  const status_internal::Payloads* larger_payloads =
      a.GetPayloads() ? a.GetPayloads() : &no_payloads;
  const status_internal::Payloads* smaller_payloads =
      b.GetPayloads() ? b.GetPayloads() : &no_payloads;
  if (larger_payloads->size() < smaller_payloads->size()) {
    std::swap(larger_payloads, smaller_payloads);
  }
  if ((larger_payloads->size() - smaller_payloads->size()) > 1) return false;
  // Payloads can be ordered differently, so we can't just compare payload
  // vectors.
  for (const auto& payload : *larger_payloads) {

    bool found = false;
    for (const auto& other_payload : *smaller_payloads) {
      if (payload.type_url == other_payload.type_url) {
        if (payload.payload != other_payload.payload) {
          return false;
        }
        found = true;
        break;
      }
    }
    if (!found) return false;
  }
  return true;
}

std::string Status::ToStringSlow(StatusToStringMode mode) const {
  std::string text;
  absl::StrAppend(&text, absl::StatusCodeToString(code()), ": ", message());

  const bool with_payload = (mode & StatusToStringMode::kWithPayload) ==
                      StatusToStringMode::kWithPayload;

  if (with_payload) {
    status_internal::StatusPayloadPrinter printer =
        status_internal::GetStatusPayloadPrinter();
    this->ForEachPayload([&](absl::string_view type_url,
                             const absl::Cord& payload) {
      absl::optional<std::string> result;
      if (printer) result = printer(type_url, payload);
      absl::StrAppend(
          &text, " [", type_url, "='",
          result.has_value() ? *result : absl::CHexEscape(std::string(payload)),
          "']");
    });
  }

  return text;
}

std::ostream& operator<<(std::ostream& os, const Status& x) {
  os << x.ToString(StatusToStringMode::kWithEverything);
  return os;
}

Status AbortedError(absl::string_view message) {
  return Status(absl::StatusCode::kAborted, message);
}

Status AlreadyExistsError(absl::string_view message) {
  return Status(absl::StatusCode::kAlreadyExists, message);
}

Status CancelledError(absl::string_view message) {
  return Status(absl::StatusCode::kCancelled, message);
}

Status DataLossError(absl::string_view message) {
  return Status(absl::StatusCode::kDataLoss, message);
}

Status DeadlineExceededError(absl::string_view message) {
  return Status(absl::StatusCode::kDeadlineExceeded, message);
}

Status FailedPreconditionError(absl::string_view message) {
  return Status(absl::StatusCode::kFailedPrecondition, message);
}

Status InternalError(absl::string_view message) {
  return Status(absl::StatusCode::kInternal, message);
}

Status InvalidArgumentError(absl::string_view message) {
  return Status(absl::StatusCode::kInvalidArgument, message);
}

Status NotFoundError(absl::string_view message) {
  return Status(absl::StatusCode::kNotFound, message);
}

Status OutOfRangeError(absl::string_view message) {
  return Status(absl::StatusCode::kOutOfRange, message);
}

Status PermissionDeniedError(absl::string_view message) {
  return Status(absl::StatusCode::kPermissionDenied, message);
}

Status ResourceExhaustedError(absl::string_view message) {
  return Status(absl::StatusCode::kResourceExhausted, message);
}

Status UnauthenticatedError(absl::string_view message) {
  return Status(absl::StatusCode::kUnauthenticated, message);
}

Status UnavailableError(absl::string_view message) {
  return Status(absl::StatusCode::kUnavailable, message);
}

Status UnimplementedError(absl::string_view message) {
  return Status(absl::StatusCode::kUnimplemented, message);
}

Status UnknownError(absl::string_view message) {
  return Status(absl::StatusCode::kUnknown, message);
}

bool IsAborted(const Status& status) {
  return status.code() == absl::StatusCode::kAborted;
}

bool IsAlreadyExists(const Status& status) {
  return status.code() == absl::StatusCode::kAlreadyExists;
}

bool IsCancelled(const Status& status) {
  return status.code() == absl::StatusCode::kCancelled;
}

bool IsDataLoss(const Status& status) {
  return status.code() == absl::StatusCode::kDataLoss;
}

bool IsDeadlineExceeded(const Status& status) {
  return status.code() == absl::StatusCode::kDeadlineExceeded;
}

bool IsFailedPrecondition(const Status& status) {
  return status.code() == absl::StatusCode::kFailedPrecondition;
}

bool IsInternal(const Status& status) {
  return status.code() == absl::StatusCode::kInternal;
}

bool IsInvalidArgument(const Status& status) {
  return status.code() == absl::StatusCode::kInvalidArgument;
}

bool IsNotFound(const Status& status) {
  return status.code() == absl::StatusCode::kNotFound;
}

bool IsOutOfRange(const Status& status) {
  return status.code() == absl::StatusCode::kOutOfRange;
}

bool IsPermissionDenied(const Status& status) {
  return status.code() == absl::StatusCode::kPermissionDenied;
}

bool IsResourceExhausted(const Status& status) {
  return status.code() == absl::StatusCode::kResourceExhausted;
}

bool IsUnauthenticated(const Status& status) {
  return status.code() == absl::StatusCode::kUnauthenticated;
}

bool IsUnavailable(const Status& status) {
  return status.code() == absl::StatusCode::kUnavailable;
}

bool IsUnimplemented(const Status& status) {
  return status.code() == absl::StatusCode::kUnimplemented;
}

bool IsUnknown(const Status& status) {
  return status.code() == absl::StatusCode::kUnknown;
}

StatusCode ErrnoToStatusCode(int error_number) {
  switch (error_number) {
    case 0:
      return StatusCode::kOk;
    case EINVAL:        // Invalid argument
    case ENAMETOOLONG:  // Filename too long
    case E2BIG:         // Argument list too long
    case EDESTADDRREQ:  // Destination address required
    case EDOM:          // Mathematics argument out of domain of function
    case EFAULT:        // Bad address
    case EILSEQ:        // Illegal byte sequence
    case ENOPROTOOPT:   // Protocol not available
    case ENOSTR:        // Not a STREAM
    case ENOTSOCK:      // Not a socket
    case ENOTTY:        // Inappropriate I/O control operation
    case EPROTOTYPE:    // Protocol wrong type for socket
    case ESPIPE:        // Invalid seek
      return StatusCode::kInvalidArgument;
    case ETIMEDOUT:  // Connection timed out
    case ETIME:      // Timer expired
      return StatusCode::kDeadlineExceeded;
    case ENODEV:  // No such device
    case ENOENT:  // No such file or directory
#ifdef ENOMEDIUM
    case ENOMEDIUM:  // No medium found
#endif
    case ENXIO:  // No such device or address
    case ESRCH:  // No such process
      return StatusCode::kNotFound;
    case EEXIST:         // File exists
    case EADDRNOTAVAIL:  // Address not available
    case EALREADY:       // Connection already in progress
#ifdef ENOTUNIQ
    case ENOTUNIQ:  // Name not unique on network
#endif
      return StatusCode::kAlreadyExists;
    case EPERM:   // Operation not permitted
    case EACCES:  // Permission denied
#ifdef ENOKEY
    case ENOKEY:  // Required key not available
#endif
    case EROFS:  // Read only file system
      return StatusCode::kPermissionDenied;
    case ENOTEMPTY:   // Directory not empty
    case EISDIR:      // Is a directory
    case ENOTDIR:     // Not a directory
    case EADDRINUSE:  // Address already in use
    case EBADF:       // Invalid file descriptor
#ifdef EBADFD
    case EBADFD:  // File descriptor in bad state
#endif
    case EBUSY:    // Device or resource busy
    case ECHILD:   // No child processes
    case EISCONN:  // Socket is connected
#ifdef EISNAM
    case EISNAM:  // Is a named type file
#endif
#ifdef ENOTBLK
    case ENOTBLK:  // Block device required
#endif
    case ENOTCONN:  // The socket is not connected
    case EPIPE:     // Broken pipe
#ifdef ESHUTDOWN
    case ESHUTDOWN:  // Cannot send after transport endpoint shutdown
#endif
    case ETXTBSY:  // Text file busy
#ifdef EUNATCH
    case EUNATCH:  // Protocol driver not attached
#endif
      return StatusCode::kFailedPrecondition;
    case ENOSPC:  // No space left on device
#ifdef EDQUOT
    case EDQUOT:  // Disk quota exceeded
#endif
    case EMFILE:   // Too many open files
    case EMLINK:   // Too many links
    case ENFILE:   // Too many open files in system
    case ENOBUFS:  // No buffer space available
    case ENODATA:  // No message is available on the STREAM read queue
    case ENOMEM:   // Not enough space
    case ENOSR:    // No STREAM resources
#ifdef EUSERS
    case EUSERS:  // Too many users
#endif
      return StatusCode::kResourceExhausted;
#ifdef ECHRNG
    case ECHRNG:  // Channel number out of range
#endif
    case EFBIG:      // File too large
    case EOVERFLOW:  // Value too large to be stored in data type
    case ERANGE:     // Result too large
      return StatusCode::kOutOfRange;
#ifdef ENOPKG
    case ENOPKG:  // Package not installed
#endif
    case ENOSYS:        // Function not implemented
    case ENOTSUP:       // Operation not supported
    case EAFNOSUPPORT:  // Address family not supported
#ifdef EPFNOSUPPORT
    case EPFNOSUPPORT:  // Protocol family not supported
#endif
    case EPROTONOSUPPORT:  // Protocol not supported
#ifdef ESOCKTNOSUPPORT
    case ESOCKTNOSUPPORT:  // Socket type not supported
#endif
    case EXDEV:  // Improper link
      return StatusCode::kUnimplemented;
    case EAGAIN:  // Resource temporarily unavailable
#ifdef ECOMM
    case ECOMM:  // Communication error on send
#endif
    case ECONNREFUSED:  // Connection refused
    case ECONNABORTED:  // Connection aborted
    case ECONNRESET:    // Connection reset
    case EINTR:         // Interrupted function call
#ifdef EHOSTDOWN
    case EHOSTDOWN:  // Host is down
#endif
    case EHOSTUNREACH:  // Host is unreachable
    case ENETDOWN:      // Network is down
    case ENETRESET:     // Connection aborted by network
    case ENETUNREACH:   // Network unreachable
    case ENOLCK:        // No locks available
    case ENOLINK:       // Link has been severed
#ifdef ENONET
    case ENONET:  // Machine is not on the network
#endif
      return StatusCode::kUnavailable;
    case EDEADLK:  // Resource deadlock avoided
#ifdef ESTALE
    case ESTALE:  // Stale file handle
#endif
      return StatusCode::kAborted;
    case ECANCELED:  // Operation cancelled
      return StatusCode::kCancelled;
    default:
      return StatusCode::kUnknown;
  }
}

namespace {
std::string MessageForErrnoToStatus(int error_number,
                                    absl::string_view message) {
  return absl::StrCat(message, ": ",
                      absl::base_internal::StrError(error_number));
}
}  // namespace

Status ErrnoToStatus(int error_number, absl::string_view message) {
  return Status(ErrnoToStatusCode(error_number),
                MessageForErrnoToStatus(error_number, message));
}

namespace status_internal {

std::string* MakeCheckFailString(const absl::Status* status,
                                 const char* prefix) {
  return new std::string(
      absl::StrCat(prefix, " (",
                   status->ToString(StatusToStringMode::kWithEverything), ")"));
}

}  // namespace status_internal

ABSL_NAMESPACE_END
}  // namespace absl
