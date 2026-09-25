/*
 *
 * Copyright 2026 gRPC authors.
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
import GRPCClient
import ProtoRPC
import RxLibrary

class ViewController: UIViewController {

  private let statusLabel: UILabel = {
    let label = UILabel()
    label.translatesAutoresizingMaskIntoConstraints = false
    label.numberOfLines = 0
    label.textAlignment = .center
    label.font = UIFont.monospacedSystemFont(ofSize: 15, weight: .medium)
    return label
  }()

  override func viewDidLoad() {
    super.viewDidLoad()

    view.backgroundColor = .systemBackground
    view.addSubview(statusLabel)
    NSLayoutConstraint.activate([
      statusLabel.leadingAnchor.constraint(equalTo: view.safeAreaLayoutGuide.leadingAnchor, constant: 20),
      statusLabel.trailingAnchor.constraint(equalTo: view.safeAreaLayoutGuide.trailingAnchor, constant: -20),
      statusLabel.centerYAnchor.constraint(equalTo: view.centerYAnchor),
    ])

    let remoteHost = "grpc-test.sandbox.googleapis.com"
    let method = GRPCProtoMethod(package: "grpc.testing", service: "TestService", method: "UnaryCall")!

    statusLabel.text = """
    gRPC SPM Sample
    Host: \(remoteHost)
    Method: \(method.httpPath!)
    Status: Calling...
    """
    NSLog("[SPMSample] Starting call to \(remoteHost)\(method.httpPath!)")

    let options = GRPCMutableCallOptions()
    options.transport = GRPCDefaultTransportImplList.core_secure

    let requestsWriter = GRXWriter(value: Data())!
    let call = GRPCCall(host: remoteHost, path: method.httpPath, requestsWriter: requestsWriter)!
    call.requestHeaders["My-Header"] = "My value"

    call.start(with: GRXWriteable { [weak self] response, error in
      DispatchQueue.main.async {
        if let response = response as? Data {
          NSLog("[SPMSample] Received response bytes: \(response.count)")
          self?.statusLabel.text = """
          gRPC SPM Sample
          Host: \(remoteHost)
          Method: \(method.httpPath!)
          Status: SUCCESS (Received \(response.count) bytes)
          """
        } else if let error = error {
          NSLog("[SPMSample] Finished with error: \(error)")
          self?.statusLabel.text = """
          gRPC SPM Sample
          Host: \(remoteHost)
          Method: \(method.httpPath!)
          Status: Finished (\(error.localizedDescription))
          """
        }
      }
    })
  }
}
