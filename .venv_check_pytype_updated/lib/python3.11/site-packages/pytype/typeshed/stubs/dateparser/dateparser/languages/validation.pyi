from typing import Any

class LanguageValidator:
    logger: Any
    VALID_KEYS: Any
    @classmethod
    def get_logger(cls): ...
    @classmethod
    def validate_info(cls, language_id, info): ...
