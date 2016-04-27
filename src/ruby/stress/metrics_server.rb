# Copyright 2016, Google Inc.
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

require_relative '../pb/grpc/testing/metrics.rb'
require_relative '../pb/grpc/testing/metrics_services.rb'

class Gauge
  def get_name
    raise NoMethodError.new
  end

  def get_type
    raise NoMethodError.new
  end

  def get_value
    raise NoMethodError.new
  end
end

class MetricsServiceImpl < Grpc::Testing::MetricsService::Service
  include Grpc::Testing
  @gauges

  def initialize
    @gauges = {}
  end

  def register_gauge(gauge)
    @gauges[gauge.get_name] = gauge
  end

  def make_gauge_response(gauge)
    response = GaugeResponse.new(:name => gauge.get_name)
    value = gauge.get_value
    case gauge.get_type
    when 'long'
      response.long_value = value
    when 'double'
      response.double_value = value
    when 'string'
      response.string_value = value
    end
    response
  end

  def get_all_gauges(_empty, _call)
    @gauges.values.map do |gauge|
      make_gauge_response gauge
    end
  end

  def get_gauge(gauge_req, _call)
    gauge = @gauges[gauge_req.name]
    make_gauge_response gauge
  end
end
