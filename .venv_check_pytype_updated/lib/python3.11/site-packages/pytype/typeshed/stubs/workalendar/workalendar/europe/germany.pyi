from _typeshed import Incomplete
from typing import ClassVar

from ..core import WesternCalendar

class Germany(WesternCalendar):
    include_labour_day: ClassVar[bool]
    FIXED_HOLIDAYS: Incomplete
    include_easter_monday: ClassVar[bool]
    include_ascension: ClassVar[bool]
    include_whit_monday: ClassVar[bool]
    include_good_friday: ClassVar[bool]
    include_boxing_day: ClassVar[bool]
    boxing_day_label: ClassVar[str]
    all_time_include_reformation_day: ClassVar[bool]
    include_reformation_day_2018: ClassVar[bool]
    def include_reformation_day(self, year): ...
    def get_reformation_day(self, year): ...
    def get_variable_days(self, year): ...

class BadenWurttemberg(Germany):
    include_epiphany: ClassVar[bool]
    include_corpus_christi: ClassVar[bool]
    include_all_saints: ClassVar[bool]

class Bavaria(Germany):
    include_epiphany: ClassVar[bool]
    include_corpus_christi: ClassVar[bool]
    include_all_saints: ClassVar[bool]
    include_assumption: ClassVar[bool]

class Berlin(Germany):
    def get_international_womens_day(self, year): ...
    def get_liberation_day(self, year): ...
    def get_variable_days(self, year): ...

class Brandenburg(Germany):
    include_easter_sunday: ClassVar[bool]
    all_time_include_reformation_day: ClassVar[bool]

class Bremen(Germany):
    include_reformation_day_2018: ClassVar[bool]

class Hamburg(Germany):
    include_reformation_day_2018: ClassVar[bool]

class Hesse(Germany):
    include_corpus_christi: ClassVar[bool]

class MecklenburgVorpommern(Germany):
    all_time_include_reformation_day: ClassVar[bool]

class LowerSaxony(Germany):
    include_reformation_day_2018: ClassVar[bool]

class NorthRhineWestphalia(Germany):
    include_corpus_christi: ClassVar[bool]
    include_all_saints: ClassVar[bool]

class RhinelandPalatinate(Germany):
    include_corpus_christi: ClassVar[bool]
    include_all_saints: ClassVar[bool]

class Saarland(Germany):
    include_corpus_christi: ClassVar[bool]
    include_assumption: ClassVar[bool]
    include_all_saints: ClassVar[bool]

class Saxony(Germany):
    all_time_include_reformation_day: ClassVar[bool]
    def get_repentance_day(self, year): ...
    def get_variable_days(self, year): ...

class SaxonyAnhalt(Germany):
    include_epiphany: ClassVar[bool]
    all_time_include_reformation_day: ClassVar[bool]

class SchleswigHolstein(Germany):
    include_reformation_day_2018: ClassVar[bool]

class Thuringia(Germany):
    all_time_include_reformation_day: ClassVar[bool]
