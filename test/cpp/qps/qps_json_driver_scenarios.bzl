"""Scenarios of qps driver."""

QPS_JSON_DRIVER_SCENARIOS = {"cpp_protobuf_sync_streaming_qps_unconstrained_insecure": '\'{\'scenarios\' : [{"name": "cpp_protobuf_sync_streaming_qps_unconstrained_insecure", "warmup_seconds": 0, "benchmark_seconds": 1, "num_servers": 1, "server_config": {"security_params": null, "server_type": "SYNC_SERVER", "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}, {"int_value": 1, "name": "grpc.minimal_stack"}], "async_server_threads": 0, "threads_per_cq": 3, "server_processes": 0}, "client_config": {"security_params": null, "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}, {"int_value": 1, "name": "grpc.minimal_stack"}], "async_client_threads": 0, "outstanding_rpcs_per_channel": 16, "rpc_type": "STREAMING", "load_params": {"closed_loop": {}}, "histogram_params": {"resolution": 0.01, "max_possible": 60000000000.0}, "client_type": "SYNC_CLIENT", "payload_config": {"simple_params": {"resp_size": 0, "req_size": 0}}, "client_channels": 64, "threads_per_cq": 3, "client_processes": 0}, "num_clients": 0}]}\'', "cpp_protobuf_sync_streaming_qps_unconstrained_1mps_insecure": '\'{\'scenarios\' : [{"name": "cpp_protobuf_sync_streaming_qps_unconstrained_1mps_insecure", "warmup_seconds": 0, "benchmark_seconds": 1, "num_servers": 1, "server_config": {"security_params": null, "server_type": "SYNC_SERVER", "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}, {"int_value": 1, "name": "grpc.minimal_stack"}], "async_server_threads": 0, "threads_per_cq": 0, "server_processes": 0}, "client_config": {"security_params": null, "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}, {"int_value": 1, "name": "grpc.minimal_stack"}], "async_client_threads": 0, "outstanding_rpcs_per_channel": 16, "rpc_type": "STREAMING", "load_params": {"closed_loop": {}}, "histogram_params": {"resolution": 0.01, "max_possible": 60000000000.0}, "client_type": "SYNC_CLIENT", "messages_per_stream": 1, "payload_config": {"simple_params": {"resp_size": 0, "req_size": 0}}, "client_channels": 64, "threads_per_cq": 0, "client_processes": 0}, "num_clients": 0}]}\'', "cpp_protobuf_async_unary_ping_pong_insecure_1MB": '\'{\'scenarios\' : [{"name": "cpp_protobuf_async_unary_ping_pong_insecure_1MB", "warmup_seconds": 0, "benchmark_seconds": 1, "num_servers": 1, "server_config": {"security_params": null, "server_type": "ASYNC_SERVER", "channel_args": [{"str_value": "latency", "name": "grpc.optimization_target"}, {"int_value": 1, "name": "grpc.minimal_stack"}], "async_server_threads": 0, "threads_per_cq": 0, "server_processes": 0}, "client_config": {"security_params": null, "channel_args": [{"str_value": "latency", "name": "grpc.optimization_target"}, {"int_value": 1, "name": "grpc.minimal_stack"}], "async_client_threads": 1, "outstanding_rpcs_per_channel": 1, "rpc_type": "UNARY", "load_params": {"closed_loop": {}}, "histogram_params": {"resolution": 0.01, "max_possible": 60000000000.0}, "client_type": "ASYNC_CLIENT", "payload_config": {"simple_params": {"resp_size": 1048576, "req_size": 1048576}}, "client_channels": 1, "threads_per_cq": 0, "client_processes": 0}, "num_clients": 1}]}\'', "cpp_protobuf_sync_streaming_qps_unconstrained_10mps_insecure": '\'{\'scenarios\' : [{"name": "cpp_protobuf_sync_streaming_qps_unconstrained_10mps_insecure", "warmup_seconds": 0, "benchmark_seconds": 1, "num_servers": 1, "server_config": {"security_params": null, "server_type": "SYNC_SERVER", "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}, {"int_value": 1, "name": "grpc.minimal_stack"}], "async_server_threads": 0, "threads_per_cq": 0, "server_processes": 0}, "client_config": {"security_params": null, "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}, {"int_value": 1, "name": "grpc.minimal_stack"}], "async_client_threads": 0, "outstanding_rpcs_per_channel": 16, "rpc_type": "STREAMING", "load_params": {"closed_loop": {}}, "histogram_params": {"resolution": 0.01, "max_possible": 60000000000.0}, "client_type": "SYNC_CLIENT", "messages_per_stream": 10, "payload_config": {"simple_params": {"resp_size": 0, "req_size": 0}}, "client_channels": 64, "threads_per_cq": 0, "client_processes": 0}, "num_clients": 0}]}\'', "cpp_protobuf_async_unary_qps_unconstrained_2waysharedcq_insecure": '\'{\'scenarios\' : [{"name": "cpp_protobuf_async_unary_qps_unconstrained_2waysharedcq_insecure", "warmup_seconds": 0, "benchmark_seconds": 1, "num_servers": 1, "server_config": {"security_params": null, "server_type": "ASYNC_SERVER", "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}], "async_server_threads": 0, "threads_per_cq": 2, "server_processes": 0}, "client_config": {"security_params": null, "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}], "async_client_threads": 0, "outstanding_rpcs_per_channel": 100, "rpc_type": "UNARY", "load_params": {"closed_loop": {}}, "histogram_params": {"resolution": 0.01, "max_possible": 60000000000.0}, "client_type": "ASYNC_CLIENT", "payload_config": {"simple_params": {"resp_size": 0, "req_size": 0}}, "client_channels": 64, "threads_per_cq": 2, "client_processes": 0}, "num_clients": 0}]}\'', "cpp_protobuf_async_unary_qps_unconstrained_1cq_insecure": '\'{\'scenarios\' : [{"name": "cpp_protobuf_async_unary_qps_unconstrained_1cq_insecure", "warmup_seconds": 0, "benchmark_seconds": 1, "num_servers": 1, "server_config": {"security_params": null, "server_type": "ASYNC_SERVER", "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}], "async_server_threads": 0, "threads_per_cq": 1000000, "server_processes": 0}, "client_config": {"security_params": null, "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}], "async_client_threads": 0, "outstanding_rpcs_per_channel": 13, "rpc_type": "UNARY", "load_params": {"closed_loop": {}}, "histogram_params": {"resolution": 0.01, "max_possible": 60000000000.0}, "client_type": "ASYNC_CLIENT", "payload_config": {"simple_params": {"resp_size": 0, "req_size": 0}}, "client_channels": 64, "threads_per_cq": 1000000, "client_processes": 0}, "num_clients": 0}]}\'', "cpp_protobuf_async_streaming_qps_unconstrained_insecure": '\'{\'scenarios\' : [{"name": "cpp_protobuf_async_streaming_qps_unconstrained_insecure", "warmup_seconds": 0, "benchmark_seconds": 1, "num_servers": 1, "server_config": {"security_params": null, "server_type": "ASYNC_SERVER", "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}, {"int_value": 1, "name": "grpc.minimal_stack"}], "async_server_threads": 0, "threads_per_cq": 3, "server_processes": 0}, "client_config": {"security_params": null, "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}, {"int_value": 1, "name": "grpc.minimal_stack"}], "async_client_threads": 0, "outstanding_rpcs_per_channel": 100, "rpc_type": "STREAMING", "load_params": {"closed_loop": {}}, "histogram_params": {"resolution": 0.01, "max_possible": 60000000000.0}, "client_type": "ASYNC_CLIENT", "payload_config": {"simple_params": {"resp_size": 0, "req_size": 0}}, "client_channels": 64, "threads_per_cq": 3, "client_processes": 0}, "num_clients": 0}]}\'', "cpp_protobuf_async_streaming_qps_unconstrained_2waysharedcq_insecure": '\'{\'scenarios\' : [{"name": "cpp_protobuf_async_streaming_qps_unconstrained_2waysharedcq_insecure", "warmup_seconds": 0, "benchmark_seconds": 1, "num_servers": 1, "server_config": {"security_params": null, "server_type": "ASYNC_SERVER", "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}], "async_server_threads": 0, "threads_per_cq": 2, "server_processes": 0}, "client_config": {"security_params": null, "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}], "async_client_threads": 0, "outstanding_rpcs_per_channel": 100, "rpc_type": "STREAMING", "load_params": {"closed_loop": {}}, "histogram_params": {"resolution": 0.01, "max_possible": 60000000000.0}, "client_type": "ASYNC_CLIENT", "payload_config": {"simple_params": {"resp_size": 0, "req_size": 0}}, "client_channels": 64, "threads_per_cq": 2, "client_processes": 0}, "num_clients": 0}]}\'', "cpp_generic_async_streaming_qps_unconstrained_1cq_insecure": '\'{\'scenarios\' : [{"name": "cpp_generic_async_streaming_qps_unconstrained_1cq_insecure", "warmup_seconds": 0, "benchmark_seconds": 1, "num_servers": 1, "server_config": {"async_server_threads": 0, "security_params": null, "server_type": "ASYNC_GENERIC_SERVER", "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}], "payload_config": {"bytebuf_params": {"resp_size": 0, "req_size": 0}}, "threads_per_cq": 1000000, "server_processes": 0}, "client_config": {"security_params": null, "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}], "async_client_threads": 0, "outstanding_rpcs_per_channel": 13, "rpc_type": "STREAMING", "load_params": {"closed_loop": {}}, "histogram_params": {"resolution": 0.01, "max_possible": 60000000000.0}, "client_type": "ASYNC_CLIENT", "payload_config": {"bytebuf_params": {"resp_size": 0, "req_size": 0}}, "client_channels": 64, "threads_per_cq": 1000000, "client_processes": 0}, "num_clients": 0}]}\'', "cpp_protobuf_async_streaming_qps_unconstrained_10mps_insecure": '\'{\'scenarios\' : [{"name": "cpp_protobuf_async_streaming_qps_unconstrained_10mps_insecure", "warmup_seconds": 0, "benchmark_seconds": 1, "num_servers": 1, "server_config": {"security_params": null, "server_type": "ASYNC_SERVER", "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}, {"int_value": 1, "name": "grpc.minimal_stack"}], "async_server_threads": 0, "threads_per_cq": 0, "server_processes": 0}, "client_config": {"security_params": null, "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}, {"int_value": 1, "name": "grpc.minimal_stack"}], "async_client_threads": 0, "outstanding_rpcs_per_channel": 100, "rpc_type": "STREAMING", "load_params": {"closed_loop": {}}, "histogram_params": {"resolution": 0.01, "max_possible": 60000000000.0}, "client_type": "ASYNC_CLIENT", "messages_per_stream": 10, "payload_config": {"simple_params": {"resp_size": 0, "req_size": 0}}, "client_channels": 64, "threads_per_cq": 0, "client_processes": 0}, "num_clients": 0}]}\'', "cpp_generic_async_streaming_qps_unconstrained_64KBmsg_insecure": '\'{\'scenarios\' : [{"name": "cpp_generic_async_streaming_qps_unconstrained_64KBmsg_insecure", "warmup_seconds": 0, "benchmark_seconds": 1, "num_servers": 1, "server_config": {"async_server_threads": 0, "security_params": null, "server_type": "ASYNC_GENERIC_SERVER", "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}, {"int_value": 1, "name": "grpc.minimal_stack"}], "payload_config": {"bytebuf_params": {"resp_size": 65536, "req_size": 65536}}, "threads_per_cq": 0, "server_processes": 0}, "client_config": {"security_params": null, "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}, {"int_value": 1, "name": "grpc.minimal_stack"}], "async_client_threads": 0, "outstanding_rpcs_per_channel": 100, "rpc_type": "STREAMING", "load_params": {"closed_loop": {}}, "histogram_params": {"resolution": 0.01, "max_possible": 60000000000.0}, "client_type": "ASYNC_CLIENT", "payload_config": {"bytebuf_params": {"resp_size": 65536, "req_size": 65536}}, "client_channels": 64, "threads_per_cq": 0, "client_processes": 0}, "num_clients": 0}]}\'', "cpp_generic_async_streaming_qps_unconstrained_insecure": '\'{\'scenarios\' : [{"name": "cpp_generic_async_streaming_qps_unconstrained_insecure", "warmup_seconds": 0, "benchmark_seconds": 1, "num_servers": 1, "server_config": {"async_server_threads": 0, "security_params": null, "server_type": "ASYNC_GENERIC_SERVER", "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}, {"int_value": 1, "name": "grpc.minimal_stack"}], "payload_config": {"bytebuf_params": {"resp_size": 0, "req_size": 0}}, "threads_per_cq": 0, "server_processes": 0}, "client_config": {"security_params": null, "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}, {"int_value": 1, "name": "grpc.minimal_stack"}], "async_client_threads": 0, "outstanding_rpcs_per_channel": 100, "rpc_type": "STREAMING", "load_params": {"closed_loop": {}}, "histogram_params": {"resolution": 0.01, "max_possible": 60000000000.0}, "client_type": "ASYNC_CLIENT", "payload_config": {"bytebuf_params": {"resp_size": 0, "req_size": 0}}, "client_channels": 64, "threads_per_cq": 0, "client_processes": 0}, "num_clients": 0}]}\'', "cpp_protobuf_async_streaming_qps_unconstrained_1cq_insecure": '\'{\'scenarios\' : [{"name": "cpp_protobuf_async_streaming_qps_unconstrained_1cq_insecure", "warmup_seconds": 0, "benchmark_seconds": 1, "num_servers": 1, "server_config": {"security_params": null, "server_type": "ASYNC_SERVER", "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}], "async_server_threads": 0, "threads_per_cq": 1000000, "server_processes": 0}, "client_config": {"security_params": null, "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}], "async_client_threads": 0, "outstanding_rpcs_per_channel": 13, "rpc_type": "STREAMING", "load_params": {"closed_loop": {}}, "histogram_params": {"resolution": 0.01, "max_possible": 60000000000.0}, "client_type": "ASYNC_CLIENT", "payload_config": {"simple_params": {"resp_size": 0, "req_size": 0}}, "client_channels": 64, "threads_per_cq": 1000000, "client_processes": 0}, "num_clients": 0}]}\'', "cpp_protobuf_async_streaming_from_server_qps_unconstrained_insecure": '\'{\'scenarios\' : [{"name": "cpp_protobuf_async_streaming_from_server_qps_unconstrained_insecure", "warmup_seconds": 0, "benchmark_seconds": 1, "num_servers": 1, "server_config": {"security_params": null, "server_type": "ASYNC_SERVER", "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}, {"int_value": 1, "name": "grpc.minimal_stack"}], "async_server_threads": 0, "threads_per_cq": 3, "server_processes": 0}, "client_config": {"security_params": null, "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}, {"int_value": 1, "name": "grpc.minimal_stack"}], "async_client_threads": 0, "outstanding_rpcs_per_channel": 100, "rpc_type": "STREAMING_FROM_SERVER", "load_params": {"closed_loop": {}}, "histogram_params": {"resolution": 0.01, "max_possible": 60000000000.0}, "client_type": "ASYNC_CLIENT", "payload_config": {"simple_params": {"resp_size": 0, "req_size": 0}}, "client_channels": 64, "threads_per_cq": 3, "client_processes": 0}, "num_clients": 0}]}\'', "cpp_protobuf_async_streaming_from_client_1channel_1MB": '\'{\'scenarios\' : [{"name": "cpp_protobuf_async_streaming_from_client_1channel_1MB", "warmup_seconds": 0, "benchmark_seconds": 1, "num_servers": 1, "server_config": {"security_params": null, "server_type": "ASYNC_SERVER", "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}], "async_server_threads": 0, "threads_per_cq": 0, "server_processes": 0}, "client_config": {"security_params": null, "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}], "async_client_threads": 0, "outstanding_rpcs_per_channel": 1, "rpc_type": "STREAMING_FROM_CLIENT", "load_params": {"closed_loop": {}}, "histogram_params": {"resolution": 0.01, "max_possible": 60000000000.0}, "client_type": "ASYNC_CLIENT", "payload_config": {"simple_params": {"resp_size": 1048576, "req_size": 1048576}}, "client_channels": 1, "threads_per_cq": 0, "client_processes": 0}, "num_clients": 1}]}\'', "cpp_protobuf_async_unary_1channel_100rpcs_1MB": '\'{\'scenarios\' : [{"name": "cpp_protobuf_async_unary_1channel_100rpcs_1MB", "warmup_seconds": 0, "benchmark_seconds": 1, "num_servers": 1, "server_config": {"security_params": null, "server_type": "ASYNC_SERVER", "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}], "async_server_threads": 0, "threads_per_cq": 0, "server_processes": 0}, "client_config": {"security_params": null, "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}], "async_client_threads": 0, "outstanding_rpcs_per_channel": 100, "rpc_type": "UNARY", "load_params": {"closed_loop": {}}, "histogram_params": {"resolution": 0.01, "max_possible": 60000000000.0}, "client_type": "ASYNC_CLIENT", "payload_config": {"simple_params": {"resp_size": 1048576, "req_size": 1048576}}, "client_channels": 1, "threads_per_cq": 0, "client_processes": 0}, "num_clients": 1}]}\'', "cpp_generic_async_streaming_qps_1channel_1MBmsg_insecure": '\'{\'scenarios\' : [{"name": "cpp_generic_async_streaming_qps_1channel_1MBmsg_insecure", "warmup_seconds": 0, "benchmark_seconds": 1, "num_servers": 1, "server_config": {"async_server_threads": 0, "security_params": null, "server_type": "ASYNC_GENERIC_SERVER", "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}, {"int_value": 1, "name": "grpc.minimal_stack"}], "payload_config": {"bytebuf_params": {"resp_size": 1048576, "req_size": 1048576}}, "threads_per_cq": 0, "server_processes": 0}, "client_config": {"security_params": null, "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}, {"int_value": 1, "name": "grpc.minimal_stack"}], "async_client_threads": 0, "outstanding_rpcs_per_channel": 100, "rpc_type": "STREAMING", "load_params": {"closed_loop": {}}, "histogram_params": {"resolution": 0.01, "max_possible": 60000000000.0}, "client_type": "ASYNC_CLIENT", "payload_config": {"bytebuf_params": {"resp_size": 1048576, "req_size": 1048576}}, "client_channels": 1, "threads_per_cq": 0, "client_processes": 0}, "num_clients": 0}]}\'', "cpp_generic_async_streaming_qps_unconstrained_1mps_insecure": '\'{\'scenarios\' : [{"name": "cpp_generic_async_streaming_qps_unconstrained_1mps_insecure", "warmup_seconds": 0, "benchmark_seconds": 1, "num_servers": 1, "server_config": {"async_server_threads": 0, "security_params": null, "server_type": "ASYNC_GENERIC_SERVER", "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}, {"int_value": 1, "name": "grpc.minimal_stack"}], "payload_config": {"bytebuf_params": {"resp_size": 0, "req_size": 0}}, "threads_per_cq": 0, "server_processes": 0}, "client_config": {"security_params": null, "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}, {"int_value": 1, "name": "grpc.minimal_stack"}], "async_client_threads": 0, "outstanding_rpcs_per_channel": 100, "rpc_type": "STREAMING", "load_params": {"closed_loop": {}}, "histogram_params": {"resolution": 0.01, "max_possible": 60000000000.0}, "client_type": "ASYNC_CLIENT", "messages_per_stream": 1, "payload_config": {"bytebuf_params": {"resp_size": 0, "req_size": 0}}, "client_channels": 64, "threads_per_cq": 0, "client_processes": 0}, "num_clients": 0}]}\'', "cpp_protobuf_async_client_sync_server_unary_qps_unconstrained_insecure": '\'{\'scenarios\' : [{"name": "cpp_protobuf_async_client_sync_server_unary_qps_unconstrained_insecure", "warmup_seconds": 0, "benchmark_seconds": 1, "num_servers": 1, "server_config": {"security_params": null, "server_type": "SYNC_SERVER", "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}, {"int_value": 1, "name": "grpc.minimal_stack"}], "async_server_threads": 0, "threads_per_cq": 0, "server_processes": 0}, "client_config": {"security_params": null, "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}, {"int_value": 1, "name": "grpc.minimal_stack"}], "async_client_threads": 0, "outstanding_rpcs_per_channel": 100, "rpc_type": "UNARY", "load_params": {"closed_loop": {}}, "histogram_params": {"resolution": 0.01, "max_possible": 60000000000.0}, "client_type": "ASYNC_CLIENT", "payload_config": {"simple_params": {"resp_size": 0, "req_size": 0}}, "client_channels": 64, "threads_per_cq": 0, "client_processes": 0}, "num_clients": 0}]}\'', "cpp_protobuf_async_client_unary_1channel_64wide_128Breq_8MBresp_insecure": '\'{\'scenarios\' : [{"name": "cpp_protobuf_async_client_unary_1channel_64wide_128Breq_8MBresp_insecure", "warmup_seconds": 0, "benchmark_seconds": 1, "num_servers": 1, "server_config": {"security_params": null, "server_type": "ASYNC_SERVER", "channel_args": [{"str_value": "latency", "name": "grpc.optimization_target"}, {"int_value": 1, "name": "grpc.minimal_stack"}], "async_server_threads": 0, "threads_per_cq": 0, "server_processes": 0}, "client_config": {"security_params": null, "channel_args": [{"str_value": "latency", "name": "grpc.optimization_target"}, {"int_value": 1, "name": "grpc.minimal_stack"}], "async_client_threads": 1, "outstanding_rpcs_per_channel": 1, "rpc_type": "UNARY", "load_params": {"closed_loop": {}}, "histogram_params": {"resolution": 0.01, "max_possible": 60000000000.0}, "client_type": "ASYNC_CLIENT", "payload_config": {"simple_params": {"resp_size": 8388608, "req_size": 128}}, "client_channels": 1, "threads_per_cq": 0, "client_processes": 0}, "num_clients": 1}]}\'', "cpp_protobuf_sync_streaming_from_client_qps_unconstrained_insecure": '\'{\'scenarios\' : [{"name": "cpp_protobuf_sync_streaming_from_client_qps_unconstrained_insecure", "warmup_seconds": 0, "benchmark_seconds": 1, "num_servers": 1, "server_config": {"security_params": null, "server_type": "SYNC_SERVER", "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}, {"int_value": 1, "name": "grpc.minimal_stack"}], "async_server_threads": 0, "threads_per_cq": 3, "server_processes": 0}, "client_config": {"security_params": null, "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}, {"int_value": 1, "name": "grpc.minimal_stack"}], "async_client_threads": 0, "outstanding_rpcs_per_channel": 16, "rpc_type": "STREAMING_FROM_CLIENT", "load_params": {"closed_loop": {}}, "histogram_params": {"resolution": 0.01, "max_possible": 60000000000.0}, "client_type": "SYNC_CLIENT", "payload_config": {"simple_params": {"resp_size": 0, "req_size": 0}}, "client_channels": 64, "threads_per_cq": 3, "client_processes": 0}, "num_clients": 0}]}\'', "cpp_protobuf_async_client_sync_server_streaming_qps_unconstrained_insecure": '\'{\'scenarios\' : [{"name": "cpp_protobuf_async_client_sync_server_streaming_qps_unconstrained_insecure", "warmup_seconds": 0, "benchmark_seconds": 1, "num_servers": 1, "server_config": {"security_params": null, "server_type": "SYNC_SERVER", "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}, {"int_value": 1, "name": "grpc.minimal_stack"}], "async_server_threads": 0, "threads_per_cq": 0, "server_processes": 0}, "client_config": {"security_params": null, "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}, {"int_value": 1, "name": "grpc.minimal_stack"}], "async_client_threads": 0, "outstanding_rpcs_per_channel": 100, "rpc_type": "STREAMING", "load_params": {"closed_loop": {}}, "histogram_params": {"resolution": 0.01, "max_possible": 60000000000.0}, "client_type": "ASYNC_CLIENT", "payload_config": {"simple_params": {"resp_size": 0, "req_size": 0}}, "client_channels": 64, "threads_per_cq": 0, "client_processes": 0}, "num_clients": 0}]}\'', "cpp_protobuf_async_streaming_from_client_qps_unconstrained_insecure": '\'{\'scenarios\' : [{"name": "cpp_protobuf_async_streaming_from_client_qps_unconstrained_insecure", "warmup_seconds": 0, "benchmark_seconds": 1, "num_servers": 1, "server_config": {"security_params": null, "server_type": "ASYNC_SERVER", "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}, {"int_value": 1, "name": "grpc.minimal_stack"}], "async_server_threads": 0, "threads_per_cq": 3, "server_processes": 0}, "client_config": {"security_params": null, "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}, {"int_value": 1, "name": "grpc.minimal_stack"}], "async_client_threads": 0, "outstanding_rpcs_per_channel": 100, "rpc_type": "STREAMING_FROM_CLIENT", "load_params": {"closed_loop": {}}, "histogram_params": {"resolution": 0.01, "max_possible": 60000000000.0}, "client_type": "ASYNC_CLIENT", "payload_config": {"simple_params": {"resp_size": 0, "req_size": 0}}, "client_channels": 64, "threads_per_cq": 3, "client_processes": 0}, "num_clients": 0}]}\'', "cpp_generic_async_streaming_qps_unconstrained_2waysharedcq_insecure": '\'{\'scenarios\' : [{"name": "cpp_generic_async_streaming_qps_unconstrained_2waysharedcq_insecure", "warmup_seconds": 0, "benchmark_seconds": 1, "num_servers": 1, "server_config": {"async_server_threads": 0, "security_params": null, "server_type": "ASYNC_GENERIC_SERVER", "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}], "payload_config": {"bytebuf_params": {"resp_size": 0, "req_size": 0}}, "threads_per_cq": 2, "server_processes": 0}, "client_config": {"security_params": null, "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}], "async_client_threads": 0, "outstanding_rpcs_per_channel": 100, "rpc_type": "STREAMING", "load_params": {"closed_loop": {}}, "histogram_params": {"resolution": 0.01, "max_possible": 60000000000.0}, "client_type": "ASYNC_CLIENT", "payload_config": {"bytebuf_params": {"resp_size": 0, "req_size": 0}}, "client_channels": 64, "threads_per_cq": 2, "client_processes": 0}, "num_clients": 0}]}\'', "cpp_protobuf_sync_unary_qps_unconstrained_insecure": '\'{\'scenarios\' : [{"name": "cpp_protobuf_sync_unary_qps_unconstrained_insecure", "warmup_seconds": 0, "benchmark_seconds": 1, "num_servers": 1, "server_config": {"security_params": null, "server_type": "SYNC_SERVER", "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}, {"int_value": 1, "name": "grpc.minimal_stack"}], "async_server_threads": 0, "threads_per_cq": 3, "server_processes": 0}, "client_config": {"security_params": null, "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}, {"int_value": 1, "name": "grpc.minimal_stack"}], "async_client_threads": 0, "outstanding_rpcs_per_channel": 16, "rpc_type": "UNARY", "load_params": {"closed_loop": {}}, "histogram_params": {"resolution": 0.01, "max_possible": 60000000000.0}, "client_type": "SYNC_CLIENT", "payload_config": {"simple_params": {"resp_size": 0, "req_size": 0}}, "client_channels": 64, "threads_per_cq": 3, "client_processes": 0}, "num_clients": 0}]}\'', "cpp_protobuf_sync_streaming_from_server_qps_unconstrained_insecure": '\'{\'scenarios\' : [{"name": "cpp_protobuf_sync_streaming_from_server_qps_unconstrained_insecure", "warmup_seconds": 0, "benchmark_seconds": 1, "num_servers": 1, "server_config": {"security_params": null, "server_type": "SYNC_SERVER", "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}, {"int_value": 1, "name": "grpc.minimal_stack"}], "async_server_threads": 0, "threads_per_cq": 3, "server_processes": 0}, "client_config": {"security_params": null, "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}, {"int_value": 1, "name": "grpc.minimal_stack"}], "async_client_threads": 0, "outstanding_rpcs_per_channel": 16, "rpc_type": "STREAMING_FROM_SERVER", "load_params": {"closed_loop": {}}, "histogram_params": {"resolution": 0.01, "max_possible": 60000000000.0}, "client_type": "SYNC_CLIENT", "payload_config": {"simple_params": {"resp_size": 0, "req_size": 0}}, "client_channels": 64, "threads_per_cq": 3, "client_processes": 0}, "num_clients": 0}]}\'', "cpp_protobuf_async_unary_qps_unconstrained_insecure": '\'{\'scenarios\' : [{"name": "cpp_protobuf_async_unary_qps_unconstrained_insecure", "warmup_seconds": 0, "benchmark_seconds": 1, "num_servers": 1, "server_config": {"security_params": null, "server_type": "ASYNC_SERVER", "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}, {"int_value": 1, "name": "grpc.minimal_stack"}], "async_server_threads": 0, "threads_per_cq": 3, "server_processes": 0}, "client_config": {"security_params": null, "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}, {"int_value": 1, "name": "grpc.minimal_stack"}], "async_client_threads": 0, "outstanding_rpcs_per_channel": 100, "rpc_type": "UNARY", "load_params": {"closed_loop": {}}, "histogram_params": {"resolution": 0.01, "max_possible": 60000000000.0}, "client_type": "ASYNC_CLIENT", "payload_config": {"simple_params": {"resp_size": 0, "req_size": 0}}, "client_channels": 64, "threads_per_cq": 3, "client_processes": 0}, "num_clients": 0}]}\'', "cpp_generic_async_streaming_qps_unconstrained_10mps_insecure": '\'{\'scenarios\' : [{"name": "cpp_generic_async_streaming_qps_unconstrained_10mps_insecure", "warmup_seconds": 0, "benchmark_seconds": 1, "num_servers": 1, "server_config": {"async_server_threads": 0, "security_params": null, "server_type": "ASYNC_GENERIC_SERVER", "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}, {"int_value": 1, "name": "grpc.minimal_stack"}], "payload_config": {"bytebuf_params": {"resp_size": 0, "req_size": 0}}, "threads_per_cq": 0, "server_processes": 0}, "client_config": {"security_params": null, "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}, {"int_value": 1, "name": "grpc.minimal_stack"}], "async_client_threads": 0, "outstanding_rpcs_per_channel": 100, "rpc_type": "STREAMING", "load_params": {"closed_loop": {}}, "histogram_params": {"resolution": 0.01, "max_possible": 60000000000.0}, "client_type": "ASYNC_CLIENT", "messages_per_stream": 10, "payload_config": {"bytebuf_params": {"resp_size": 0, "req_size": 0}}, "client_channels": 64, "threads_per_cq": 0, "client_processes": 0}, "num_clients": 0}]}\'', "cpp_generic_async_streaming_ping_pong_insecure": '\'{\'scenarios\' : [{"name": "cpp_generic_async_streaming_ping_pong_insecure", "warmup_seconds": 0, "benchmark_seconds": 1, "num_servers": 1, "server_config": {"async_server_threads": 1, "security_params": null, "server_type": "ASYNC_GENERIC_SERVER", "channel_args": [{"str_value": "latency", "name": "grpc.optimization_target"}], "payload_config": {"bytebuf_params": {"resp_size": 0, "req_size": 0}}, "threads_per_cq": 0, "server_processes": 0}, "client_config": {"security_params": null, "channel_args": [{"str_value": "latency", "name": "grpc.optimization_target"}], "async_client_threads": 1, "outstanding_rpcs_per_channel": 1, "rpc_type": "STREAMING", "load_params": {"closed_loop": {}}, "histogram_params": {"resolution": 0.01, "max_possible": 60000000000.0}, "client_type": "ASYNC_CLIENT", "payload_config": {"bytebuf_params": {"resp_size": 0, "req_size": 0}}, "client_channels": 1, "threads_per_cq": 0, "client_processes": 0}, "num_clients": 1}]}\'', "cpp_protobuf_async_streaming_qps_unconstrained_1mps_insecure": '\'{\'scenarios\' : [{"name": "cpp_protobuf_async_streaming_qps_unconstrained_1mps_insecure", "warmup_seconds": 0, "benchmark_seconds": 1, "num_servers": 1, "server_config": {"security_params": null, "server_type": "ASYNC_SERVER", "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}, {"int_value": 1, "name": "grpc.minimal_stack"}], "async_server_threads": 0, "threads_per_cq": 0, "server_processes": 0}, "client_config": {"security_params": null, "channel_args": [{"str_value": "throughput", "name": "grpc.optimization_target"}, {"int_value": 1, "name": "grpc.minimal_stack"}], "async_client_threads": 0, "outstanding_rpcs_per_channel": 100, "rpc_type": "STREAMING", "load_params": {"closed_loop": {}}, "histogram_params": {"resolution": 0.01, "max_possible": 60000000000.0}, "client_type": "ASYNC_CLIENT", "messages_per_stream": 1, "payload_config": {"simple_params": {"resp_size": 0, "req_size": 0}}, "client_channels": 64, "threads_per_cq": 0, "client_processes": 0}, "num_clients": 0}]}\''}
