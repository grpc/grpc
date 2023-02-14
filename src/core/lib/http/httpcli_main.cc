#include <condition_variable>
#include <memory>
#include <mutex>

#include "src/core/lib/http/httpcli.h"
#include "src/core/lib/http/parser.h"

struct Completion {
  bool completed = false;
  std::mutex mu;
  std::condition_variable cv;
};

void on_done(void* arg, absl::Status error) {
  GPR_ASSERT(error.ok());
  Completion* c = static_cast<Completion*>(arg);
  std::lock_guard<std::mutex> l(c->mu);
  c->completed = true;
  c->cv.notify_one();
}

int main() {
  grpc_http_request req;
  grpc_http_response response;
  // std::string host = absl::StrFormat("localhost:%d", g_server_port);
  std::string host = "www.google.com";
  gpr_log(GPR_INFO, "requesting from %s", host.c_str());
  memset(&req, 0, sizeof(req));
  auto uri = grpc_core::URI::Create("http", host, "/get", {} /* query params */,
                                    "" /* fragment */);
  GPR_ASSERT(uri.ok());
  Completion c;
  {
    std::unique_lock<std::mutex> l(c.mu);
    grpc_core::OrphanablePtr<grpc_core::HttpRequest> http_request =
        grpc_core::HttpRequest::Get(
            std::move(*uri), nullptr /* channel args */, pops(), &req,
            NSecondsTime(15), GRPC_CLOSURE_CREATE(&on_done, &c, 0), &response,
            grpc_core::RefCountedPtr<grpc_channel_credentials>(
                grpc_insecure_credentials_create()));
    http_request->Start();
    c.cv.wait(l, [&c] { return c.completed; });
  }
  gpr_log(GPR_INFO, "response body: %s", response.body);
}
