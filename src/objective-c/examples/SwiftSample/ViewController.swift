/*
 *
 * Copyright 2015 gRPC authors.
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
    service.unaryCall(with: request) { response, error in
      if let response = response {
        NSLog("1. Finished successfully with response:\n\(response)")
      } else {
        NSLog("1. Finished with error: \(error!)")
      }
    }


    // Same but manipulating headers:

    var RPC : GRPCProtoCall! // Needed to convince Swift to capture by reference (__block)
    RPC = service.rpcToUnaryCall(with: request) { response, error in
      if let response = response {
        NSLog("2. Finished successfully with response:\n\(response)")
      } else {
        NSLog("2. Finished with error: \(error!)")
      }
      NSLog("2. Response headers: \(String(describing: RPC.responseHeaders))")
      NSLog("2. Response trailers: \(String(describing: RPC.responseTrailers))")
    }

    // TODO(jcanizales): Revert to using subscript syntax once XCode 8 is released.
    RPC.requestHeaders["My-Header"] = "My value"

    RPC.start()


    // Same example call using the generic gRPC client library:

    let method = GRPCProtoMethod(package: "grpc.testing", service: "TestService", method: "UnaryCall")!

    let requestsWriter = GRXWriter(value: request.data())!

    let call = GRPCCall(host: RemoteHost, path: method.httpPath, requestsWriter: requestsWriter)!

    call.requestHeaders["My-Header"] = "My value"

    call.start(with: GRXWriteable { response, error in
      if let response = response as? Data {
        NSLog("3. Received response:\n\(try! RMTSimpleResponse(data: response))")
      } else {
        NSLog("3. Finished with error: \(error!)")
      }
      NSLog("3. Response headers: \(String(describing: call.responseHeaders))")
      NSLog("3. Response trailers: \(String(describing: call.responseTrailers))")
    })
  }
}
