import sys
from typing import Any

if sys.platform == "linux":
    RLIMIT_AS: int
    RLIMIT_CORE: int
    RLIMIT_CPU: int
    RLIMIT_DATA: int
    RLIMIT_FSIZE: int
    RLIMIT_LOCKS: int
    RLIMIT_MEMLOCK: int
    RLIMIT_MSGQUEUE: int
    RLIMIT_NICE: int
    RLIMIT_NOFILE: int
    RLIMIT_NPROC: int
    RLIMIT_RSS: int
    RLIMIT_RTPRIO: int
    RLIMIT_RTTIME: int
    RLIMIT_SIGPENDING: int
    RLIMIT_STACK: int
    RLIM_INFINITY: int

def getpagesize(*args, **kwargs) -> Any: ...
def getpriority(*args, **kwargs) -> Any: ...
def net_if_addrs(*args, **kwargs) -> Any: ...
def net_if_flags(*args, **kwargs) -> Any: ...
def net_if_is_running(*args, **kwargs) -> Any: ...
def net_if_mtu(*args, **kwargs) -> Any: ...

if sys.platform == "darwin":
    AF_LINK: int
    def net_if_duplex_speed(*args, **kwargs): ...

def setpriority(*args, **kwargs) -> Any: ...
