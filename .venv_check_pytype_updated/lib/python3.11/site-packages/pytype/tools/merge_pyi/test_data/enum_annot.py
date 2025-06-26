import enum


# Pytype special cases enum member as "Literal[int]" - we don't want to print it
class Fruit(enum.Enum):
  PEN = 1
  PINEAPPLE = 2
  APPLE = 3
  PEN_2 = 4
