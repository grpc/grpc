//
//
// Copyright 2016 gRPC authors.
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
//

#ifndef GRPC_SRC_CORE_LIB_TRANSPORT_ERROR_UTILS_H
#define GRPC_SRC_CORE_LIB_TRANSPORT_ERROR_UTILS_H

#include <grpc/status.h>
#include <grpc/support/port_platform.h>

#include <string>

#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/util/time.h"
#include "absl/status/status.h"

/// A utility function to get the status code and message to be returned
/// to the application.  If not set in the top-level message, looks
/// through child errors until it finds the first one with these attributes.
/// All attributes are pulled from the same child error. error_string will
/// be populated with the entire error string. If any of the attributes (code,
/// msg, http_status, error_string) are unneeded, they can be passed as
/// NULL.
void grpc_error_get_status(grpc_error_handle error,
                           grpc_core::Timestamp deadline,
                           grpc_status_code* code, std::string* message,
                           grpc_core::http2::Http2ErrorCode* http_error,
                           const char** error_string);

#endif  // GRPC_SRC_CORE_LIB_TRANSPORT_ERROR_UTILS_H
