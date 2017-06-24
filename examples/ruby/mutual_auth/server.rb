#!/usr/bin/env ruby

# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# Sample gRPC server that implements the Greeter::Helloworld service and
# enforces mutual authentication.
#
# Usage: $ path/to/server.rb

this_dir = File.expand_path(File.dirname(__FILE__))
lib_dir = File.join(File.dirname(this_dir), 'lib')
$LOAD_PATH.unshift(lib_dir) unless $LOAD_PATH.include?(lib_dir)

require 'grpc'
require 'openssl'

require 'helloworld_services_pb'

# Additional peer certificate validation that checks for a specific CN in the
# subject name.
def check_peer_cert(call)
  valid_cert = false
  certificate = OpenSSL::X509::Certificate.new call.peer_cert
  certificate.subject().to_a().each do |name_entry|
    if (name_entry[0] == "CN") && (name_entry[1] == "testclient")
      valid_cert = true
    end
  end
  unless valid_cert
    fail GRPC::BadStatus.new(GRPC::Core::StatusCodes::UNAUTHENTICATED,
                             "Client cert has invalid CN")
  end
end


# GreeterServer is simple server that implements the Helloworld Greeter server.
class GreeterServer < Helloworld::Greeter::Service
  # say_hello implements the SayHello rpc method.
  def say_hello(hello_req, call)
    check_peer_cert(call)
    Helloworld::HelloReply.new(message: "Hello #{hello_req.name}")
  end
end

# loads the certificates for the test server.
# Returns [trusted root certs for client auth, server private key, server cert]
def load_test_certs
  this_dir = File.expand_path(File.dirname(__FILE__))
  client_cert_dir = File.join(this_dir, 'client_certs')
  server_cert_dir = File.join(this_dir, 'server_certs')
  files = [File.join(client_cert_dir, 'ca.pem'),
           File.join(server_cert_dir, 'server1.key'),
           File.join(server_cert_dir, 'server1.pem')]
  files.map { |f| File.open(f).read }
end

# creates ServerCredentials from the test certificates.
def test_server_creds
  certs = load_test_certs
  # Pass the set of trusted roots for client certification and the server
  # certificate with its private key. Force client authentication by passing
  # true as the final argument.
  GRPC::Core::ServerCredentials.new(
      certs[0], [{private_key: certs[1], cert_chain: certs[2]}], true)
end

# main starts an RpcServer that receives requests to GreeterServer at the sample
# server port.
def main
  s = GRPC::RpcServer.new
  s.add_http2_port('0.0.0.0:50051', test_server_creds)
  s.handle(GreeterServer)
  s.run_till_terminated
end

main
