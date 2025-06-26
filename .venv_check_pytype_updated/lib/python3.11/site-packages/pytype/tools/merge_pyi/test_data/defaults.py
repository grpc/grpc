def f1(x="foo"):
    pass

def f2(x=
    #
"foo"):
    pass

def f3(#
 x#=
  = #
    #
 123#
#
):
    pass


def f4(x=(1,2)):
    pass

def f5(x=(1,)):
    pass

def f6(x=int):
    pass

# static analysis would give error here
def f7(x : int=int):
    pass

def f8(x={1:2}):
    pass

def f9(x=[1,2][:1]):
    pass
