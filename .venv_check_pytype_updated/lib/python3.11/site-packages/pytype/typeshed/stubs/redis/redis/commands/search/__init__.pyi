from _typeshed import Incomplete

from .commands import SearchCommands

class Search(SearchCommands):
    class BatchIndexer:
        def __init__(self, client, chunk_size: int = 1000) -> None: ...
        def add_document(
            self,
            doc_id,
            nosave: bool = False,
            score: float = 1.0,
            payload: Incomplete | None = None,
            replace: bool = False,
            partial: bool = False,
            no_create: bool = False,
            **fields,
        ): ...
        def add_document_hash(self, doc_id, score: float = 1.0, replace: bool = False): ...
        def commit(self): ...

    def __init__(self, client, index_name: str = "idx") -> None: ...
