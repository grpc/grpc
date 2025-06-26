import ast
import textwrap

from pytype import config
from pytype.tests import test_base
from pytype.tools.annotate_ast import annotate_ast


class AnnotaterTest(test_base.BaseTest):

  def annotate(self, source):
    source = textwrap.dedent(source.lstrip('\n'))
    pytype_options = config.Options.create(python_version=self.python_version)

    module = annotate_ast.annotate_source(source, ast, pytype_options)
    return module

  def get_annotations_dict(self, module):
    return {self._get_node_key(node): node.resolved_annotation
            for node in ast.walk(module)
            if hasattr(node, 'resolved_type')}

  def _get_node_key(self, node):
    base = (node.lineno, node.__class__.__name__)

    if isinstance(node, ast.Name):
      return base + (node.id,)
    elif isinstance(node, ast.Attribute):
      return base + (node.attr,)
    elif isinstance(node, ast.FunctionDef):
      return base + (node.name,)
    else:
      return base

  def test_annotating_name(self):
    source = """
    a = 1
    b = {1: 'foo'}
    c = [1, 2, 3]
    d = 3, 4
    """
    module = self.annotate(source)

    expected = {
        (1, 'Name', 'a'): 'int',
        (2, 'Name', 'b'): 'dict[int, str]',
        (3, 'Name', 'c'): 'list[int]',
        (4, 'Name', 'd'): 'tuple[int, int]',
    }
    self.assertEqual(expected, self.get_annotations_dict(module))

  def test_annotating_attribute(self):
    source = """
    f = Foo()
    x = f.Bar().bar()
    """

    module = self.annotate(source)

    expected = {
        (1, 'Name', 'f'): 'Any',
        (1, 'Name', 'Foo'): 'Any',
        (2, 'Name', 'x'): 'Any',
        (2, 'Name', 'f'): 'Any',
        (2, 'Attribute', 'Bar'): 'Any',
        (2, 'Attribute', 'bar'): 'Any',
    }
    self.assertEqual(expected, self.get_annotations_dict(module))

  def test_annotating_for(self):
    source = """
    for i in 1, 2, 3:
      pass
    """

    module = self.annotate(source)

    expected = {
        (1, 'Name', 'i'): 'int',
    }
    self.assertEqual(expected, self.get_annotations_dict(module))

  def test_annotating_with(self):
    source = """
    with foo() as f:
      pass
    """

    module = self.annotate(source)

    expected = {
        (1, 'Name', 'foo'): 'Any',
        (1, 'Name', 'f'): 'Any',
    }
    self.assertEqual(expected, self.get_annotations_dict(module))

  def test_annotating_def(self):
    source = """
    def foo(a, b):
      # type: (str, int) -> str
      pass
    """

    module = self.annotate(source)

    expected = {
        (1, 'FunctionDef', 'foo'): 'Callable[[str, int], str]',
    }
    self.assertEqual(expected, self.get_annotations_dict(module))


if __name__ == '__main__':
  test_base.main()
