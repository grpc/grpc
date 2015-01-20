CLANG_FORMAT=clang-format-3.5

set -ex

if not hash $CLANG_FORMAT 2>/dev/null; then
  echo "$CLANG_FORMAT is needed but not installed"
  echo "perhaps try:"
  echo "  sudo apt-get install $CLANG_FORMAT"
  exit 1
fi

