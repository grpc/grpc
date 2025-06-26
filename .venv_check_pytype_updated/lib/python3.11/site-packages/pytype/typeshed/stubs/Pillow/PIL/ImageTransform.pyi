from _typeshed import Incomplete

from .Image import ImageTransformHandler

class Transform(ImageTransformHandler):
    data: Incomplete
    def __init__(self, data) -> None: ...
    def getdata(self): ...
    def transform(self, size, image, **options): ...

class AffineTransform(Transform):
    method: Incomplete

class ExtentTransform(Transform):
    method: Incomplete

class QuadTransform(Transform):
    method: Incomplete

class MeshTransform(Transform):
    method: Incomplete
