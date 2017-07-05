# Copyright 2016 gRPC authors.
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

require_relative '../pb/grpc/testing/metrics_pb.rb'
require_relative '../pb/grpc/testing/metrics_services_pb.rb'

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
