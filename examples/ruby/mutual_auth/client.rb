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

# Sample app that connects to a Greeter service with mutual client/server
# authentication.
#
# Usage: $ path/to/client.rb

this_dir = File.expand_path(File.dirname(__FILE__))
lib_dir = File.join(File.dirname(this_dir), 'lib')
$LOAD_PATH.unshift(lib_dir) unless $LOAD_PATH.include?(lib_dir)

require 'grpc'
require 'optparse'

require 'helloworld_services_pb'

# Loads the certificates used to access the test server securely.
def load_test_certs
  this_dir = File.expand_path(File.dirname(__FILE__))
  cert_dir = File.join(this_dir, 'client_certs')
  files = ['trusted_server_roots.pem', 'client.key', 'client.crt']
  files.map { |f| File.open(File.join(cert_dir, f)).read }
end

def test_client_creds
  certs = load_test_certs
  GRPC::Core::ChannelCredentials.new(certs[0], certs[1], certs[2])
end

def main
  host_override = 'foo.test.google.fr'
  OptionParser.new do |opts|
    opts.on('--server_host_override HOST_OVERRIDE',
	    'override the SSL target host name.') do |v|
      host_override = v
    end
  end.parse!

  # NEVER override the target hostname in a production environment. In
  # production, the FQDN of the server and the name in the certificate have to
  # match.
  stub_opts = {
    channel_args: {
      GRPC::Core::Channel::SSL_TARGET => host_override
    }
  }
  stub = Helloworld::Greeter::Stub.new('localhost:50051', test_client_creds,
                                       **stub_opts)
  user = ARGV.first || 'world'
  message = stub.say_hello(Helloworld::HelloRequest.new(name: user)).message
  p "Greeting: #{message}"
end

main
