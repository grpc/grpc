// Copyright 2022 The gRPC Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifdef GPR_WINDOWS

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  auto factory = []() {
    return std::make_unique<
        grpc_event_engine::experimental::WindowsEventEngine>();
  };
  SetEventEngineFactories(factory, factory);
  return RUN_ALL_TESTS();
}

#else  // not GPR_WINDOWS

int main(int /* argc */, char** /* argv */) { return 0; }

#endif  // GPR_WINDOWS
