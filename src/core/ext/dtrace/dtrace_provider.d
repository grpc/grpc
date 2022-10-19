/*
 * DTrace provider for gRPC probes.
 */

struct {
	const char *key;
	const char *value;
} grpc_transport_metadata_t;

struct {
  void *opaque;
  const char *transport;
  const char *load_address;
  const char *peer_address;
} grpc_transport_stream_t;

provider grpc {
	probe transport_stream_created(grpc_transport_stream_t *stream);

	probe transport_stream_destroyed(grpc_transport_stream_t *stream);

	probe transport_send_initial_metadata(grpc_transport_stream_t *stream, const grpc_transport_metadata_t *metadata, uint32_t size);

	probe transport_send_message(grpc_transport_stream_t *stream, void *data, uintptr_t size);

	probe transport_send_trailing_metadata(grpc_transport_stream_t *stream, const grpc_transport_metadata_t *metadata, uintptr_t size);

	probe transport_recv_initial_metadata(grpc_transport_stream_t *stream, const grpc_transport_metadata_t *metadata, uintptr_t size);

	probe transport_recv_message(grpc_transport_stream_t *stream, void *data, uintptr_t size);

	probe transport_recv_trailing_metadata(grpc_transport_stream_t *stream, const grpc_transport_metadata_t *metadata, uintptr_t size);

	probe transport_cancel_stream(grpc_transport_stream_t *stream, int status, string message);
};
