from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from typing import ClassVar as _ClassVar, Mapping as _Mapping, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class Feature(_message.Message):
    __slots__ = ["location", "name"]
    LOCATION_FIELD_NUMBER: _ClassVar[int]
    NAME_FIELD_NUMBER: _ClassVar[int]
    location: Point
    name: str
    def __init__(self, name: _Optional[str] = ..., location: _Optional[_Union[Point, _Mapping]] = ...) -> None: ...

class Point(_message.Message):
    __slots__ = ["latitude", "longitude"]
    LATITUDE_FIELD_NUMBER: _ClassVar[int]
    LONGITUDE_FIELD_NUMBER: _ClassVar[int]
    latitude: int
    longitude: int
    def __init__(self, latitude: _Optional[int] = ..., longitude: _Optional[int] = ...) -> None: ...

class Rectangle(_message.Message):
    __slots__ = ["hi", "lo"]
    HI_FIELD_NUMBER: _ClassVar[int]
    LO_FIELD_NUMBER: _ClassVar[int]
    hi: Point
    lo: Point
    def __init__(self, lo: _Optional[_Union[Point, _Mapping]] = ..., hi: _Optional[_Union[Point, _Mapping]] = ...) -> None: ...

class RouteNote(_message.Message):
    __slots__ = ["location", "message"]
    LOCATION_FIELD_NUMBER: _ClassVar[int]
    MESSAGE_FIELD_NUMBER: _ClassVar[int]
    location: Point
    message: str
    def __init__(self, location: _Optional[_Union[Point, _Mapping]] = ..., message: _Optional[str] = ...) -> None: ...

class RouteSummary(_message.Message):
    __slots__ = ["distance", "elapsed_time", "feature_count", "point_count"]
    DISTANCE_FIELD_NUMBER: _ClassVar[int]
    ELAPSED_TIME_FIELD_NUMBER: _ClassVar[int]
    FEATURE_COUNT_FIELD_NUMBER: _ClassVar[int]
    POINT_COUNT_FIELD_NUMBER: _ClassVar[int]
    distance: int
    elapsed_time: int
    feature_count: int
    point_count: int
    def __init__(self, point_count: _Optional[int] = ..., feature_count: _Optional[int] = ..., distance: _Optional[int] = ..., elapsed_time: _Optional[int] = ...) -> None: ...
