# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: src/proto/grpc/testing/control.proto

require 'google/protobuf'

require 'src/proto/grpc/testing/payloads_pb'
require 'src/proto/grpc/testing/stats_pb'
Google::Protobuf::DescriptorPool.generated_pool.build do
  add_message "grpc.testing.PoissonParams" do
    optional :offered_load, :double, 1
  end
  add_message "grpc.testing.ClosedLoopParams" do
  end
  add_message "grpc.testing.LoadParams" do
    oneof :load do
      optional :closed_loop, :message, 1, "grpc.testing.ClosedLoopParams"
      optional :poisson, :message, 2, "grpc.testing.PoissonParams"
    end
  end
  add_message "grpc.testing.SecurityParams" do
    optional :use_test_ca, :bool, 1
    optional :server_host_override, :string, 2
  end
  add_message "grpc.testing.ClientConfig" do
    repeated :server_targets, :string, 1
    optional :client_type, :enum, 2, "grpc.testing.ClientType"
    optional :security_params, :message, 3, "grpc.testing.SecurityParams"
    optional :outstanding_rpcs_per_channel, :int32, 4
    optional :client_channels, :int32, 5
    optional :async_client_threads, :int32, 7
    optional :rpc_type, :enum, 8, "grpc.testing.RpcType"
    optional :load_params, :message, 10, "grpc.testing.LoadParams"
    optional :payload_config, :message, 11, "grpc.testing.PayloadConfig"
    optional :histogram_params, :message, 12, "grpc.testing.HistogramParams"
    repeated :core_list, :int32, 13
    optional :core_limit, :int32, 14
    optional :other_client_api, :string, 15
  end
  add_message "grpc.testing.ClientStatus" do
    optional :stats, :message, 1, "grpc.testing.ClientStats"
  end
  add_message "grpc.testing.Mark" do
    optional :reset, :bool, 1
  end
  add_message "grpc.testing.ClientArgs" do
    oneof :argtype do
      optional :setup, :message, 1, "grpc.testing.ClientConfig"
      optional :mark, :message, 2, "grpc.testing.Mark"
    end
  end
  add_message "grpc.testing.ServerConfig" do
    optional :server_type, :enum, 1, "grpc.testing.ServerType"
    optional :security_params, :message, 2, "grpc.testing.SecurityParams"
    optional :port, :int32, 4
    optional :async_server_threads, :int32, 7
    optional :core_limit, :int32, 8
    optional :payload_config, :message, 9, "grpc.testing.PayloadConfig"
    repeated :core_list, :int32, 10
    optional :other_server_api, :string, 11
  end
  add_message "grpc.testing.ServerArgs" do
    oneof :argtype do
      optional :setup, :message, 1, "grpc.testing.ServerConfig"
      optional :mark, :message, 2, "grpc.testing.Mark"
    end
  end
  add_message "grpc.testing.ServerStatus" do
    optional :stats, :message, 1, "grpc.testing.ServerStats"
    optional :port, :int32, 2
    optional :cores, :int32, 3
  end
  add_message "grpc.testing.CoreRequest" do
  end
  add_message "grpc.testing.CoreResponse" do
    optional :cores, :int32, 1
  end
  add_message "grpc.testing.Void" do
  end
  add_message "grpc.testing.Scenario" do
    optional :name, :string, 1
    optional :client_config, :message, 2, "grpc.testing.ClientConfig"
    optional :num_clients, :int32, 3
    optional :server_config, :message, 4, "grpc.testing.ServerConfig"
    optional :num_servers, :int32, 5
    optional :warmup_seconds, :int32, 6
    optional :benchmark_seconds, :int32, 7
    optional :spawn_local_worker_count, :int32, 8
  end
  add_message "grpc.testing.Scenarios" do
    repeated :scenarios, :message, 1, "grpc.testing.Scenario"
  end
  add_message "grpc.testing.ScenarioResultSummary" do
    optional :qps, :double, 1
    optional :qps_per_server_core, :double, 2
    optional :server_system_time, :double, 3
    optional :server_user_time, :double, 4
    optional :client_system_time, :double, 5
    optional :client_user_time, :double, 6
    optional :latency_50, :double, 7
    optional :latency_90, :double, 8
    optional :latency_95, :double, 9
    optional :latency_99, :double, 10
    optional :latency_999, :double, 11
  end
  add_message "grpc.testing.ScenarioResult" do
    optional :scenario, :message, 1, "grpc.testing.Scenario"
    optional :latencies, :message, 2, "grpc.testing.HistogramData"
    repeated :client_stats, :message, 3, "grpc.testing.ClientStats"
    repeated :server_stats, :message, 4, "grpc.testing.ServerStats"
    repeated :server_cores, :int32, 5
    optional :summary, :message, 6, "grpc.testing.ScenarioResultSummary"
    repeated :client_success, :bool, 7
    repeated :server_success, :bool, 8
  end
  add_enum "grpc.testing.ClientType" do
    value :SYNC_CLIENT, 0
    value :ASYNC_CLIENT, 1
    value :OTHER_CLIENT, 2
  end
  add_enum "grpc.testing.ServerType" do
    value :SYNC_SERVER, 0
    value :ASYNC_SERVER, 1
    value :ASYNC_GENERIC_SERVER, 2
    value :OTHER_SERVER, 3
  end
  add_enum "grpc.testing.RpcType" do
    value :UNARY, 0
    value :STREAMING, 1
  end
end

module Grpc
  module Testing
    PoissonParams = Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.PoissonParams").msgclass
    ClosedLoopParams = Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.ClosedLoopParams").msgclass
    LoadParams = Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.LoadParams").msgclass
    SecurityParams = Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.SecurityParams").msgclass
    ClientConfig = Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.ClientConfig").msgclass
    ClientStatus = Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.ClientStatus").msgclass
    Mark = Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.Mark").msgclass
    ClientArgs = Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.ClientArgs").msgclass
    ServerConfig = Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.ServerConfig").msgclass
    ServerArgs = Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.ServerArgs").msgclass
    ServerStatus = Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.ServerStatus").msgclass
    CoreRequest = Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.CoreRequest").msgclass
    CoreResponse = Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.CoreResponse").msgclass
    Void = Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.Void").msgclass
    Scenario = Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.Scenario").msgclass
    Scenarios = Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.Scenarios").msgclass
    ScenarioResultSummary = Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.ScenarioResultSummary").msgclass
    ScenarioResult = Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.ScenarioResult").msgclass
    ClientType = Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.ClientType").enummodule
    ServerType = Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.ServerType").enummodule
    RpcType = Google::Protobuf::DescriptorPool.generated_pool.lookup("grpc.testing.RpcType").enummodule
  end
end
