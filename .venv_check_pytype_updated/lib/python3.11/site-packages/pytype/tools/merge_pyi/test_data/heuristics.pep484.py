# If not annotate_pep484, info in pyi files is augmented with heuristics to decide if un-annotated
# arguments are "Any" or "" (like "self")

class B:
    def __init__(self):
        pass

    def f(self, x: e1):
        pass

class C:
    def __init__(self, x: e2):
        pass

    @staticmethod
    def f2():
        pass

    @staticmethod
    def f3(x, y: e3):
        pass

    @classmethod
    def f4(cls):
        pass
