#include "other_service.h"

// Logic and data behind the server's behavior.
Status OtherServiceImpl::SayHello(
  ServerContext* context,
  const HelloRequest* request,
  HelloReply* reply
) {
  std::string prefix("Other Hello ");
  reply->set_message(prefix + request->name());
  return Status::OK;
}
