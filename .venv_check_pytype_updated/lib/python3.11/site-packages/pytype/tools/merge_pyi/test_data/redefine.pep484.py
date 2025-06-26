def f1(x) -> Union[int, str]:
    return 1
def f1(x) -> Union[int, str]:
    return 'foo'

def f2(x) -> None:
    pass
def f2(x,y) -> None:
    pass

def f3(x: int) -> int:
    return 'asd'+x
def f3(x: int) -> int:
    return 1+x
