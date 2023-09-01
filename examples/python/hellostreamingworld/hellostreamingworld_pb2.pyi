from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from typing import ClassVar as _ClassVar, Optional as _Optional

DESCRIPTOR: _descriptor.FileDescriptor

class HelloReply(_message.Message):
    __slots__ = ["message"]
    MESSAGE_FIELD_NUMBER: _ClassVar[int]
    message: str
    def __init__(self, message: _Optional[str] = ...) -> None: ...

class HelloRequest(_message.Message):
    __slots__ = ["name", "num_greetings"]
    NAME_FIELD_NUMBER: _ClassVar[int]
    NUM_GREETINGS_FIELD_NUMBER: _ClassVar[int]
    name: str
    num_greetings: str
    def __init__(self, name: _Optional[str] = ..., num_greetings: _Optional[str] = ...) -> None: ...
