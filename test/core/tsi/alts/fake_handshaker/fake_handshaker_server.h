#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>

namespace grpc {
namespace gcp {

std::unique_ptr<grpc::Service> CreateFakeHandshakerService();

}  // namespace gcp
}  // namespace grpc
