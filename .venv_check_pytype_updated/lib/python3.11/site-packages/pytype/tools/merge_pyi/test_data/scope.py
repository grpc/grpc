class C:
    def f(self, x):
        pass

    def g(self):
        def f(x): #gets ignored by pytype but fixer sees it, generates warning (FIXME?)
            return 1
        return f
