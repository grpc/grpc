from typing import ClassVar

class VictoriaDayMixin:
    include_victoria_day: ClassVar[bool]
    victoria_day_label: ClassVar[str]

class VictoriaDayFourthMondayMay(VictoriaDayMixin):
    def get_victoria_day(self, year): ...

class VictoriaDayLastMondayMay(VictoriaDayMixin):
    def get_victoria_day(self, year): ...

class VictoriaDayFirstMondayJune(VictoriaDayMixin):
    def get_victoria_day(self, year): ...
