from xml.etree.ElementTree import Element

from markdown.blockprocessors import BlockProcessor
from markdown.extensions import Extension
from markdown.postprocessors import RawHtmlPostprocessor

class MarkdownInHtmlProcessor(BlockProcessor):
    def parse_element_content(self, element: Element) -> None: ...

class MarkdownInHTMLPostprocessor(RawHtmlPostprocessor):
    def stash_to_string(self, text: str | Element) -> str: ...

class MarkdownInHtmlExtension(Extension): ...

def makeExtension(**kwargs) -> MarkdownInHtmlExtension: ...
