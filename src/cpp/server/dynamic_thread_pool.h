#include <grpcpp/ext/csm_observability.h>

int main() {
  // …
  auto observability = grpc::experimental::CsmObservabilityBuilder()
                           .SetMeterProvider(std::move(meter_provider))
                           .BuildAndRegister();
  assert(observability.ok());
  // …
}
