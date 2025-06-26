def f1(x:e1="foo") -> r1:
    pass

def f2(x:e2=
    #
"foo") -> r2:
    pass

def f3(#
 x:e3#=
  = #
    #
 123#
#
) -> r3:
    pass


def f4(x:e4=(1,2)) -> r4:
    pass

def f5(x:e5=(1,)) -> r5:
    pass

def f6(x:e6=int) -> r6:
    pass

# static analysis would give error here
def f7(x : int=int):
    pass

def f8(x:e8={1:2}) -> r8:
    pass

def f9(x:e9=[1,2][:1]) -> r9:
    pass
