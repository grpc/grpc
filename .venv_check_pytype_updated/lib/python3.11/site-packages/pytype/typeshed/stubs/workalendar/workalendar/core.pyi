from _typeshed import Incomplete
from collections.abc import Generator
from typing import ClassVar

MON: Incomplete
TUE: Incomplete
WED: Incomplete
THU: Incomplete
FRI: Incomplete
SAT: Incomplete
SUN: Incomplete
ISO_MON: Incomplete
ISO_TUE: Incomplete
ISO_WED: Incomplete
ISO_THU: Incomplete
ISO_FRI: Incomplete
ISO_SAT: Incomplete
ISO_SUN: Incomplete

def cleaned_date(day, keep_datetime: bool = False): ...
def daterange(start, end) -> Generator[Incomplete, None, None]: ...

class ChristianMixin:
    EASTER_METHOD: Incomplete
    include_epiphany: ClassVar[bool]
    include_clean_monday: ClassVar[bool]
    include_annunciation: ClassVar[bool]
    include_fat_tuesday: ClassVar[bool]
    fat_tuesday_label: ClassVar[str | None]
    include_ash_wednesday: ClassVar[bool]
    ash_wednesday_label: ClassVar[str]
    include_palm_sunday: ClassVar[bool]
    include_holy_thursday: ClassVar[bool]
    holy_thursday_label: ClassVar[str]
    include_good_friday: ClassVar[bool]
    good_friday_label: ClassVar[str]
    include_easter_monday: ClassVar[bool]
    include_easter_saturday: ClassVar[bool]
    easter_saturday_label: ClassVar[str]
    include_easter_sunday: ClassVar[bool]
    include_all_saints: ClassVar[bool]
    include_immaculate_conception: ClassVar[bool]
    immaculate_conception_label: ClassVar[str]
    include_christmas: ClassVar[bool]
    christmas_day_label: ClassVar[str]
    include_christmas_eve: ClassVar[bool]
    include_ascension: ClassVar[bool]
    include_assumption: ClassVar[bool]
    include_whit_sunday: ClassVar[bool]
    whit_sunday_label: ClassVar[str]
    include_whit_monday: ClassVar[bool]
    whit_monday_label: ClassVar[str]
    include_corpus_christi: ClassVar[bool]
    include_boxing_day: ClassVar[bool]
    boxing_day_label: ClassVar[str]
    include_all_souls: ClassVar[bool]
    def get_fat_tuesday(self, year): ...
    def get_ash_wednesday(self, year): ...
    def get_palm_sunday(self, year): ...
    def get_holy_thursday(self, year): ...
    def get_good_friday(self, year): ...
    def get_clean_monday(self, year): ...
    def get_easter_saturday(self, year): ...
    def get_easter_sunday(self, year): ...
    def get_easter_monday(self, year): ...
    def get_ascension_thursday(self, year): ...
    def get_whit_monday(self, year): ...
    def get_whit_sunday(self, year): ...
    def get_corpus_christi(self, year): ...
    def shift_christmas_boxing_days(self, year): ...
    def get_variable_days(self, year): ...

class WesternMixin(ChristianMixin):
    EASTER_METHOD: Incomplete
    WEEKEND_DAYS: Incomplete

class OrthodoxMixin(ChristianMixin):
    EASTER_METHOD: Incomplete
    WEEKEND_DAYS: Incomplete
    include_orthodox_christmas: ClassVar[bool]
    orthodox_christmas_day_label: ClassVar[str]
    def get_fixed_holidays(self, year): ...

class LunarMixin:
    @staticmethod
    def lunar(year, month, day): ...

class ChineseNewYearMixin(LunarMixin):
    include_chinese_new_year_eve: ClassVar[bool]
    chinese_new_year_eve_label: ClassVar[str]
    include_chinese_new_year: ClassVar[bool]
    chinese_new_year_label: ClassVar[str]
    include_chinese_second_day: ClassVar[bool]
    chinese_second_day_label: ClassVar[str]
    include_chinese_third_day: ClassVar[bool]
    chinese_third_day_label: ClassVar[str]
    shift_sunday_holidays: ClassVar[bool]
    shift_start_cny_sunday: ClassVar[bool]
    def get_chinese_new_year(self, year): ...
    def get_variable_days(self, year): ...
    def get_shifted_holidays(self, dates) -> Generator[Incomplete, None, None]: ...
    def get_calendar_holidays(self, year): ...

class CalverterMixin:
    conversion_method: Incomplete
    ISLAMIC_HOLIDAYS: Incomplete
    def __init__(self, *args, **kwargs) -> None: ...
    def converted(self, year): ...
    def calverted_years(self, year): ...
    def get_islamic_holidays(self): ...
    def get_delta_islamic_holidays(self, year) -> None: ...
    def get_variable_days(self, year): ...

class IslamicMixin(CalverterMixin):
    WEEKEND_DAYS: Incomplete
    conversion_method: Incomplete
    include_prophet_birthday: ClassVar[bool]
    include_day_after_prophet_birthday: ClassVar[bool]
    include_start_ramadan: ClassVar[bool]
    include_eid_al_fitr: ClassVar[bool]
    length_eid_al_fitr: int
    eid_al_fitr_label: ClassVar[str]
    include_eid_al_adha: ClassVar[bool]
    eid_al_adha_label: ClassVar[str]
    length_eid_al_adha: int
    include_day_of_sacrifice: ClassVar[bool]
    day_of_sacrifice_label: ClassVar[str]
    include_islamic_new_year: ClassVar[bool]
    include_laylat_al_qadr: ClassVar[bool]
    include_nuzul_al_quran: ClassVar[bool]
    def get_islamic_holidays(self): ...

class CoreCalendar:
    FIXED_HOLIDAYS: Incomplete
    WEEKEND_DAYS: Incomplete
    def __init__(self) -> None: ...
    def name(cls): ...
    def get_fixed_holidays(self, year): ...
    def get_variable_days(self, year): ...
    def get_calendar_holidays(self, year): ...
    def holidays(self, year: Incomplete | None = None): ...
    def get_holiday_label(self, day): ...
    def holidays_set(self, year: Incomplete | None = None): ...
    def get_weekend_days(self): ...
    def is_working_day(self, day, extra_working_days: Incomplete | None = None, extra_holidays: Incomplete | None = None): ...
    def is_holiday(self, day, extra_holidays: Incomplete | None = None): ...
    def add_working_days(
        self,
        day,
        delta,
        extra_working_days: Incomplete | None = None,
        extra_holidays: Incomplete | None = None,
        keep_datetime: bool = False,
    ): ...
    def sub_working_days(
        self,
        day,
        delta,
        extra_working_days: Incomplete | None = None,
        extra_holidays: Incomplete | None = None,
        keep_datetime: bool = False,
    ): ...
    def find_following_working_day(self, day): ...
    @staticmethod
    def get_nth_weekday_in_month(year, month, weekday, n: int = 1, start: Incomplete | None = None): ...
    @staticmethod
    def get_last_weekday_in_month(year, month, weekday): ...
    @staticmethod
    def get_iso_week_date(year, week_nb, weekday=1): ...
    @staticmethod
    def get_first_weekday_after(day, weekday): ...
    def get_working_days_delta(
        self,
        start,
        end,
        include_start: bool = False,
        extra_working_days: Incomplete | None = None,
        extra_holidays: Incomplete | None = None,
    ): ...
    def export_to_ical(self, period=[2000, 2030], target_path: Incomplete | None = None): ...

class Calendar(CoreCalendar):
    include_new_years_day: ClassVar[bool]
    include_new_years_eve: ClassVar[bool]
    shift_new_years_day: ClassVar[bool]
    include_labour_day: ClassVar[bool]
    labour_day_label: ClassVar[str]
    def __init__(self, **kwargs) -> None: ...
    def get_fixed_holidays(self, year): ...
    def get_variable_days(self, year): ...

class WesternCalendar(WesternMixin, Calendar): ...
class OrthodoxCalendar(OrthodoxMixin, Calendar): ...

class ChineseNewYearCalendar(ChineseNewYearMixin, Calendar):
    WEEKEND_DAYS: Incomplete

class IslamicCalendar(IslamicMixin, Calendar): ...

class IslamoWesternCalendar(IslamicMixin, WesternMixin, Calendar):
    FIXED_HOLIDAYS: Incomplete
