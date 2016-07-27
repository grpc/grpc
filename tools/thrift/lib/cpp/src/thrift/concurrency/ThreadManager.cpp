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
#include <thrift/concurrency/Exception.h>
#include <thrift/concurrency/Monitor.h>
#include <thrift/concurrency/Util.h>

#include <boost/shared_ptr.hpp>

#include <assert.h>
#include <queue>
#include <set>

#if defined(DEBUG)
#include <iostream>
#endif // defined(DEBUG)

namespace apache {
namespace thrift {
namespace concurrency {

using boost::shared_ptr;
using boost::dynamic_pointer_cast;

/**
 * ThreadManager class
 *
 * This class manages a pool of threads. It uses a ThreadFactory to create
 * threads.  It never actually creates or destroys worker threads, rather
 * it maintains statistics on number of idle threads, number of active threads,
 * task backlog, and average wait and service times.
 *
 * @version $Id:$
 */
class ThreadManager::Impl : public ThreadManager {

public:
  Impl()
    : workerCount_(0),
      workerMaxCount_(0),
      idleCount_(0),
      pendingTaskCountMax_(0),
      expiredCount_(0),
      state_(ThreadManager::UNINITIALIZED),
      monitor_(&mutex_),
      maxMonitor_(&mutex_) {}

  ~Impl() { stop(); }

  void start();

  void stop() { stopImpl(false); }

  void join() { stopImpl(true); }

  ThreadManager::STATE state() const { return state_; }

  shared_ptr<ThreadFactory> threadFactory() const {
    Synchronized s(monitor_);
    return threadFactory_;
  }

  void threadFactory(shared_ptr<ThreadFactory> value) {
    Synchronized s(monitor_);
    threadFactory_ = value;
  }

  void addWorker(size_t value);

  void removeWorker(size_t value);

  size_t idleWorkerCount() const { return idleCount_; }

  size_t workerCount() const {
    Synchronized s(monitor_);
    return workerCount_;
  }

  size_t pendingTaskCount() const {
    Synchronized s(monitor_);
    return tasks_.size();
  }

  size_t totalTaskCount() const {
    Synchronized s(monitor_);
    return tasks_.size() + workerCount_ - idleCount_;
  }

  size_t pendingTaskCountMax() const {
    Synchronized s(monitor_);
    return pendingTaskCountMax_;
  }

  size_t expiredTaskCount() {
    Synchronized s(monitor_);
    size_t result = expiredCount_;
    expiredCount_ = 0;
    return result;
  }

  void pendingTaskCountMax(const size_t value) {
    Synchronized s(monitor_);
    pendingTaskCountMax_ = value;
  }

  bool canSleep();

  void add(shared_ptr<Runnable> value, int64_t timeout, int64_t expiration);

  void remove(shared_ptr<Runnable> task);

  shared_ptr<Runnable> removeNextPending();

  void removeExpiredTasks();

  void setExpireCallback(ExpireCallback expireCallback);

private:
  void stopImpl(bool join);

  size_t workerCount_;
  size_t workerMaxCount_;
  size_t idleCount_;
  size_t pendingTaskCountMax_;
  size_t expiredCount_;
  ExpireCallback expireCallback_;

  ThreadManager::STATE state_;
  shared_ptr<ThreadFactory> threadFactory_;

  friend class ThreadManager::Task;
  std::queue<shared_ptr<Task> > tasks_;
  Mutex mutex_;
  Monitor monitor_;
  Monitor maxMonitor_;
  Monitor workerMonitor_;

  friend class ThreadManager::Worker;
  std::set<shared_ptr<Thread> > workers_;
  std::set<shared_ptr<Thread> > deadWorkers_;
  std::map<const Thread::id_t, shared_ptr<Thread> > idMap_;
};

class ThreadManager::Task : public Runnable {

public:
  enum STATE { WAITING, EXECUTING, CANCELLED, COMPLETE };

  Task(shared_ptr<Runnable> runnable, int64_t expiration = 0LL)
    : runnable_(runnable),
      state_(WAITING),
      expireTime_(expiration != 0LL ? Util::currentTime() + expiration : 0LL) {}

  ~Task() {}

  void run() {
    if (state_ == EXECUTING) {
      runnable_->run();
      state_ = COMPLETE;
    }
  }

  shared_ptr<Runnable> getRunnable() { return runnable_; }

  int64_t getExpireTime() const { return expireTime_; }

private:
  shared_ptr<Runnable> runnable_;
  friend class ThreadManager::Worker;
  STATE state_;
  int64_t expireTime_;
};

class ThreadManager::Worker : public Runnable {
  enum STATE { UNINITIALIZED, STARTING, STARTED, STOPPING, STOPPED };

public:
  Worker(ThreadManager::Impl* manager) : manager_(manager), state_(UNINITIALIZED), idle_(false) {}

  ~Worker() {}

private:
  bool isActive() const {
    return (manager_->workerCount_ <= manager_->workerMaxCount_)
           || (manager_->state_ == JOINING && !manager_->tasks_.empty());
  }

public:
  /**
   * Worker entry point
   *
   * As long as worker thread is running, pull tasks off the task queue and
   * execute.
   */
  void run() {
    bool active = false;
    /**
     * Increment worker semaphore and notify manager if worker count reached
     * desired max
     *
     * Note: We have to release the monitor and acquire the workerMonitor
     * since that is what the manager blocks on for worker add/remove
     */
    {
      bool notifyManager = false;
      {
        Synchronized s(manager_->monitor_);
        active = manager_->workerCount_ < manager_->workerMaxCount_;
        if (active) {
          manager_->workerCount_++;
          notifyManager = manager_->workerCount_ == manager_->workerMaxCount_;
        }
      }

      if (notifyManager) {
        Synchronized s(manager_->workerMonitor_);
        manager_->workerMonitor_.notify();
      }
    }

    while (active) {
      shared_ptr<ThreadManager::Task> task;

      /**
       * While holding manager monitor block for non-empty task queue (Also
       * check that the thread hasn't been requested to stop). Once the queue
       * is non-empty, dequeue a task, release monitor, and execute. If the
       * worker max count has been decremented such that we exceed it, mark
       * ourself inactive, decrement the worker count and notify the manager
       * (technically we're notifying the next blocked thread but eventually
       * the manager will see it.
       */
      {
        Guard g(manager_->mutex_);
        active = isActive();

        while (active && manager_->tasks_.empty()) {
          manager_->idleCount_++;
          idle_ = true;
          manager_->monitor_.wait();
          active = isActive();
          idle_ = false;
          manager_->idleCount_--;
        }

        if (active) {
          manager_->removeExpiredTasks();

          if (!manager_->tasks_.empty()) {
            task = manager_->tasks_.front();
            manager_->tasks_.pop();
            if (task->state_ == ThreadManager::Task::WAITING) {
              task->state_ = ThreadManager::Task::EXECUTING;
            }
          }

          /* If we have a pending task max and we just dropped below it, wakeup any
             thread that might be blocked on add. */
          if (manager_->pendingTaskCountMax_ != 0
              && manager_->tasks_.size() <= manager_->pendingTaskCountMax_ - 1) {
            manager_->maxMonitor_.notify();
          }
        }
      }

      if (task) {
        if (task->state_ == ThreadManager::Task::EXECUTING) {
          try {
            task->run();
          } catch (const std::exception& e) {
            GlobalOutput.printf("[ERROR] task->run() raised an exception: %s", e.what());
          } catch (...) {
            GlobalOutput.printf("[ERROR] task->run() raised an unknown exception");
          }
        }
      }
    }

    {
      Synchronized s(manager_->workerMonitor_);
      manager_->deadWorkers_.insert(this->thread());
      idle_ = true;
      manager_->workerCount_--;
      bool notifyManager = (manager_->workerCount_ == manager_->workerMaxCount_);
      if (notifyManager) {
        manager_->workerMonitor_.notify();
      }
    }

    return;
  }

private:
  ThreadManager::Impl* manager_;
  friend class ThreadManager::Impl;
  STATE state_;
  bool idle_;
};

void ThreadManager::Impl::addWorker(size_t value) {
  std::set<shared_ptr<Thread> > newThreads;
  for (size_t ix = 0; ix < value; ix++) {
    shared_ptr<ThreadManager::Worker> worker
        = shared_ptr<ThreadManager::Worker>(new ThreadManager::Worker(this));
    newThreads.insert(threadFactory_->newThread(worker));
  }

  {
    Synchronized s(monitor_);
    workerMaxCount_ += value;
    workers_.insert(newThreads.begin(), newThreads.end());
  }

  for (std::set<shared_ptr<Thread> >::iterator ix = newThreads.begin(); ix != newThreads.end();
       ++ix) {
    shared_ptr<ThreadManager::Worker> worker
        = dynamic_pointer_cast<ThreadManager::Worker, Runnable>((*ix)->runnable());
    worker->state_ = ThreadManager::Worker::STARTING;
    (*ix)->start();
    idMap_.insert(std::pair<const Thread::id_t, shared_ptr<Thread> >((*ix)->getId(), *ix));
  }

  {
    Synchronized s(workerMonitor_);
    while (workerCount_ != workerMaxCount_) {
      workerMonitor_.wait();
    }
  }
}

void ThreadManager::Impl::start() {

  if (state_ == ThreadManager::STOPPED) {
    return;
  }

  {
    Synchronized s(monitor_);
    if (state_ == ThreadManager::UNINITIALIZED) {
      if (!threadFactory_) {
        throw InvalidArgumentException();
      }
      state_ = ThreadManager::STARTED;
      monitor_.notifyAll();
    }

    while (state_ == STARTING) {
      monitor_.wait();
    }
  }
}

void ThreadManager::Impl::stopImpl(bool join) {
  bool doStop = false;
  if (state_ == ThreadManager::STOPPED) {
    return;
  }

  {
    Synchronized s(monitor_);
    if (state_ != ThreadManager::STOPPING && state_ != ThreadManager::JOINING
        && state_ != ThreadManager::STOPPED) {
      doStop = true;
      state_ = join ? ThreadManager::JOINING : ThreadManager::STOPPING;
    }
  }

  if (doStop) {
    removeWorker(workerCount_);
  }

  // XXX
  // should be able to block here for transition to STOPPED since we're no
  // using shared_ptrs

  {
    Synchronized s(monitor_);
    state_ = ThreadManager::STOPPED;
  }
}

void ThreadManager::Impl::removeWorker(size_t value) {
  std::set<shared_ptr<Thread> > removedThreads;
  {
    Synchronized s(monitor_);
    if (value > workerMaxCount_) {
      throw InvalidArgumentException();
    }

    workerMaxCount_ -= value;

    if (idleCount_ < value) {
      for (size_t ix = 0; ix < idleCount_; ix++) {
        monitor_.notify();
      }
    } else {
      monitor_.notifyAll();
    }
  }

  {
    Synchronized s(workerMonitor_);

    while (workerCount_ != workerMaxCount_) {
      workerMonitor_.wait();
    }

    for (std::set<shared_ptr<Thread> >::iterator ix = deadWorkers_.begin();
         ix != deadWorkers_.end();
         ++ix) {
      idMap_.erase((*ix)->getId());
      workers_.erase(*ix);
    }

    deadWorkers_.clear();
  }
}

bool ThreadManager::Impl::canSleep() {
  const Thread::id_t id = threadFactory_->getCurrentThreadId();
  return idMap_.find(id) == idMap_.end();
}

void ThreadManager::Impl::add(shared_ptr<Runnable> value, int64_t timeout, int64_t expiration) {
  Guard g(mutex_, timeout);

  if (!g) {
    throw TimedOutException();
  }

  if (state_ != ThreadManager::STARTED) {
    throw IllegalStateException(
        "ThreadManager::Impl::add ThreadManager "
        "not started");
  }

  removeExpiredTasks();
  if (pendingTaskCountMax_ > 0 && (tasks_.size() >= pendingTaskCountMax_)) {
    if (canSleep() && timeout >= 0) {
      while (pendingTaskCountMax_ > 0 && tasks_.size() >= pendingTaskCountMax_) {
        // This is thread safe because the mutex is shared between monitors.
        maxMonitor_.wait(timeout);
      }
    } else {
      throw TooManyPendingTasksException();
    }
  }

  tasks_.push(shared_ptr<ThreadManager::Task>(new ThreadManager::Task(value, expiration)));

  // If idle thread is available notify it, otherwise all worker threads are
  // running and will get around to this task in time.
  if (idleCount_ > 0) {
    monitor_.notify();
  }
}

void ThreadManager::Impl::remove(shared_ptr<Runnable> task) {
  (void)task;
  Synchronized s(monitor_);
  if (state_ != ThreadManager::STARTED) {
    throw IllegalStateException(
        "ThreadManager::Impl::remove ThreadManager not "
        "started");
  }
}

boost::shared_ptr<Runnable> ThreadManager::Impl::removeNextPending() {
  Guard g(mutex_);
  if (state_ != ThreadManager::STARTED) {
    throw IllegalStateException(
        "ThreadManager::Impl::removeNextPending "
        "ThreadManager not started");
  }

  if (tasks_.empty()) {
    return boost::shared_ptr<Runnable>();
  }

  shared_ptr<ThreadManager::Task> task = tasks_.front();
  tasks_.pop();

  return task->getRunnable();
}

void ThreadManager::Impl::removeExpiredTasks() {
  int64_t now = 0LL; // we won't ask for the time untile we need it

  // note that this loop breaks at the first non-expiring task
  while (!tasks_.empty()) {
    shared_ptr<ThreadManager::Task> task = tasks_.front();
    if (task->getExpireTime() == 0LL) {
      break;
    }
    if (now == 0LL) {
      now = Util::currentTime();
    }
    if (task->getExpireTime() > now) {
      break;
    }
    if (expireCallback_) {
      expireCallback_(task->getRunnable());
    }
    tasks_.pop();
    expiredCount_++;
  }
}

void ThreadManager::Impl::setExpireCallback(ExpireCallback expireCallback) {
  expireCallback_ = expireCallback;
}

class SimpleThreadManager : public ThreadManager::Impl {

public:
  SimpleThreadManager(size_t workerCount = 4, size_t pendingTaskCountMax = 0)
    : workerCount_(workerCount), pendingTaskCountMax_(pendingTaskCountMax) {}

  void start() {
    ThreadManager::Impl::pendingTaskCountMax(pendingTaskCountMax_);
    ThreadManager::Impl::start();
    addWorker(workerCount_);
  }

private:
  const size_t workerCount_;
  const size_t pendingTaskCountMax_;
  Monitor monitor_;
};

shared_ptr<ThreadManager> ThreadManager::newThreadManager() {
  return shared_ptr<ThreadManager>(new ThreadManager::Impl());
}

shared_ptr<ThreadManager> ThreadManager::newSimpleThreadManager(size_t count,
                                                                size_t pendingTaskCountMax) {
  return shared_ptr<ThreadManager>(new SimpleThreadManager(count, pendingTaskCountMax));
}
}
}
} // apache::thrift::concurrency
