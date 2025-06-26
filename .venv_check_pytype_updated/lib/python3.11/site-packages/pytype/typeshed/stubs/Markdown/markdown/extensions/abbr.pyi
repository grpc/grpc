from re import Pattern
from typing import ClassVar

from markdown.blockprocessors import BlockProcessor
from markdown.extensions import Extension
from markdown.inlinepatterns import InlineProcessor

class AbbrExtension(Extension): ...

class AbbrPreprocessor(BlockProcessor):
    RE: ClassVar[Pattern[str]]

class AbbrInlineProcessor(InlineProcessor):
    title: str
    def __init__(self, pattern: str, title: str) -> None: ...

def makeExtension(**kwargs) -> AbbrExtension: ...
