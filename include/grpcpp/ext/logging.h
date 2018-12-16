#pragma once

namespace grpc {
namespace logging {

/**
  register logging plugin and probe 'grpc.service' and 'grpc.method' to 
  server_context.client_metadata()
  */
void probe_logging_field_to_clientmeta();

}
}
