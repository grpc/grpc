class C:
    def f(self, x: e1) -> r1:
        pass

    def g(self) -> function:
        def f(x): #gets ignored by pytype but fixer sees it, generates warning (FIXME?)
            return 1
        return f
