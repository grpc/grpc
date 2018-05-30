/*
 *
 * Copyright 2018 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#ifdef GRPC_CFSTREAM
#include <CoreFoundation/CoreFoundation.h>

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/iomgr/error.h"

#define MAX_ERROR_DESCRIPTION 256

grpc_error* grpc_error_create_from_cferror(const char* file, int line,
                                           void* arg, const char* custom_desc) {
  CFErrorRef error = static_cast<CFErrorRef>(arg);
  char buf_domain[MAX_ERROR_DESCRIPTION];
  char buf_desc[MAX_ERROR_DESCRIPTION];
  char* error_msg;
  CFErrorDomain domain = CFErrorGetDomain((error));
  CFIndex code = CFErrorGetCode((error));
  CFStringRef desc = CFErrorCopyDescription((error));
  CFStringGetCString(domain, buf_domain, MAX_ERROR_DESCRIPTION,
                     kCFStringEncodingUTF8);
  CFStringGetCString(desc, buf_desc, MAX_ERROR_DESCRIPTION,
                     kCFStringEncodingUTF8);
  gpr_asprintf(&error_msg, "%s (error domain:%s, code:%ld, description:%s)",
               custom_desc, buf_domain, code, buf_desc);
  CFRelease(desc);
  grpc_error* return_error = grpc_error_create(
      file, line, grpc_slice_from_copied_string(error_msg), NULL, 0);
  gpr_free(error_msg);
  return return_error;
}
#endif /* GRPC_CFSTREAM */
