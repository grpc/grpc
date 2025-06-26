"""This allows running pytype as `python -m pytype`."""

# pylint: disable=invalid-name

import sys

from pytype.tools.analyze_project.main import main


if __name__ == '__main__':
  sys.exit(main())
