def f1(*a):
    pass

def f2(**a):
    pass

def f3(a, *b):
    pass

def f4(a, **b):
    pass

## arg with default after *args is valid python3, not python2
def f5(*a, b=1):
    pass

def f6(*a, b=1, **c):
    pass

def f7(x=1, *a, b=1, **c):
    pass

def f8(#asd
        *a):
    pass
