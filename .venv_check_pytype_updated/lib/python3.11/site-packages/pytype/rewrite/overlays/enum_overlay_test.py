from typing import cast

from pytype.rewrite.abstract import abstract
from pytype.rewrite.overlays import enum_overlay
from pytype.rewrite.tests import test_utils

import unittest


class EnumMetaNewTest(test_utils.ContextfulTestBase):

  def test_call(self):
    # Simulate:
    #   class E(enum.Enum):
    #     X = 42
    metaclass = cast(abstract.SimpleClass,
                     self.ctx.abstract_loader.load_value('enum', 'EnumMeta'))
    base = cast(abstract.SimpleClass,
                self.ctx.abstract_loader.load_value('enum', 'Enum'))
    enum_cls = abstract.SimpleClass(
        ctx=self.ctx,
        name='E',
        members={'X': self.ctx.consts[42]},
        bases=(base,),
        keywords={'metaclass': metaclass},
    )
    enum_overlay.transform_enum_class(self.ctx, enum_cls)
    enum_member = enum_cls.members['X']
    self.assertIsInstance(enum_member, abstract.BaseInstance)
    self.assertEqual(enum_member.cls.name, 'E')


if __name__ == '__main__':
  unittest.main()
