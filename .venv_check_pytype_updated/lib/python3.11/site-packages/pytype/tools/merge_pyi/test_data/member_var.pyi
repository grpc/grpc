class C(object):
    y = ...  # type: Union[complex, float, int, long]

    def __init__(self, x: Union[complex, float, int, long]) -> None: ...
