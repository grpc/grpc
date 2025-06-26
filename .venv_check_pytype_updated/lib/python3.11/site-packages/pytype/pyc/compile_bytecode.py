"""Compiles a single .py to a .pyc and writes it to stdout."""

# These are C modules built into Python. Don't add any modules that are
# implemented in a .py:
import importlib.util
import marshal
import re
import sys

MAGIC = importlib.util.MAGIC_NUMBER

# This pattern is as per PEP-263.
ENCODING_PATTERN = "^[ \t\v]*#.*?coding[:=][ \t]*([-_.a-zA-Z0-9]+)"


def is_comment_only(line):
  return re.match("[ \t\v]*#.*", line) is not None


def _write32(f, w):
  f.write(
      bytearray(
          [(w >> 0) & 0xFF, (w >> 8) & 0xFF, (w >> 16) & 0xFF, (w >> 24) & 0xFF]
      )
  )


def write_pyc(f, codeobject, source_size=0, timestamp=0):
  f.write(MAGIC)
  f.write(b"\r\n\0\0")
  _write32(f, timestamp)
  _write32(f, source_size)
  f.write(marshal.dumps(codeobject))


def compile_to_pyc(data_file, filename, output, mode):
  """Compile the source code to byte code."""
  with open(data_file, encoding="utf-8") as fi:
    src = fi.read()
  compile_src_to_pyc(src, filename, output, mode)


def strip_encoding(src):
  """Strip encoding from a src string assumed to be read from a file."""
  # Python 2's compile function does not like the line specifying the encoding.
  # So, we strip it off if it is present, replacing it with an empty comment to
  # preserve the original line numbering. As per PEP-263, the line specifying
  # the encoding can occur only in the first or the second line.
  if "\n" not in src:
    return src
  l1, rest = src.split("\n", 1)
  if re.match(ENCODING_PATTERN, l1.rstrip()):
    return "#\n" + rest
  elif "\n" not in rest:
    return src
  l2, rest = rest.split("\n", 1)
  if is_comment_only(l1) and re.match(ENCODING_PATTERN, l2.rstrip()):
    return "#\n#\n" + rest
  return src


def compile_src_to_pyc(src, filename, output, mode):
  """Compile a string of source code."""
  try:
    codeobject = compile(src, filename, mode)
  except Exception as err:  # pylint: disable=broad-except
    output.write(b"\1")
    output.write(str(err).encode("utf-8"))
  else:
    output.write(b"\0")
    write_pyc(output, codeobject)


def main():
  if len(sys.argv) != 4:
    sys.exit(1)
  output = sys.stdout.buffer if hasattr(sys.stdout, "buffer") else sys.stdout
  compile_to_pyc(
      data_file=sys.argv[1],
      filename=sys.argv[2],
      output=output,
      mode=sys.argv[3],
  )


if __name__ == "__main__":
  main()
