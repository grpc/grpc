#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>

bool protobuf_test(const google::protobuf::MethodDescriptor *method) {
  return method->client_streaming() || method->server_streaming();
}

int main() {
  return 0;
}
