from typing import Any

integer_types = int
text_type = str
binary_type = bytes

class Generator:
    dict: Any
    def __init__(self, dict) -> None: ...
    def generate(self): ...
