import datetime
from _typeshed import Incomplete
from collections import defaultdict
from collections.abc import Callable, Container, Iterable, Iterator, Mapping, Sequence
from typing import Any, Literal, TypeVar, overload
from typing_extensions import Self, TypeAlias
from urllib.parse import ParseResult, SplitResult

from vobject.base import VBase

from .davclient import DAVClient
from .elements.cdav import CalendarData, CalendarQuery, CompFilter, ScheduleInboxURL, ScheduleOutboxURL
from .lib.url import URL

_CC = TypeVar("_CC", bound=CalendarObjectResource)
# Actually "type[Todo] | type[Event] | type[Journal]", but mypy doesn't like that.
_CompClass: TypeAlias = type[CalendarObjectResource]
_VCalAddress: TypeAlias = Incomplete  # actually icalendar.vCalAddress

class DAVObject:
    id: str | None
    url: URL | None
    client: DAVClient | None
    parent: DAVObject | None
    name: str | None
    props: Mapping[Incomplete, Incomplete]
    extra_init_options: dict[str, Incomplete]
    def __init__(
        self,
        client: DAVClient | None = None,
        url: str | ParseResult | SplitResult | URL | None = None,
        parent: DAVObject | None = None,
        name: str | None = None,
        id: str | None = None,
        props: Mapping[Incomplete, Incomplete] | None = None,
        **extra: Incomplete,
    ) -> None: ...
    @property
    def canonical_url(self) -> str: ...
    def children(self, type: str | None = None) -> list[tuple[URL, Incomplete, Incomplete]]: ...
    def get_property(self, prop, use_cached: bool = False, **passthrough) -> Incomplete | None: ...
    def get_properties(
        self, props: Incomplete | None = None, depth: int = 0, parse_response_xml: bool = True, parse_props: bool = True
    ): ...
    def set_properties(self, props: Incomplete | None = None) -> Self: ...
    def save(self) -> Self: ...
    def delete(self) -> None: ...

class CalendarSet(DAVObject):
    def calendars(self) -> list[Calendar]: ...
    def make_calendar(
        self, name: str | None = None, cal_id: str | None = None, supported_calendar_component_set: Incomplete | None = None
    ) -> Calendar: ...
    def calendar(self, name: str | None = None, cal_id: str | None = None) -> Calendar: ...

class Principal(DAVObject):
    def __init__(self, client: DAVClient | None = None, url: str | ParseResult | SplitResult | URL | None = None) -> None: ...
    def calendars(self) -> list[Calendar]: ...
    def make_calendar(
        self, name: str | None = None, cal_id: str | None = None, supported_calendar_component_set: Incomplete | None = None
    ) -> Calendar: ...
    def calendar(self, name: str | None = None, cal_id: str | None = None, cal_url: str | None = None) -> Calendar: ...
    def get_vcal_address(self) -> _VCalAddress: ...
    calendar_home_set: CalendarSet  # can also be set to anything URL.objectify() accepts
    def freebusy_request(self, dtstart, dtend, attendees): ...
    def calendar_user_address_set(self) -> list[str]: ...
    def schedule_inbox(self) -> ScheduleInbox: ...
    def schedule_outbox(self) -> ScheduleOutbox: ...

class Calendar(DAVObject):
    def get_supported_components(self) -> list[Incomplete]: ...
    def save_with_invites(self, ical: str, attendees, **attendeeoptions) -> None: ...
    def save_event(
        self, ical: str | None = None, no_overwrite: bool = False, no_create: bool = False, **ical_data: Incomplete
    ) -> Event: ...
    def save_todo(
        self, ical: str | None = None, no_overwrite: bool = False, no_create: bool = False, **ical_data: Incomplete
    ) -> Todo: ...
    def save_journal(
        self, ical: str | None = None, no_overwrite: bool = False, no_create: bool = False, **ical_data: Incomplete
    ) -> Journal: ...
    add_event = save_event
    add_todo = save_todo
    add_journal = save_journal
    def calendar_multiget(self, event_urls: Iterable[URL]) -> list[Event]: ...
    def build_date_search_query(
        self,
        start,
        end: datetime.datetime | None = None,
        compfilter: Literal["VEVENT"] | None = "VEVENT",
        expand: bool | Literal["maybe"] = "maybe",
    ): ...
    @overload
    def date_search(
        self,
        start: datetime.datetime,
        end: datetime.datetime | None = None,
        compfilter: Literal["VEVENT"] = "VEVENT",
        expand: bool | Literal["maybe"] = "maybe",
        verify_expand: bool = False,
    ) -> list[Event]: ...
    @overload
    def date_search(
        self,
        start: datetime.datetime,
        *,
        compfilter: None,
        expand: bool | Literal["maybe"] = "maybe",
        verify_expand: bool = False,
    ) -> list[CalendarObjectResource]: ...
    @overload
    def date_search(
        self,
        start: datetime.datetime,
        end: datetime.datetime | None,
        compfilter: None,
        expand: bool | Literal["maybe"] = "maybe",
        verify_expand: bool = False,
    ) -> list[CalendarObjectResource]: ...
    @overload
    def search(
        self,
        xml: None = None,
        comp_class: None = None,
        todo: bool | None = None,
        include_completed: bool = False,
        sort_keys: Sequence[str] = (),
        split_expanded: bool = True,
        props: list[CalendarData] | None = None,
        **kwargs,
    ) -> list[CalendarObjectResource]: ...
    @overload
    def search(
        self,
        xml,
        comp_class: type[_CC],
        todo: bool | None = None,
        include_completed: bool = False,
        sort_keys: Sequence[str] = (),
        split_expanded: bool = True,
        props: list[CalendarData] | None = None,
        **kwargs,
    ) -> list[_CC]: ...
    @overload
    def search(
        self,
        *,
        comp_class: type[_CC],
        todo: bool | None = None,
        include_completed: bool = False,
        sort_keys: Sequence[str] = (),
        split_expanded: bool = True,
        props: list[CalendarData] | None = None,
        **kwargs,
    ) -> list[_CC]: ...
    def build_search_xml_query(
        self,
        comp_class: _CompClass | None = None,
        todo: bool | None = None,
        ignore_completed1: bool | None = None,
        ignore_completed2: bool | None = None,
        ignore_completed3: bool | None = None,
        event: bool | None = None,
        filters: list[Incomplete] | None = None,
        expand: bool | None = None,
        start: datetime.datetime | None = None,
        end: datetime.datetime | None = None,
        props: list[CalendarData] | None = None,
        *,
        uid=...,
        summary=...,
        comment=...,
        description=...,
        location=...,
        status=...,
        **kwargs: str,
    ) -> tuple[CalendarQuery, _CompClass]: ...
    def freebusy_request(self, start: datetime.datetime, end: datetime.datetime) -> FreeBusy: ...
    def todos(
        self, sort_keys: Iterable[str] = ("due", "priority"), include_completed: bool = False, sort_key: str | None = None
    ) -> list[Todo]: ...
    def event_by_url(self, href, data: Incomplete | None = None) -> Event: ...
    def object_by_uid(self, uid: str, comp_filter: CompFilter | None = None, comp_class: _CompClass | None = None) -> Event: ...
    def todo_by_uid(self, uid: str) -> CalendarObjectResource: ...
    def event_by_uid(self, uid: str) -> CalendarObjectResource: ...
    def journal_by_uid(self, uid: str) -> CalendarObjectResource: ...
    event = event_by_uid
    def events(self) -> list[Event]: ...
    def objects_by_sync_token(
        self, sync_token: Incomplete | None = None, load_objects: bool = False
    ) -> SynchronizableCalendarObjectCollection: ...
    objects = objects_by_sync_token
    def journals(self) -> list[Journal]: ...

class ScheduleMailbox(Calendar):
    def __init__(
        self,
        client: DAVClient | None = None,
        principal: Principal | None = None,
        url: str | ParseResult | SplitResult | URL | None = None,
    ) -> None: ...
    def get_items(self): ...

class ScheduleInbox(ScheduleMailbox):
    findprop = ScheduleInboxURL

class ScheduleOutbox(ScheduleMailbox):
    findprop = ScheduleOutboxURL

class SynchronizableCalendarObjectCollection:
    def __init__(self, calendar, objects, sync_token) -> None: ...
    def __iter__(self) -> Iterator[Incomplete]: ...
    def __len__(self) -> int: ...
    def objects_by_url(self): ...
    def sync(self) -> tuple[Incomplete, Incomplete]: ...

class CalendarObjectResource(DAVObject):
    def __init__(
        self,
        client: DAVClient | None = None,
        url: str | ParseResult | SplitResult | URL | None = None,
        data: Incomplete | None = None,
        parent: Incomplete | None = None,
        id: Incomplete | None = None,
        props: Incomplete | None = None,
    ) -> None: ...
    def add_organizer(self) -> None: ...
    def split_expanded(self) -> list[Self]: ...
    def expand_rrule(self, start: datetime.datetime, end: datetime.datetime) -> None: ...
    def get_relatives(
        self,
        reltypes: Container[str] | None = None,
        relfilter: Callable[[Any], bool] | None = None,
        fetch_objects: bool = True,
        ignore_missing: bool = True,
    ) -> defaultdict[str, set[str]]: ...
    def add_attendee(self, attendee, no_default_parameters: bool = False, **parameters) -> None: ...
    def is_invite_request(self) -> bool: ...
    def accept_invite(self, calendar: Incomplete | None = None) -> None: ...
    def decline_invite(self, calendar: Incomplete | None = None) -> None: ...
    def tentatively_accept_invite(self, calendar: Incomplete | None = None) -> None: ...
    def copy(self, keep_uid: bool = False, new_parent: Incomplete | None = None) -> Self: ...
    def load(self, only_if_unloaded: bool = False) -> Self: ...
    def change_attendee_status(self, attendee: Incomplete | None = None, **kwargs) -> None: ...
    def save(
        self,
        no_overwrite: bool = False,
        no_create: bool = False,
        obj_type: str | None = None,
        increase_seqno: bool = True,
        if_schedule_tag_match: bool = False,
    ) -> Self: ...
    def get_duration(self) -> datetime.timedelta: ...
    data: Incomplete
    vobject_instance: VBase
    icalendar_instance: Incomplete
    instance: VBase

class Event(CalendarObjectResource): ...
class Journal(CalendarObjectResource): ...

class FreeBusy(CalendarObjectResource):
    def __init__(
        self, parent, data, url: str | ParseResult | SplitResult | URL | None = None, id: Incomplete | None = None
    ) -> None: ...

class Todo(CalendarObjectResource):
    def complete(
        self,
        completion_timestamp: datetime.datetime | None = None,
        handle_rrule: bool = False,
        rrule_mode: Literal["safe", "this_and_future"] = "safe",
    ) -> None: ...
