def f1(x: e1) -> r1:
    pass

def f2(x) -> r2:
    pass

def f3(x: e3):
    pass

def f4(x: e4) -> r4:
    pass

def f5(x:             # stripme
    e5) -> r5:
    pass

def f6(x: e6) -> r6:
    pass

def f7(x: e7) -> r7:
    pass

def f8(x:     \
     e8)     \
     ->    \
     r8:
    pass

def f9(x) -> """
this is 
valid""":
    pass

def f10(x: """
this is 
valid"""):
    pass
