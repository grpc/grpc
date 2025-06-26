ABOVE_NORMAL_PRIORITY_CLASS: int
BELOW_NORMAL_PRIORITY_CLASS: int
ERROR_ACCESS_DENIED: int
ERROR_INVALID_NAME: int
ERROR_PRIVILEGE_NOT_HELD: int
ERROR_SERVICE_DOES_NOT_EXIST: int
HIGH_PRIORITY_CLASS: int
IDLE_PRIORITY_CLASS: int
INFINITE: int
MIB_TCP_STATE_CLOSED: int
MIB_TCP_STATE_CLOSE_WAIT: int
MIB_TCP_STATE_CLOSING: int
MIB_TCP_STATE_DELETE_TCB: int
MIB_TCP_STATE_ESTAB: int
MIB_TCP_STATE_FIN_WAIT1: int
MIB_TCP_STATE_FIN_WAIT2: int
MIB_TCP_STATE_LAST_ACK: int
MIB_TCP_STATE_LISTEN: int
MIB_TCP_STATE_SYN_RCVD: int
MIB_TCP_STATE_SYN_SENT: int
MIB_TCP_STATE_TIME_WAIT: int
NORMAL_PRIORITY_CLASS: int
PSUTIL_CONN_NONE: int
REALTIME_PRIORITY_CLASS: int
WINDOWS_10: int
WINDOWS_7: int
WINDOWS_8: int
WINDOWS_8_1: int
WINDOWS_VISTA: int
WINVER: int
version: int

class TimeoutAbandoned(Exception): ...
class TimeoutExpired(Exception): ...

def QueryDosDevice(*args, **kwargs): ...  # incomplete
def boot_time(*args, **kwargs): ...  # incomplete
def check_pid_range(pid: int, /) -> None: ...
def cpu_count_cores(*args, **kwargs): ...  # incomplete
def cpu_count_logical(*args, **kwargs): ...  # incomplete
def cpu_freq(*args, **kwargs): ...  # incomplete
def cpu_stats(*args, **kwargs): ...  # incomplete
def cpu_times(*args, **kwargs): ...  # incomplete
def disk_io_counters(*args, **kwargs): ...  # incomplete
def disk_partitions(*args, **kwargs): ...  # incomplete
def disk_usage(*args, **kwargs): ...  # incomplete
def getloadavg(*args, **kwargs): ...  # incomplete
def getpagesize(*args, **kwargs): ...  # incomplete
def init_loadavg_counter(*args, **kwargs): ...  # incomplete
def net_connections(*args, **kwargs): ...  # incomplete
def net_if_addrs(*args, **kwargs): ...  # incomplete
def net_if_stats(*args, **kwargs): ...  # incomplete
def net_io_counters(*args, **kwargs): ...  # incomplete
def per_cpu_times(*args, **kwargs): ...  # incomplete
def pid_exists(*args, **kwargs): ...  # incomplete
def pids(*args, **kwargs): ...  # incomplete
def ppid_map(*args, **kwargs): ...  # incomplete
def proc_cmdline(*args, **kwargs): ...  # incomplete
def proc_cpu_affinity_get(*args, **kwargs): ...  # incomplete
def proc_cpu_affinity_set(*args, **kwargs): ...  # incomplete
def proc_cwd(*args, **kwargs): ...  # incomplete
def proc_environ(*args, **kwargs): ...  # incomplete
def proc_exe(*args, **kwargs): ...  # incomplete
def proc_info(*args, **kwargs): ...  # incomplete
def proc_io_counters(*args, **kwargs): ...  # incomplete
def proc_io_priority_get(*args, **kwargs): ...  # incomplete
def proc_io_priority_set(*args, **kwargs): ...  # incomplete
def proc_is_suspended(*args, **kwargs): ...  # incomplete
def proc_kill(*args, **kwargs): ...  # incomplete
def proc_memory_info(*args, **kwargs): ...  # incomplete
def proc_memory_maps(*args, **kwargs): ...  # incomplete
def proc_memory_uss(*args, **kwargs): ...  # incomplete
def proc_num_handles(*args, **kwargs): ...  # incomplete
def proc_open_files(*args, **kwargs): ...  # incomplete
def proc_priority_get(*args, **kwargs): ...  # incomplete
def proc_priority_set(*args, **kwargs): ...  # incomplete
def proc_suspend_or_resume(*args, **kwargs): ...  # incomplete
def proc_threads(*args, **kwargs): ...  # incomplete
def proc_times(*args, **kwargs): ...  # incomplete
def proc_username(*args, **kwargs): ...  # incomplete
def proc_wait(*args, **kwargs): ...  # incomplete
def sensors_battery(*args, **kwargs): ...  # incomplete
def set_debug(*args, **kwargs): ...  # incomplete
def swap_percent(*args, **kwargs): ...  # incomplete
def users(*args, **kwargs): ...  # incomplete
def virtual_mem(*args, **kwargs): ...  # incomplete
def winservice_enumerate(*args, **kwargs): ...  # incomplete
def winservice_query_config(*args, **kwargs): ...  # incomplete
def winservice_query_descr(*args, **kwargs): ...  # incomplete
def winservice_query_status(*args, **kwargs): ...  # incomplete
def winservice_start(*args, **kwargs): ...  # incomplete
def winservice_stop(*args, **kwargs): ...  # incomplete
