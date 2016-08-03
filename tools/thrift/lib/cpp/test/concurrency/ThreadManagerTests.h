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

#include <thrift/thrift-config.h>
#include <thrift/concurrency/ThreadManager.h>
#include <thrift/concurrency/PlatformThreadFactory.h>
#include <thrift/concurrency/Monitor.h>
#include <thrift/concurrency/Util.h>

#include <assert.h>
#include <set>
#include <iostream>
#include <set>
#include <stdint.h>

namespace apache {
namespace thrift {
namespace concurrency {
namespace test {

using namespace apache::thrift::concurrency;

class ThreadManagerTests {

  static const double TEST_TOLERANCE;

public:
  class Task : public Runnable {

  public:
    Task(Monitor& monitor, size_t& count, int64_t timeout)
      : _monitor(monitor), _count(count), _timeout(timeout), _done(false) {}

    void run() {

      _startTime = Util::currentTime();

      {
        Synchronized s(_sleep);

        try {
          _sleep.wait(_timeout);
        } catch (TimedOutException&) {
          ;
        } catch (...) {
          assert(0);
        }
      }

      _endTime = Util::currentTime();

      _done = true;

      {
        Synchronized s(_monitor);

        // std::cout << "Thread " << _count << " completed " << std::endl;

        _count--;

        if (_count == 0) {

          _monitor.notify();
        }
      }
    }

    Monitor& _monitor;
    size_t& _count;
    int64_t _timeout;
    int64_t _startTime;
    int64_t _endTime;
    bool _done;
    Monitor _sleep;
  };

  /**
   * Dispatch count tasks, each of which blocks for timeout milliseconds then
   * completes. Verify that all tasks completed and that thread manager cleans
   * up properly on delete.
   */
  bool loadTest(size_t count = 100, int64_t timeout = 100LL, size_t workerCount = 4) {

    Monitor monitor;

    size_t activeCount = count;

    shared_ptr<ThreadManager> threadManager = ThreadManager::newSimpleThreadManager(workerCount);

    shared_ptr<PlatformThreadFactory> threadFactory
        = shared_ptr<PlatformThreadFactory>(new PlatformThreadFactory());

#if !USE_BOOST_THREAD && !USE_STD_THREAD
    threadFactory->setPriority(PosixThreadFactory::HIGHEST);
#endif
    threadManager->threadFactory(threadFactory);

    threadManager->start();

    std::set<shared_ptr<ThreadManagerTests::Task> > tasks;

    for (size_t ix = 0; ix < count; ix++) {

      tasks.insert(shared_ptr<ThreadManagerTests::Task>(
          new ThreadManagerTests::Task(monitor, activeCount, timeout)));
    }

    int64_t time00 = Util::currentTime();

    for (std::set<shared_ptr<ThreadManagerTests::Task> >::iterator ix = tasks.begin();
         ix != tasks.end();
         ix++) {

      threadManager->add(*ix);
    }

    {
      Synchronized s(monitor);

      while (activeCount > 0) {

        monitor.wait();
      }
    }

    int64_t time01 = Util::currentTime();

    int64_t firstTime = 9223372036854775807LL;
    int64_t lastTime = 0;

    double averageTime = 0;
    int64_t minTime = 9223372036854775807LL;
    int64_t maxTime = 0;

    for (std::set<shared_ptr<ThreadManagerTests::Task> >::iterator ix = tasks.begin();
         ix != tasks.end();
         ix++) {

      shared_ptr<ThreadManagerTests::Task> task = *ix;

      int64_t delta = task->_endTime - task->_startTime;

      assert(delta > 0);

      if (task->_startTime < firstTime) {
        firstTime = task->_startTime;
      }

      if (task->_endTime > lastTime) {
        lastTime = task->_endTime;
      }

      if (delta < minTime) {
        minTime = delta;
      }

      if (delta > maxTime) {
        maxTime = delta;
      }

      averageTime += delta;
    }

    averageTime /= count;

    std::cout << "\t\t\tfirst start: " << firstTime << "ms Last end: " << lastTime
              << "ms min: " << minTime << "ms max: " << maxTime << "ms average: " << averageTime
              << "ms" << std::endl;

    double expectedTime = (double(count + (workerCount - 1)) / workerCount) * timeout;

    double error = ((time01 - time00) - expectedTime) / expectedTime;

    if (error < 0) {
      error *= -1.0;
    }

    bool success = error < TEST_TOLERANCE;

    std::cout << "\t\t\t" << (success ? "Success" : "Failure")
              << "! expected time: " << expectedTime << "ms elapsed time: " << time01 - time00
              << "ms error%: " << error * 100.0 << std::endl;

    return success;
  }

  class BlockTask : public Runnable {

  public:
    BlockTask(Monitor& monitor, Monitor& bmonitor, size_t& count)
      : _monitor(monitor), _bmonitor(bmonitor), _count(count) {}

    void run() {
      {
        Synchronized s(_bmonitor);

        _bmonitor.wait();
      }

      {
        Synchronized s(_monitor);

        _count--;

        if (_count == 0) {

          _monitor.notify();
        }
      }
    }

    Monitor& _monitor;
    Monitor& _bmonitor;
    size_t& _count;
  };

  /**
   * Block test.  Create pendingTaskCountMax tasks.  Verify that we block adding the
   * pendingTaskCountMax + 1th task.  Verify that we unblock when a task completes */

  bool blockTest(int64_t timeout = 100LL, size_t workerCount = 2) {
    (void)timeout;
    bool success = false;

    try {

      Monitor bmonitor;
      Monitor monitor;

      size_t pendingTaskMaxCount = workerCount;

      size_t activeCounts[] = {workerCount, pendingTaskMaxCount, 1};

      shared_ptr<ThreadManager> threadManager
          = ThreadManager::newSimpleThreadManager(workerCount, pendingTaskMaxCount);

      shared_ptr<PlatformThreadFactory> threadFactory
          = shared_ptr<PlatformThreadFactory>(new PlatformThreadFactory());

#if !USE_BOOST_THREAD && !USE_STD_THREAD
      threadFactory->setPriority(PosixThreadFactory::HIGHEST);
#endif
      threadManager->threadFactory(threadFactory);

      threadManager->start();

      std::set<shared_ptr<ThreadManagerTests::BlockTask> > tasks;

      for (size_t ix = 0; ix < workerCount; ix++) {

        tasks.insert(shared_ptr<ThreadManagerTests::BlockTask>(
            new ThreadManagerTests::BlockTask(monitor, bmonitor, activeCounts[0])));
      }

      for (size_t ix = 0; ix < pendingTaskMaxCount; ix++) {

        tasks.insert(shared_ptr<ThreadManagerTests::BlockTask>(
            new ThreadManagerTests::BlockTask(monitor, bmonitor, activeCounts[1])));
      }

      for (std::set<shared_ptr<ThreadManagerTests::BlockTask> >::iterator ix = tasks.begin();
           ix != tasks.end();
           ix++) {
        threadManager->add(*ix);
      }

      if (!(success = (threadManager->totalTaskCount() == pendingTaskMaxCount + workerCount))) {
        throw TException("Unexpected pending task count");
      }

      shared_ptr<ThreadManagerTests::BlockTask> extraTask(
          new ThreadManagerTests::BlockTask(monitor, bmonitor, activeCounts[2]));

      try {
        threadManager->add(extraTask, 1);
        throw TException("Unexpected success adding task in excess of pending task count");
      } catch (TooManyPendingTasksException&) {
        throw TException("Should have timed out adding task in excess of pending task count");
      } catch (TimedOutException&) {
        // Expected result
      }

      try {
        threadManager->add(extraTask, -1);
        throw TException("Unexpected success adding task in excess of pending task count");
      } catch (TimedOutException&) {
        throw TException("Unexpected timeout adding task in excess of pending task count");
      } catch (TooManyPendingTasksException&) {
        // Expected result
      }

      std::cout << "\t\t\t"
                << "Pending tasks " << threadManager->pendingTaskCount() << std::endl;

      {
        Synchronized s(bmonitor);

        bmonitor.notifyAll();
      }

      {
        Synchronized s(monitor);

        while (activeCounts[0] != 0) {
          monitor.wait();
        }
      }

      std::cout << "\t\t\t"
                << "Pending tasks " << threadManager->pendingTaskCount() << std::endl;

      try {
        threadManager->add(extraTask, 1);
      } catch (TimedOutException&) {
        std::cout << "\t\t\t"
                  << "add timed out unexpectedly" << std::endl;
        throw TException("Unexpected timeout adding task");

      } catch (TooManyPendingTasksException&) {
        std::cout << "\t\t\t"
                  << "add encountered too many pending exepctions" << std::endl;
        throw TException("Unexpected timeout adding task");
      }

      // Wake up tasks that were pending before and wait for them to complete

      {
        Synchronized s(bmonitor);

        bmonitor.notifyAll();
      }

      {
        Synchronized s(monitor);

        while (activeCounts[1] != 0) {
          monitor.wait();
        }
      }

      // Wake up the extra task and wait for it to complete

      {
        Synchronized s(bmonitor);

        bmonitor.notifyAll();
      }

      {
        Synchronized s(monitor);

        while (activeCounts[2] != 0) {
          monitor.wait();
        }
      }

      if (!(success = (threadManager->totalTaskCount() == 0))) {
        throw TException("Unexpected pending task count");
      }

    } catch (TException& e) {
      std::cout << "ERROR: " << e.what() << std::endl;
    }

    std::cout << "\t\t\t" << (success ? "Success" : "Failure") << std::endl;
    return success;
  }
};

const double ThreadManagerTests::TEST_TOLERANCE = .20;
}
}
}
} // apache::thrift::concurrency

using namespace apache::thrift::concurrency::test;
