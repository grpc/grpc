from typing import Any

class ReverseDnsSetting:
    OFF: Any
    REQUIRE_RESOLVE_ALL_ADDRESSES: Any
    REQUIRE_RESOLVE_IP_ADDRESSES_ONLY: Any
    OPTIONAL_RESOLVE_ALL_ADDRESSES: Any
    OPTIONAL_RESOLVE_IP_ADDRESSES_ONLY: Any
    SUPPORTED_VALUES: Any

def get_hostname_by_addr(addr, success_required: bool = True): ...
def is_ip_addr(addr): ...
