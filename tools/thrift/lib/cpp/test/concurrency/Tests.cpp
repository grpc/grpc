/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements. See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership. The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <iostream>
#include <vector>
#include <string>

#include "ThreadFactoryTests.h"
#include "TimerManagerTests.h"
#include "ThreadManagerTests.h"

int main(int argc, char** argv) {

  std::string arg;

  std::vector<std::string> args(argc - 1 > 1 ? argc - 1 : 1);

  args[0] = "all";

  for (int ix = 1; ix < argc; ix++) {
    args[ix - 1] = std::string(argv[ix]);
  }

  bool runAll = args[0].compare("all") == 0;

  if (runAll || args[0].compare("thread-factory") == 0) {

    ThreadFactoryTests threadFactoryTests;

    std::cout << "ThreadFactory tests..." << std::endl;

    size_t count = 1000;
    size_t floodLoops = 1;
    size_t floodCount = 100000;

    std::cout << "\t\tThreadFactory reap N threads test: N = " << count << std::endl;

    assert(threadFactoryTests.reapNThreads(count));

    std::cout << "\t\tThreadFactory floodN threads test: N = " << floodCount << std::endl;

    assert(threadFactoryTests.floodNTest(floodLoops, floodCount));

    std::cout << "\t\tThreadFactory synchronous start test" << std::endl;

    assert(threadFactoryTests.synchStartTest());

    std::cout << "\t\tThreadFactory monitor timeout test" << std::endl;

    assert(threadFactoryTests.monitorTimeoutTest());
  }

  if (runAll || args[0].compare("util") == 0) {

    std::cout << "Util tests..." << std::endl;

    std::cout << "\t\tUtil minimum time" << std::endl;

    int64_t time00 = Util::currentTime();
    int64_t time01 = Util::currentTime();

    std::cout << "\t\t\tMinimum time: " << time01 - time00 << "ms" << std::endl;

    time00 = Util::currentTime();
    time01 = time00;
    size_t count = 0;

    while (time01 < time00 + 10) {
      count++;
      time01 = Util::currentTime();
    }

    std::cout << "\t\t\tscall per ms: " << count / (time01 - time00) << std::endl;
  }

  if (runAll || args[0].compare("timer-manager") == 0) {

    std::cout << "TimerManager tests..." << std::endl;

    std::cout << "\t\tTimerManager test00" << std::endl;

    TimerManagerTests timerManagerTests;

    assert(timerManagerTests.test00());
  }

  if (runAll || args[0].compare("thread-manager") == 0) {

    std::cout << "ThreadManager tests..." << std::endl;

    {

      size_t workerCount = 100;

      size_t taskCount = 100000;

      int64_t delay = 10LL;

      std::cout << "\t\tThreadManager load test: worker count: " << workerCount
                << " task count: " << taskCount << " delay: " << delay << std::endl;

      ThreadManagerTests threadManagerTests;

      assert(threadManagerTests.loadTest(taskCount, delay, workerCount));

      std::cout << "\t\tThreadManager block test: worker count: " << workerCount
                << " delay: " << delay << std::endl;

      assert(threadManagerTests.blockTest(delay, workerCount));
    }
  }

  if (runAll || args[0].compare("thread-manager-benchmark") == 0) {

    std::cout << "ThreadManager benchmark tests..." << std::endl;

    {

      size_t minWorkerCount = 2;

      size_t maxWorkerCount = 512;

      size_t tasksPerWorker = 1000;

      int64_t delay = 10LL;

      for (size_t workerCount = minWorkerCount; workerCount < maxWorkerCount; workerCount *= 2) {

        size_t taskCount = workerCount * tasksPerWorker;

        std::cout << "\t\tThreadManager load test: worker count: " << workerCount
                  << " task count: " << taskCount << " delay: " << delay << std::endl;

        ThreadManagerTests threadManagerTests;

        threadManagerTests.loadTest(taskCount, delay, workerCount);
      }
    }
  }
}
