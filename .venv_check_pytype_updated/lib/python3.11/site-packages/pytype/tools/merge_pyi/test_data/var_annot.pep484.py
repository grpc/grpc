import datetime
# Built-in types are not merged
a = 1
b = 1.0
c = ""
d = True
# non built-in types should be merged
e: datetime.datetime = datetime.datetime(2024, 9, 30)
# existing type annotation should not get erased
f: int = 1
g: float = ""
h: str = ""
