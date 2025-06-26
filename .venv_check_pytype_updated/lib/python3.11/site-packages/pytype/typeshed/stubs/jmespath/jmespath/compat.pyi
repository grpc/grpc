from _typeshed import Incomplete
from collections.abc import Generator
from itertools import zip_longest as zip_longest

text_type = str
string_type = str

def with_str_method(cls): ...
def with_repr_method(cls): ...
def get_methods(cls) -> Generator[Incomplete, None, None]: ...
