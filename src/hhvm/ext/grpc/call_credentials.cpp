/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "channel_credentials.h"
#include "call_credentials.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "hhvm_grpc.h"

#include "call.h"

#include <zend_exceptions.h>
#include <zend_hash.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>

class CallCredentialsWrapper {
  private:
    grpc_call_credentials* wrapped{nullptr};
  public:
    CallCredentialsWrapper() {}
    ~CallCredentialsWrapper() { sweep(); }

    void new(grpc_call_credentials* call_credentials) {
      memcpy(wrapped, call_credentials, sizeof(grpc_call_credentials));
    }

    void sweep() {
      if (wrapped) {
        grpc_call_credentials_release(wrapped);
        req::free(wrapped);
        wrapped = nullptr;
      }
    }

    grpc_call_credentials* getWrapped() {
      return wrapped;
    }
}

/**
 * Create composite credentials from two existing credentials.
 * @param CallCredentials $cred1_obj The first credential
 * @param CallCredentials $cred2_obj The second credential
 * @return CallCredentials The new composite credentials object
 */
Object HHVM_METHOD(CallCredentials, createComposite,
  const Object& cred1_obj,
  const Object& cred2_obj) {
  auto callCredentialsWrapper1 = Native::data<CallCredentialsWrapper>(cred1_obj);
  auto callCredentialsWrapper2 = Native::data<CallCredentialsWrapper>(cred2_obj);

  grpc_call_credentials *call_credentials =
        grpc_composite_call_credentials_create(callCredentialsWrapper1->getWrapped(),
                                               callCredentialsWrapper2->getWrapped(),
                                               NULL);

  auto newCallCredentialsWrapper = req::make<CallCredentialsWrapper>(call_credentials);

  return Object(std::move(newCallCredentialsWrapper));
}

/**
 * Create a call credentials object from the plugin API
 * @param function $fci The callback function
 * @return CallCredentials The new call credentials object
 */
Object HHVM_METHOD(CallCredentials, createFromPlugin,
  const Variant& callback) {
  ...
}


