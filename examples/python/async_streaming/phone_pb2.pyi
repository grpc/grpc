from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from typing import ClassVar as _ClassVar, Mapping as _Mapping, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class CallInfo(_message.Message):
    __slots__ = ["media", "session_id"]
    MEDIA_FIELD_NUMBER: _ClassVar[int]
    SESSION_ID_FIELD_NUMBER: _ClassVar[int]
    media: str
    session_id: str
    def __init__(self, session_id: _Optional[str] = ..., media: _Optional[str] = ...) -> None: ...

class CallState(_message.Message):
    __slots__ = ["state"]
    class State(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = []
    ACTIVE: CallState.State
    ENDED: CallState.State
    NEW: CallState.State
    STATE_FIELD_NUMBER: _ClassVar[int]
    UNDEFINED: CallState.State
    state: CallState.State
    def __init__(self, state: _Optional[_Union[CallState.State, str]] = ...) -> None: ...

class StreamCallRequest(_message.Message):
    __slots__ = ["phone_number"]
    PHONE_NUMBER_FIELD_NUMBER: _ClassVar[int]
    phone_number: str
    def __init__(self, phone_number: _Optional[str] = ...) -> None: ...

class StreamCallResponse(_message.Message):
    __slots__ = ["call_info", "call_state"]
    CALL_INFO_FIELD_NUMBER: _ClassVar[int]
    CALL_STATE_FIELD_NUMBER: _ClassVar[int]
    call_info: CallInfo
    call_state: CallState
    def __init__(self, call_info: _Optional[_Union[CallInfo, _Mapping]] = ..., call_state: _Optional[_Union[CallState, _Mapping]] = ...) -> None: ...
