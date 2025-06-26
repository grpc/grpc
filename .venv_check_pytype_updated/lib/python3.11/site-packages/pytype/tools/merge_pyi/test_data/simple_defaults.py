def f1(x=12):
    pass

def f2(x=-12):
    pass

def f3(x=12):
    pass

def f4(x=-12):
    pass

def f5(x=12.3):
    pass

def f6(x=-12.3):
    pass

def f7(x="asd"):
    pass

def f8(x="asd"):
    pass

def f9(x=r"asd"):
    pass

def f10(x=True):
    pass

def f11(x=False):
    pass

# Broken
def f12(x=3j):
    pass

def f13(x=(1+2j)):
    pass

def f14(x=(1.3+2j)):
    pass
