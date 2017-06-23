/*
 *
 * Copyright 2015 gRPC authors.
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

#ifndef QPS_WORKER_H
#define QPS_WORKER_H

#include <memory>

#include <grpc/support/atm.h>

namespace grpc {

class Server;

namespace testing {

class WorkerServiceImpl;

class QpsWorker {
 public:
  explicit QpsWorker(int driver_port, int server_port = 0);
  ~QpsWorker();

  bool Done() const;
  void MarkDone();

 private:
  std::unique_ptr<WorkerServiceImpl> impl_;
  std::unique_ptr<Server> server_;

  gpr_atm done_;
};

}  // namespace testing
}  // namespace grpc

#endif
