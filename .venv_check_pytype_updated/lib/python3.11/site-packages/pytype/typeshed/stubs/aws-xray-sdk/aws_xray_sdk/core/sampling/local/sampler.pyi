from _typeshed import Incomplete
from typing import Any

from ...exceptions.exceptions import InvalidSamplingManifestError as InvalidSamplingManifestError
from .sampling_rule import SamplingRule as SamplingRule

local_sampling_rule: Any
SUPPORTED_RULE_VERSION: Any

class LocalSampler:
    def __init__(self, rules=...) -> None: ...
    def should_trace(self, sampling_req: Incomplete | None = None): ...
    def load_local_rules(self, rules) -> None: ...
