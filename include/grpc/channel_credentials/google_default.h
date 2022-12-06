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

#ifndef GRPC_CHANNEL_CREDENTIALS_GOOGLE_DEFAULT_H
#define GRPC_CHANNEL_CREDENTIALS_GOOGLE_DEFAULT_H

#include <grpc/support/port_platform.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Creates default credentials to connect to a google gRPC service.
   WARNING: Do NOT use this credentials to connect to a non-google service as
   this could result in an oauth2 token leak. The security level of the
   resulting connection is GRPC_PRIVACY_AND_INTEGRITY.

   If specified, the supplied call credentials object will be attached to the
   returned channel credentials object. The call_credentials object must remain
   valid throughout the lifetime of the returned grpc_channel_credentials
   object. It is expected that the call credentials object was generated
   according to the Application Default Credentials mechanism and asserts the
   identity of the default service account of the machine. Supplying any other
   sort of call credential will result in undefined behavior, up to and
   including the sudden and unexpected failure of RPCs.

   If nullptr is supplied, the returned channel credentials object will use a
   call credentials object based on the Application Default Credentials
   mechanism.
*/
GRPCAPI grpc_channel_credentials* grpc_google_default_credentials_create(
    grpc_call_credentials* call_credentials);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CHANNEL_CREDENTIALS_GOOGLE_DEFAULT_H */
