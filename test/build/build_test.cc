// NOLINT(namespace-envoy)
#include <cstdlib>
#include <iostream>

#include "google/protobuf/descriptor.h"

// Basic C++ build/link validation for the v2 xDS APIs.
int main(int argc, char* argv[]) {
  const auto methods = {
      "envoy.api.v2.ClusterDiscoveryService.FetchClusters",
      "envoy.api.v2.ClusterDiscoveryService.StreamClusters",
      "envoy.api.v2.EndpointDiscoveryService.FetchEndpoints",
      "envoy.api.v2.EndpointDiscoveryService.StreamEndpoints",
      "envoy.api.v2.ListenerDiscoveryService.FetchListeners",
      "envoy.api.v2.ListenerDiscoveryService.StreamListeners",
      "envoy.api.v2.RouteDiscoveryService.FetchRoutes",
      "envoy.api.v2.RouteDiscoveryService.StreamRoutes",
      "envoy.service.discovery.v2.AggregatedDiscoveryService.StreamAggregatedResources",
      "envoy.service.discovery.v2.HealthDiscoveryService.FetchHealthCheck",
      "envoy.service.discovery.v2.HealthDiscoveryService.StreamHealthCheck",
      "envoy.service.discovery.v2.RuntimeDiscoveryService.FetchRuntime",
      "envoy.service.discovery.v2.RuntimeDiscoveryService.StreamRuntime",
      "envoy.service.accesslog.v2.AccessLogService.StreamAccessLogs",
      "envoy.service.metrics.v2.MetricsService.StreamMetrics",
      "envoy.service.ratelimit.v2.RateLimitService.ShouldRateLimit",
      "udpa.service.orca.v1.OpenRcaService.StreamCoreMetrics",
  };

  for (const auto& method : methods) {
    if (google::protobuf::DescriptorPool::generated_pool()->FindMethodByName(method) == nullptr) {
      std::cout << "Unable to find method descriptor for " << method << std::endl;
      exit(EXIT_FAILURE);
    }
  }

  exit(EXIT_SUCCESS);
}
