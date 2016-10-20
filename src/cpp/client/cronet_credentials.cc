#include <grpc++/security/credentials.h>

#include <grpc++/channel.h>
#include <grpc++/support/channel_arguments.h>
#include <grpc/grpc_cronet.h>
#include "src/cpp/client/create_channel_internal.h"

namespace grpc {

class CronetChannelCredentialsImpl GRPC_FINAL : public ChannelCredentials {
 public:
  CronetChannelCredentialsImpl(void* engine) : engine_(engine) {}

  std::shared_ptr<grpc::Channel> CreateChannel(
      const string& target, const grpc::ChannelArguments& args) GRPC_OVERRIDE {
    grpc_channel_args channel_args;
    args.SetChannelArgs(&channel_args);
    return CreateChannelInternal(
        "", grpc_cronet_secure_channel_create(engine_, target.c_str(),
                                              &channel_args, nullptr));
  }

  SecureChannelCredentials* AsSecureCredentials() GRPC_OVERRIDE {
    return nullptr;
  }

 private:
  void* engine_;
};

std::shared_ptr<ChannelCredentials> CronetChannelCredentials(void* engine) {
  return std::shared_ptr<ChannelCredentials>(
      new CronetChannelCredentialsImpl(engine));
}

}  // namespace grpc
