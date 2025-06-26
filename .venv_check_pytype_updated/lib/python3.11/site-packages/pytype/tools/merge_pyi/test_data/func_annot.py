def f():
  return 1

# The Never type in pyi file should not be merged here.
def g():
  raise Exception("hi")