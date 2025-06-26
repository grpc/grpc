class MissingPivotFunction(UserWarning):
    @staticmethod
    def print_warning(query: str): ...

class CloudOnlyWarning(UserWarning):
    @staticmethod
    def print_warning(api_name: str, doc_url: str): ...
