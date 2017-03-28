#!/usr/bin/env ruby
# -*- coding: utf-8 -*-

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

# Error-throwing implementation of Route Guide service.
#
# Usage: $ path/to/route_guide_server.rb

this_dir = File.expand_path(File.dirname(__FILE__))
lib_dir = File.join(File.dirname(this_dir), 'lib')
$LOAD_PATH.unshift(lib_dir) unless $LOAD_PATH.include?(lib_dir)

require 'grpc'
require 'route_guide_services_pb'

include Routeguide

include GRPC::Core::StatusCodes

# CanellingandErrorReturningServiceImpl provides an implementation of the RouteGuide service.
class CancellingAndErrorReturningServerImpl < RouteGuide::Service
  # def get_feature
  #   Note get_feature isn't implemented in this subclass, so the server
  #   will get a gRPC UNIMPLEMENTED error when it's called.

  def list_features(rectangle, _call)
    raise "string appears on the client in the 'details' field of a 'GRPC::Unknown' exception"
  end

  def record_route(call)
    raise GRPC::BadStatus.new_status_exception(CANCELLED)
  end

  def route_chat(notes)
    raise GRPC::BadStatus.new_status_exception(ABORTED, details = 'arbitrary', metadata = {somekey: 'val'})
  end
end

def main
  port = '0.0.0.0:50051'
  s = GRPC::RpcServer.new
  s.add_http2_port(port, :this_port_is_insecure)
  GRPC.logger.info("... running insecurely on #{port}")
  s.handle(CancellingAndErrorReturningServerImpl.new)
  s.run_till_terminated
end

main
