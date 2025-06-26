"""Tests for pytype.pyi.metadata."""

from pytype.pyi import metadata
import unittest


class StringTest(unittest.TestCase):

  def test_to_string(self):
    self.assertEqual(metadata.to_string({'tag': 'call'}), "{'tag': 'call'}")

  def test_from_string(self):
    self.assertEqual(metadata.from_string("{'tag': 'call'}"), {'tag': 'call'})


class CallToAnnotationTest(unittest.TestCase):

  def test_deprecated(self):
    annotation = metadata.call_to_annotation(
        'Deprecated', posargs=['squished by duck']
    )
    self.assertEqual(
        annotation, "{'tag': 'Deprecated', 'reason': 'squished by duck'}"
    )

  def test_noargs(self):
    annotation = metadata.call_to_annotation('Quack')
    self.assertEqual(
        annotation,
        "{'tag': 'call', 'fn': 'Quack', 'posargs': (), 'kwargs': {}}",
    )

  def test_posargs(self):
    annotation = metadata.call_to_annotation('Quack', posargs=[2])
    self.assertEqual(
        annotation,
        "{'tag': 'call', 'fn': 'Quack', 'posargs': [2], 'kwargs': {}}",
    )

  def test_kwargs(self):
    annotation = metadata.call_to_annotation('Quack', kwargs={'volume': 4.5})
    self.assertEqual(
        annotation,
        (
            "{'tag': 'call', 'fn': 'Quack',"
            " 'posargs': (), 'kwargs': {'volume': 4.5}}"
        ),
    )

  def test_allargs(self):
    annotation = metadata.call_to_annotation(
        'Quack', posargs=[2, 'brown'], kwargs={'volume': 4.5, 'mode': 'correct'}
    )
    self.assertEqual(
        annotation,
        (
            "{'tag': 'call', 'fn': 'Quack', 'posargs': [2, 'brown'],"
            " 'kwargs': {'volume': 4.5, 'mode': 'correct'}}"
        ),
    )


class ToPytdTest(unittest.TestCase):

  def test_deprecated(self):
    pytd = metadata.to_pytd({'tag': 'Deprecated', 'reason': 'squished by duck'})
    self.assertEqual(pytd, "Deprecated('squished by duck')")

  def test_call_noargs(self):
    pytd = metadata.to_pytd(
        {'tag': 'call', 'fn': 'Quack', 'posargs': (), 'kwargs': {}}
    )
    self.assertEqual(pytd, 'Quack()')

  def test_call_posargs(self):
    pytd = metadata.to_pytd(
        {'tag': 'call', 'fn': 'Quack', 'posargs': [2], 'kwargs': {}}
    )
    self.assertEqual(pytd, 'Quack(2)')

  def test_call_kwargs(self):
    pytd = metadata.to_pytd(
        {'tag': 'call', 'fn': 'Quack', 'posargs': (), 'kwargs': {'volume': 4.5}}
    )
    self.assertEqual(pytd, 'Quack(volume=4.5)')

  def test_call_allargs(self):
    pytd = metadata.to_pytd({
        'tag': 'call',
        'fn': 'Quack',
        'posargs': [2, 'brown'],
        'kwargs': {'volume': 4.5, 'mode': 'correct'},
    })
    self.assertEqual(pytd, "Quack(2, 'brown', volume=4.5, mode='correct')")

  def test_noncall_tag(self):
    self.assertEqual(metadata.to_pytd({'tag': 'sneeze'}), "{'tag': 'sneeze'}")

  def test_no_tag(self):
    self.assertEqual(metadata.to_pytd({}), '{}')


if __name__ == '__main__':
  unittest.main()
