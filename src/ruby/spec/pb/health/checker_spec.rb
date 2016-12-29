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

require 'grpc'
require 'grpc/health/v1/health_pb'
require 'grpc/health/checker'
require 'open3'
require 'tmpdir'

def can_run_codegen_check
  system('which grpc_ruby_plugin') && system('which protoc')
end

describe 'Health protobuf code generation' do
  context 'the health service file used by grpc/health/checker' do
    if !can_run_codegen_check
      skip 'protoc || grpc_ruby_plugin missing, cannot verify health code-gen'
    else
      it 'should already be loaded indirectly i.e, used by the other specs' do
        expect(require('grpc/health/v1/health_services_pb')).to be(false)
      end

      it 'should have the same content as created by code generation' do
        root_dir = File.join(File.dirname(__FILE__), '..', '..', '..', '..')
        pb_dir = File.join(root_dir, 'proto')

        # Get the current content
        service_path = File.join(root_dir, 'ruby', 'pb', 'grpc',
                                 'health', 'v1', 'health_services_pb.rb')
        want = nil
        File.open(service_path) { |f| want = f.read }

        # Regenerate it
        plugin, = Open3.capture2('which', 'grpc_ruby_plugin')
        plugin = plugin.strip
        got = nil
        Dir.mktmpdir do |tmp_dir|
          gen_out = File.join(tmp_dir, 'grpc', 'health', 'v1',
                              'health_services_pb.rb')
          pid = spawn(
            'protoc',
            '-I.',
            'grpc/health/v1/health.proto',
            "--grpc_out=#{tmp_dir}",
            "--plugin=protoc-gen-grpc=#{plugin}",
            chdir: pb_dir)
          Process.wait(pid)
          File.open(gen_out) { |f| got = f.read }
        end
        expect(got).to eq(want)
      end
    end
  end
end

describe Grpc::Health::Checker do
  StatusCodes = GRPC::Core::StatusCodes
  ServingStatus = Grpc::Health::V1::HealthCheckResponse::ServingStatus
  HCResp = Grpc::Health::V1::HealthCheckResponse
  HCReq = Grpc::Health::V1::HealthCheckRequest
  success_tests =
    [
      {
        desc: 'the service is not specified',
        service: ''
      }, {
        desc: 'the service is specified',
        service: 'fake-service-1'
      }
    ]

  context 'initialization' do
    it 'can be constructed with no args' do
      checker = Grpc::Health::Checker.new
      expect(checker).to_not be(nil)
    end
  end

  context 'method `add_status` and `check`' do
    success_tests.each do |t|
      it "should succeed when #{t[:desc]}" do
        checker = Grpc::Health::Checker.new
        checker.add_status(t[:service], ServingStatus::NOT_SERVING)
        got = checker.check(HCReq.new(service: t[:service]), nil)
        want = HCResp.new(status: ServingStatus::NOT_SERVING)
        expect(got).to eq(want)
      end
    end
  end

  context 'method `check`' do
    success_tests.each do |t|
      it "should fail with NOT_FOUND when #{t[:desc]}" do
        checker = Grpc::Health::Checker.new
        blk = proc do
          checker.check(HCReq.new(service: t[:service]), nil)
        end
        expected_msg = /#{StatusCodes::NOT_FOUND}/
        expect(&blk).to raise_error GRPC::NotFound, expected_msg
      end
    end
  end

  context 'method `clear_status`' do
    success_tests.each do |t|
      it "should fail after clearing status when #{t[:desc]}" do
        checker = Grpc::Health::Checker.new
        checker.add_status(t[:service], ServingStatus::NOT_SERVING)
        got = checker.check(HCReq.new(service: t[:service]), nil)
        want = HCResp.new(status: ServingStatus::NOT_SERVING)
        expect(got).to eq(want)

        checker.clear_status(t[:service])
        blk = proc do
          checker.check(HCReq.new(service: t[:service]), nil)
        end
        expected_msg = /#{StatusCodes::NOT_FOUND}/
        expect(&blk).to raise_error GRPC::NotFound, expected_msg
      end
    end
  end

  context 'method `clear_all`' do
    it 'should return NOT_FOUND after being invoked' do
      checker = Grpc::Health::Checker.new
      success_tests.each do |t|
        checker.add_status(t[:service], ServingStatus::NOT_SERVING)
        got = checker.check(HCReq.new(service: t[:service]), nil)
        want = HCResp.new(status: ServingStatus::NOT_SERVING)
        expect(got).to eq(want)
      end

      checker.clear_all

      success_tests.each do |t|
        blk = proc do
          checker.check(HCReq.new(service: t[:service]), nil)
        end
        expected_msg = /#{StatusCodes::NOT_FOUND}/
        expect(&blk).to raise_error GRPC::NotFound, expected_msg
      end
    end
  end

  describe 'running on RpcServer' do
    RpcServer = GRPC::RpcServer
    CheckerStub = Grpc::Health::Checker.rpc_stub_class

    before(:each) do
      server_host = '0.0.0.0:0'
      @client_opts = { channel_override: @ch }
      server_opts = {
        poll_period: 1
      }
      @srv = RpcServer.new(**server_opts)
      server_port = @srv.add_http2_port(server_host, :this_port_is_insecure)
      @host = "localhost:#{server_port}"
      @ch = GRPC::Core::Channel.new(@host, nil, :this_channel_is_insecure)
    end

    after(:each) do
      @srv.stop
    end

    it 'should receive the correct status', server: true do
      Thread.abort_on_exception = true
      checker = Grpc::Health::Checker.new
      @srv.handle(checker)
      checker.add_status('', ServingStatus::NOT_SERVING)
      t = Thread.new { @srv.run }
      @srv.wait_till_running

      stub = CheckerStub.new(@host, :this_channel_is_insecure, **@client_opts)
      got = stub.check(HCReq.new)
      want = HCResp.new(status: ServingStatus::NOT_SERVING)
      expect(got).to eq(want)
      @srv.stop
      t.join
    end

    it 'should fail on unknown services', server: true do
      checker = Grpc::Health::Checker.new
      @srv.handle(checker)
      t = Thread.new { @srv.run }
      @srv.wait_till_running
      blk = proc do
        stub = CheckerStub.new(@host, :this_channel_is_insecure, **@client_opts)
        stub.check(HCReq.new(service: 'unknown'))
      end
      expected_msg = /#{StatusCodes::NOT_FOUND}/
      expect(&blk).to raise_error GRPC::NotFound, expected_msg
      @srv.stop
      t.join
    end
  end
end
