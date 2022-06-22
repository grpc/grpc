#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_LINUX_EVENTFD

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <grpc/support/log.h>

#include "src/core/lib/event_engine/iomgr_engine/wakeup_fd_posix.h"
#include "src/core/lib/iomgr/socket_utils_posix.h"
#include "src/core/lib/profiling/timers.h"
#endif

#include "src/core/lib/event_engine/iomgr_engine/wakeup_fd_eventfd.h"

namespace grpc_event_engine {
namespace iomgr_engine {

#ifdef GRPC_LINUX_EVENTFD

absl::Status EventFdWakeupFd::Init() {
  this->read_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  this->write_fd_ = -1;
  if (this->read_fd_ < 0) {
    return GRPC_OS_ERROR(errno, "eventfd");
  }
  return absl::OkStatus();
}

absl::Status EventFdWakeupFd::Consume() {
  eventfd_t value;
  int err;
  do {
    err = eventfd_read(this->read_fd_, &value);
  } while (err < 0 && errno == EINTR);
  if (err < 0 && errno != EAGAIN) {
    return GRPC_OS_ERROR(errno, "eventfd_read");
  }
  return absl::OkStatus();
}

absl::Status EventFdWakeupFd::Wakeup() {
  int err;
  do {
    err = eventfd_write(this->read_fd_, 1);
  } while (err < 0 && errno == EINTR);
  if (err < 0) {
    return GRPC_OS_ERROR(errno, "eventfd_write");
  }
  return absl::OkStatus();
}

void EventFdWakeupFd::Destroy() {
  if (this->read_fd_ != 0) {
    close(this->read_fd_);
    this->read_fd_ = 0;
  }
}

bool EventFdWakeupFd::IsSupported() {
  EventFdWakeupFd event_fd_wakeup_fd;
  if (event_fd_wakeup_fd.Init().ok()) {
    event_fd_wakeup_fd.Destroy();
    return true;
  } else {
    return false;
  }
}

absl::StatusOr<std::shared_ptr<EventFdWakeupFd>>
EventFdWakeupFd::CreateEventFdWakeupFd() {
  static kIsEventFdWakeupFdSupported = EventFdWakeupFd::IsSupported();
  if (kIsEventFdWakeupFdSupported) {
    auto event_fd_wakeup_fd = std::make_shared<EventFdWakeupFd>();
    auto status = pipe_wakeup_fd.Init();
    if (status.ok()) {
      return pipe_wakeup_fd;
    }
    return status;
  }
  return nullptr;
}

#else  //  GRPC_LINUX_EVENTFD

absl::Status EventFdWakeupFd::Init() { GPR_ASSERT(false && "unimplemented"); }

absl::Status EventFdWakeupFd::ConsumeWakeup() {
  GPR_ASSERT(false && "unimplemented");
}

absl::Status EventFdWakeupFd::Wakeup() { GPR_ASSERT(false && "unimplemented"); }

void EventFdWakeupFd::Destroy() { GPR_ASSERT(false && "unimplemented"); }

bool EventFdWakeupFd::Supported() { return false; }

absl::StatusOr<std::shared_ptr<EventFdWakeupFd>>
EventFdWakeupFd::CreatePipeWakeupFd() {
  return absl::NotFoundError("Eventfd wakeup fd is not supported");
}

#endif  // GRPC_LINUX_EVENTFD

}  // namespace iomgr_engine
}  // namespace grpc_event_engine
