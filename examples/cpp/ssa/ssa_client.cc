/*
 *
 * Copyright 2021 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpcpp/grpcpp.h>

#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"

#ifdef BAZEL_BUILD
#include "examples/protos/helloworld.grpc.pb.h"
#else
#include "helloworld.grpc.pb.h"
#endif

ABSL_FLAG(std::string, target, "xds:///helloworld:50051", "Target string");
ABSL_FLAG(bool, secure, true, "Secure mode");
ABSL_FLAG(std::string, ssa_cookie, "grpc_session_cookie",
          "gRPC session cookie name. Must match the cookie name from the xDS "
          "configuration.");

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

struct Cookie {
  std::string value;
  std::optional<int> max_age_s;
};

grpc::string_ref TrimWhitespace(grpc::string_ref string) {
  for (; string.length() > 0 && string.starts_with(" ");
       string = string.substr(1)) {
  }
  return string;
}

std::optional<int> GetMaxAgeAttributeValue(grpc::string_ref cookie_attributes) {
  while (!cookie_attributes.empty()) {
    size_t attr_name_end = cookie_attributes.find("=");
    if (attr_name_end == grpc::string_ref::npos) {
      return std::nullopt;
    }
    size_t end = cookie_attributes.find(";");
    if (cookie_attributes.substr(0, attr_name_end) == "Max-Age") {
      grpc::string_ref attribute_value =
          end == grpc::string_ref::npos
              ? cookie_attributes.substr(attr_name_end + 1)
              : cookie_attributes.substr(attr_name_end + 1, end);
      std::stringstream ss(
          std::string(attribute_value.data(), attribute_value.length()));
      int max_age_sec;
      ss >> max_age_sec;
      if (ss.fail()) {
        std::cerr << attribute_value << " is not a valid integer\n";
      } else {
        return max_age_sec;
      }
    }
    cookie_attributes = end == grpc::string_ref::npos
                            ? ""
                            : TrimWhitespace(cookie_attributes.substr(end + 1));
  }
  return std::nullopt;
}

// Extract the value of cookie with the provided name from the initial
// metadata map.
std::optional<Cookie> GetCookieValue(
    grpc::string_ref cookie_name,
    const std::multimap<grpc::string_ref, grpc::string_ref>& initial_metadata) {
  for (auto [start, end] = initial_metadata.equal_range("set-cookie");
       start != end; ++start) {
    auto name_value_split = start->second.find("=");
    // Malformed cookie
    if (name_value_split == grpc::string_ref::npos) {
      continue;
    }
    auto name = start->second.substr(0, name_value_split);
    // Not a session cookie
    if (name != cookie_name) {
      continue;
    }
    auto cookie = start->second.substr(name_value_split + 1);
    auto value_end = cookie.find(";");
    if (value_end == grpc::string_ref::npos) {
      // No attributes, entire string is the value
      return Cookie{std::string(cookie.data(), cookie.length()), std::nullopt};
    }
    auto value = cookie.substr(0, value_end);
    std::optional<int> max_age =
        GetMaxAgeAttributeValue(TrimWhitespace(cookie.substr(value_end + 1)));
    return Cookie{std::string(value.data(), value.length()), max_age};
  }
  return std::nullopt;
}

// Assembles and sends the client's payload including the optional cookie.
std::pair<std::string, Cookie> SayHelloAndGetCookie(
    const std::unique_ptr<Greeter::Stub>& stub, const std::string& user,
    grpc::string_ref cookie_name, std::optional<Cookie> current_cookie) {
  HelloRequest request;
  request.set_name(user);
  HelloReply reply;
  ClientContext context;
  // Set cookie header if the cookie value was provided
  if (current_cookie.has_value()) {
    context.AddMetadata(std::string("cookie"),
                        std::string(cookie_name.data(), cookie_name.length()) +
                            "=" + current_cookie->value);
  }
  std::mutex mu;
  std::condition_variable cv;
  bool done = false;
  Status status;
  stub->async()->SayHello(&context, &request, &reply,
                          [&mu, &cv, &done, &status](Status s) {
                            status = std::move(s);
                            std::lock_guard<std::mutex> lock(mu);
                            done = true;
                            cv.notify_one();
                          });

  std::unique_lock<std::mutex> lock(mu);
  while (!done) {
    cv.wait(lock);
  }
  if (status.ok()) {
    return {reply.message(),
            GetCookieValue(cookie_name, context.GetServerInitialMetadata())
                .value_or(Cookie())};
  } else {
    std::cout << status.error_code() << ": " << status.error_message()
              << std::endl;
    return {"RPC failed", Cookie{}};
  }
}

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  auto channel = grpc::CreateChannel(
      absl::GetFlag(FLAGS_target),
      absl::GetFlag(FLAGS_secure)
          ? grpc::XdsCredentials(grpc::InsecureChannelCredentials())
          : grpc::InsecureChannelCredentials());
  auto stub = Greeter::NewStub(channel);
  std::string cookie_name = absl::GetFlag(FLAGS_ssa_cookie);
  std::string user("world");
  // Do not send cookie the first time. Let gRPC generate a session cookie once
  // the endpoint is chosen.
  auto [reply, cookie] =
      SayHelloAndGetCookie(stub, user, cookie_name, std::nullopt);
  // Session cookie is available at this point.
  std::cout << "Greeter received: " << reply
            << ", session cookie: " << cookie.value
            << ", max-age: " << cookie.max_age_s.value_or(-1)
            << std::endl;
  // Do another call, this time including the cookie. Note that client code is
  // supposed to track the cookie max age if it was included.
  std::tie(reply, cookie) =
      SayHelloAndGetCookie(stub, user, cookie_name, cookie);
  // In some cases the cookie value may be different, e.g. if the original
  // endpoint is no longer available.
  std::cout << "Greeter received: " << reply
            << ", session cookie: " << cookie.value
            << ", max-age: " << cookie.max_age_s.value_or(-1) << std::endl;
  return 0;
}
