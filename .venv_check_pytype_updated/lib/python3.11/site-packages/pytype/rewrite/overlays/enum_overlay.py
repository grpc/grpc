"""Enum overlay."""
from pytype.rewrite.abstract import abstract
from pytype.rewrite.overlays import overlays


@overlays.register_class_transform(inheritance_hook='enum.Enum')
def transform_enum_class(
    ctx: abstract.ContextType, cls: abstract.SimpleClass) -> None:
  for name, value in cls.members.items():
    if name.startswith('__') and name.endswith('__'):
      continue
    member = abstract.MutableInstance(ctx, cls)
    member.members['name'] = name
    member.members['value'] = value
    cls.members[name] = member
