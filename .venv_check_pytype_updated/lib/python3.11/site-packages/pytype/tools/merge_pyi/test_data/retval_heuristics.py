# With heuristics, have to pick between returning None or Any. If generating comment annotations,
# heuristics matter even if we have a pyi


def f1(x):
    return 1

def f2(x):
    pass

def f3(x):
    return

def f4(x):
    def f(y):
        return 1

def f5(x):
    return \
           1

def f6(x):
    return # foo
