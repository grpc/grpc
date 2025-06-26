from _typeshed import Incomplete
from typing import ClassVar

from ..core import WesternCalendar

class Spain(WesternCalendar):
    include_epiphany: ClassVar[bool]
    include_good_friday: ClassVar[bool]
    include_assumption: ClassVar[bool]
    include_all_saints: ClassVar[bool]
    include_immaculate_conception: ClassVar[bool]
    include_labour_day: ClassVar[bool]
    labour_day_label: ClassVar[str]
    FIXED_HOLIDAYS: Incomplete

class Andalusia(Spain):
    FIXED_HOLIDAYS: Incomplete
    include_holy_thursday: ClassVar[bool]

class Aragon(Spain):
    FIXED_HOLIDAYS: Incomplete
    include_holy_thursday: ClassVar[bool]

class CastileAndLeon(Spain):
    FIXED_HOLIDAYS: Incomplete
    include_holy_thursday: ClassVar[bool]

class CastillaLaMancha(Spain):
    FIXED_HOLIDAYS: Incomplete
    include_holy_thursday: ClassVar[bool]

class CanaryIslands(Spain):
    FIXED_HOLIDAYS: Incomplete
    include_holy_thursday: ClassVar[bool]

class Catalonia(Spain):
    include_easter_monday: ClassVar[bool]
    include_boxing_day: ClassVar[bool]
    boxing_day_label: ClassVar[str]
    FIXED_HOLIDAYS: Incomplete

class Extremadura(Spain):
    FIXED_HOLIDAYS: Incomplete
    include_holy_thursday: ClassVar[bool]

class Galicia(Spain):
    FIXED_HOLIDAYS: Incomplete
    include_holy_thursday: ClassVar[bool]

class BalearicIslands(Spain):
    FIXED_HOLIDAYS: Incomplete
    include_holy_thursday: ClassVar[bool]
    include_easter_monday: ClassVar[bool]

class LaRioja(Spain):
    FIXED_HOLIDAYS: Incomplete
    include_holy_thursday: ClassVar[bool]

class CommunityofMadrid(Spain):
    FIXED_HOLIDAYS: Incomplete
    include_holy_thursday: ClassVar[bool]

class Murcia(Spain):
    FIXED_HOLIDAYS: Incomplete
    include_holy_thursday: ClassVar[bool]

class Navarre(Spain):
    include_holy_thursday: ClassVar[bool]
    include_easter_monday: ClassVar[bool]

class Asturias(Spain):
    FIXED_HOLIDAYS: Incomplete
    include_holy_thursday: ClassVar[bool]

class BasqueCountry(Spain):
    FIXED_HOLIDAYS: Incomplete
    include_holy_thursday: ClassVar[bool]
    include_easter_monday: ClassVar[bool]

class Cantabria(Spain):
    FIXED_HOLIDAYS: Incomplete
    include_holy_thursday: ClassVar[bool]

class ValencianCommunity(Spain):
    FIXED_HOLIDAYS: Incomplete
    include_easter_monday: ClassVar[bool]
