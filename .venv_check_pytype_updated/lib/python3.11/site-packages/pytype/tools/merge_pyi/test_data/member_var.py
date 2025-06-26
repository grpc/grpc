# pytype generates member variable annotations as comments, check that fix_annotate ignores them
# properly

class C:
    def __init__(self, x):
        self.y = 1 + x
