from typing import Dict, TypeVar

_KT = TypeVar('_KT')
_VT = TypeVar('_VT')

class UserDict(Dict[_KT, _VT]):
    def __init__(self, initialdata: Dict[_KT, _VT] = ...): ...


a,b = c, d

a = 3 # comment

# Below is unwanted

f(a=2)

f(
a=2
)

def f():
    dontwant = 3 
