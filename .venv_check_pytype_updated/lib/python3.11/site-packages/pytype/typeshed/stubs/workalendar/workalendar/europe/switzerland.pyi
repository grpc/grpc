from _typeshed import Incomplete
from typing import ClassVar

from ..core import WesternCalendar

class Switzerland(WesternCalendar):
    include_good_friday: ClassVar[bool]
    include_easter_sunday: ClassVar[bool]
    include_easter_monday: ClassVar[bool]
    include_ascension: ClassVar[bool]
    include_whit_sunday: ClassVar[bool]
    include_whit_monday: ClassVar[bool]
    include_christmas: ClassVar[bool]
    include_boxing_day: ClassVar[bool]
    include_epiphany: ClassVar[bool]
    include_corpus_christi: ClassVar[bool]
    include_assumption: ClassVar[bool]
    include_all_saints: ClassVar[bool]
    include_immaculate_conception: ClassVar[bool]
    include_berchtolds_day: ClassVar[bool]
    include_st_josephs_day: ClassVar[bool]
    FIXED_HOLIDAYS: Incomplete
    def has_berchtolds_day(self, year): ...
    def get_federal_thanksgiving_monday(self, year): ...
    def get_variable_days(self, year): ...

class Aargau(Switzerland):
    include_berchtolds_day: ClassVar[bool]
    include_corpus_christi: ClassVar[bool]
    include_all_saints: ClassVar[bool]
    include_immaculate_conception: ClassVar[bool]

class AppenzellInnerrhoden(Switzerland):
    include_corpus_christi: ClassVar[bool]
    include_assumption: ClassVar[bool]
    include_all_saints: ClassVar[bool]
    include_immaculate_conception: ClassVar[bool]

class AppenzellAusserrhoden(Switzerland):
    include_labour_day: ClassVar[bool]

class Bern(Switzerland):
    include_berchtolds_day: ClassVar[bool]

class BaselLandschaft(Switzerland):
    include_labour_day: ClassVar[bool]

class BaselStadt(Switzerland):
    include_labour_day: ClassVar[bool]

class Fribourg(Switzerland):
    include_berchtolds_day: ClassVar[bool]
    include_labour_day: ClassVar[bool]
    include_corpus_christi: ClassVar[bool]
    include_assumption: ClassVar[bool]
    include_all_saints: ClassVar[bool]
    include_immaculate_conception: ClassVar[bool]

class Geneva(Switzerland):
    include_boxing_day: ClassVar[bool]
    FIXED_HOLIDAYS: Incomplete
    def get_genevan_fast(self, year): ...
    def get_variable_days(self, year): ...

class Glarus(Switzerland):
    include_berchtolds_day: ClassVar[bool]
    include_all_saints: ClassVar[bool]
    FIXED_HOLIDAYS: Incomplete

class Graubunden(Switzerland):
    include_epiphany: ClassVar[bool]
    include_st_josephs_day: ClassVar[bool]
    include_corpus_christi: ClassVar[bool]
    include_immaculate_conception: ClassVar[bool]

class Jura(Switzerland):
    include_berchtolds_day: ClassVar[bool]
    include_labour_day: ClassVar[bool]
    include_corpus_christi: ClassVar[bool]
    include_assumption: ClassVar[bool]
    include_all_saints: ClassVar[bool]
    include_boxing_day: ClassVar[bool]
    FIXED_HOLIDAYS: Incomplete

class Luzern(Switzerland):
    include_berchtolds_day: ClassVar[bool]
    include_epiphany: ClassVar[bool]
    include_st_josephs_day: ClassVar[bool]
    include_corpus_christi: ClassVar[bool]
    include_assumption: ClassVar[bool]
    include_all_saints: ClassVar[bool]
    include_immaculate_conception: ClassVar[bool]

class Neuchatel(Switzerland):
    include_boxing_day: ClassVar[bool]
    include_labour_day: ClassVar[bool]
    FIXED_HOLIDAYS: Incomplete
    def has_berchtolds_day(self, year): ...
    def get_variable_days(self, year): ...

class Nidwalden(Switzerland):
    include_st_josephs_day: ClassVar[bool]
    include_corpus_christi: ClassVar[bool]
    include_assumption: ClassVar[bool]
    include_all_saints: ClassVar[bool]
    include_immaculate_conception: ClassVar[bool]

class Obwalden(Switzerland):
    include_berchtolds_day: ClassVar[bool]
    include_corpus_christi: ClassVar[bool]
    include_assumption: ClassVar[bool]
    include_all_saints: ClassVar[bool]
    include_immaculate_conception: ClassVar[bool]
    FIXED_HOLIDAYS: Incomplete

class StGallen(Switzerland):
    include_all_saints: ClassVar[bool]

class Schaffhausen(Switzerland):
    include_berchtolds_day: ClassVar[bool]
    include_labour_day: ClassVar[bool]

class Solothurn(Switzerland):
    include_berchtolds_day: ClassVar[bool]
    include_st_josephs_day: ClassVar[bool]
    include_labour_day: ClassVar[bool]
    include_corpus_christi: ClassVar[bool]
    include_assumption: ClassVar[bool]
    include_all_saints: ClassVar[bool]
    include_immaculate_conception: ClassVar[bool]

class Schwyz(Switzerland):
    include_epiphany: ClassVar[bool]
    include_st_josephs_day: ClassVar[bool]
    include_corpus_christi: ClassVar[bool]
    include_assumption: ClassVar[bool]
    include_all_saints: ClassVar[bool]
    include_immaculate_conception: ClassVar[bool]

class Thurgau(Switzerland):
    include_berchtolds_day: ClassVar[bool]
    include_labour_day: ClassVar[bool]

class Ticino(Switzerland):
    include_good_friday: ClassVar[bool]
    include_epiphany: ClassVar[bool]
    include_st_josephs_day: ClassVar[bool]
    include_labour_day: ClassVar[bool]
    include_corpus_christi: ClassVar[bool]
    include_assumption: ClassVar[bool]
    include_all_saints: ClassVar[bool]
    include_immaculate_conception: ClassVar[bool]
    FIXED_HOLIDAYS: Incomplete

class Uri(Switzerland):
    include_epiphany: ClassVar[bool]
    include_st_josephs_day: ClassVar[bool]
    include_corpus_christi: ClassVar[bool]
    include_assumption: ClassVar[bool]
    include_all_saints: ClassVar[bool]
    include_immaculate_conception: ClassVar[bool]

class Vaud(Switzerland):
    include_berchtolds_day: ClassVar[bool]
    include_boxing_day: ClassVar[bool]
    def get_variable_days(self, year): ...

class Valais(Switzerland):
    include_good_friday: ClassVar[bool]
    include_easter_monday: ClassVar[bool]
    include_whit_monday: ClassVar[bool]
    include_st_josephs_day: ClassVar[bool]
    include_corpus_christi: ClassVar[bool]
    include_assumption: ClassVar[bool]
    include_all_saints: ClassVar[bool]
    include_immaculate_conception: ClassVar[bool]
    include_boxing_day: ClassVar[bool]

class Zug(Switzerland):
    include_berchtolds_day: ClassVar[bool]
    include_corpus_christi: ClassVar[bool]
    include_assumption: ClassVar[bool]
    include_all_saints: ClassVar[bool]
    include_immaculate_conception: ClassVar[bool]

class Zurich(Switzerland):
    include_berchtolds_day: ClassVar[bool]
    include_labour_day: ClassVar[bool]
