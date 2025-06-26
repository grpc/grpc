"""Test import-as."""
import m4_old as m4
from m1 import A_old as A
from m2 import B_old as B
from m3 import C_old as C
from m5 import D_old as D
from m5.something import E_old as E

def f(a: A, b: B, c: C, d: D, e: E) -> m4.D:
    pass
