# With heuristics, have to pick between returning None or Any. If generating comment annotations,
# heuristics matter even if we have a pyi


def f1(x: e1):
    return 1

def f2(x: e2):
    pass

def f3(x: e3):
    return

def f4(x: e4):
    def f(y):
        return 1

def f5(x: e5):
    return \
           1

def f6(x: e6):
    return # foo
