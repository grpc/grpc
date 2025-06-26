# Following imports need to be copied into .py

from m1 import A
from m2 import B, B2
import m3
from m4 import (D,
D2)
from m5.sub import E
from m6 import F
from mStar import *  # G
from m7 import (a, a2, a3)
from m8 import (b, b2, )
from ......m9 import c
import existing_import
from existing_import import d

def f(a1: A, a2: B, a3: m3.C, a4: D, a5: E, a6: F) -> G: ...
def g(a7: a, a8: b, a9: c, a10: d) -> existing_import: ...
