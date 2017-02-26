# Generated by the protocol buffer compiler.  DO NOT EDIT!
# Source: src/proto/grpc/testing/metrics.proto for package 'grpc.testing'
# Original file comments:
# Copyright 2015-2016, Google Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# Contains the definitions for a metrics service and the type of metrics
# exposed by the service.
#
# Currently, 'Gauge' (i.e a metric that represents the measured value of
# something at an instant of time) is the only metric type supported by the
# service.

require 'grpc'
require 'src/proto/grpc/testing/metrics_pb'

module Grpc
  module Testing
    module MetricsService
      class Service

        include GRPC::GenericService

        self.marshal_class_method = :encode
        self.unmarshal_class_method = :decode
        self.service_name = 'grpc.testing.MetricsService'

        # Returns the values of all the gauges that are currently being maintained by
        # the service
        rpc :GetAllGauges, EmptyMessage, stream(GaugeResponse)
        # Returns the value of one gauge
        rpc :GetGauge, GaugeRequest, GaugeResponse
      end

      Stub = Service.rpc_stub_class
    end
  end
end
