#!/usr/bin/env ruby

# Copyright 2021 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Sample app that connects to a Greeter service.
#
# Usage: $ path/to/retry_client.rb

this_dir = File.expand_path(File.dirname(__FILE__))
lib_dir = File.join(File.dirname(this_dir), 'lib')
$LOAD_PATH.unshift(lib_dir) unless $LOAD_PATH.include?(lib_dir)

require 'grpc'
require 'json'

require 'helloworld_services_pb'

def main
  user = ARGV.size > 0 ?  ARGV[0] : 'world'
  hostname = ARGV.size > 1 ?  ARGV[1] : 'localhost:50051'
  stub = Helloworld::Greeter::Stub.new(
    hostname,
    :this_channel_is_insecure,
    {
      channel_args: {
        'grpc.enable_retries' => 1,
        'grpc.service_config' => JSON.dump(
          methodConfig: [
            {
              # To apply retry to all methods, put [{}] in the "name" field
              name: [
                {
                  service: 'helloworld.Greeter',
                  method: 'SayHello'
                }
              ],
              retryPolicy: {
                maxAttempts: 5,
                initialBackoff: '0.1s',
                maxBackoff: '1s',
                backoffMultiplier: 2,
                retryableStatusCodes: ['UNAVAILABLE']
              }
            }
          ]
        )
      }
    }
  )

  begin
    message = stub.say_hello(Helloworld::HelloRequest.new(name: user)).message
    p "Greeting: #{message}"
  rescue GRPC::BadStatus => e
    abort "ERROR: #{e.message}"
  end
end

main
