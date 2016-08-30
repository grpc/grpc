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

import UIKit

import RemoteTest

class ViewController: UIViewController {

  override func viewDidLoad() {
    super.viewDidLoad()

    let RemoteHost = "grpc-test.sandbox.googleapis.com"

    let request = RMTSimpleRequest()
    request.responseSize = 10
    request.fillUsername = true
    request.fillOauthScope = true


    // Example gRPC call using a generated proto client library:

    let service = RMTTestService(host: RemoteHost)
    service.unaryCallWithRequest(request) { response, error in
      if let response = response {
        NSLog("1. Finished successfully with response:\n\(response)")
      } else {
        NSLog("1. Finished with error: \(error!)")
      }
    }


    // Same but manipulating headers:

    var RPC : GRPCProtoCall! // Needed to convince Swift to capture by reference (__block)
    RPC = service.RPCToUnaryCallWithRequest(request) { response, error in
      if let response = response {
        NSLog("2. Finished successfully with response:\n\(response)")
      } else {
        NSLog("2. Finished with error: \(error!)")
      }
      NSLog("2. Response headers: \(RPC.responseHeaders)")
      NSLog("2. Response trailers: \(RPC.responseTrailers)")
    }

    // TODO(jcanizales): Revert to using subscript syntax once XCode 8 is released.
    RPC.requestHeaders.setObject("My value", forKey: "My-Header")

    RPC.start()


    // Same example call using the generic gRPC client library:

    let method = GRPCProtoMethod(package: "grpc.testing", service: "TestService", method: "UnaryCall")

    let requestsWriter = GRXWriter(value: request.data())

    let call = GRPCCall(host: RemoteHost, path: method.HTTPPath, requestsWriter: requestsWriter)

    call.requestHeaders.setObject("My value", forKey: "My-Header")

    call.startWithWriteable(GRXWriteable { response, error in
      if let response = response as? NSData {
        NSLog("3. Received response:\n\(try! RMTSimpleResponse(data: response))")
      } else {
        NSLog("3. Finished with error: \(error!)")
      }
      NSLog("3. Response headers: \(call.responseHeaders)")
      NSLog("3. Response trailers: \(call.responseTrailers)")
    })
  }
}
