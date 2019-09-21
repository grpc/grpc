#ifndef GRPC_CORE_LIB_IOMGR_POLLER_EVENTMANAGER_INTERFACE_H_
#define GRPC_CORE_LIB_IOMGR_POLLER_EVENTMANAGER_INTERFACE_H_

namespace grpc {

namespace experimental {

class BaseEventManagerInterface {
 public:
  virtual ~BaseEventManagerInterface() {}
};

class EpollEventManagerInterface : public BaseEventManagerInterface {};

}  // namespace experimental
}  // namespace grpc

#endif  // GRPC_CORE_LIB_IOMGR_POLLER_EVENTMANAGER_INTERFACE_H_
