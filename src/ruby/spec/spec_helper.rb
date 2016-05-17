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

spec_dir = File.expand_path(File.dirname(__FILE__))
root_dir = File.expand_path(File.join(spec_dir, '..'))
lib_dir = File.expand_path(File.join(root_dir, 'lib'))

$LOAD_PATH.unshift(spec_dir)
$LOAD_PATH.unshift(lib_dir)
$LOAD_PATH.uniq!

# set up coverage
require 'simplecov'
SimpleCov.start do
  add_filter 'spec'
  add_filter 'bin'
  SimpleCov.command_name ENV['COVERAGE_NAME']
end if ENV['COVERAGE_NAME']

require 'rspec'
require 'logging'
require 'rspec/logging_helper'

# GRPC is the general RPC module
#
# Configure its logging for fine-grained log control during test runs
module GRPC
  extend Logging.globally
end
Logging.logger.root.appenders = Logging.appenders.stdout
Logging.logger.root.level = :info
Logging.logger['GRPC'].level = :info
Logging.logger['GRPC::ActiveCall'].level = :info
Logging.logger['GRPC::BidiCall'].level = :info

# Configure RSpec to capture log messages for each test. The output from the
# logs will be stored in the @log_output variable. It is a StringIO instance.
RSpec.configure do |config|
  include RSpec::LoggingHelper
  config.capture_log_messages  # comment this out to see logs during test runs
end

RSpec::Expectations.configuration.warn_about_potential_false_positives = false
