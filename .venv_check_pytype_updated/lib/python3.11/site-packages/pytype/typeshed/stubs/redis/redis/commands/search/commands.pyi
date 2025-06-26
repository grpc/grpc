from _typeshed import Incomplete
from collections.abc import Mapping
from typing import Any, Literal
from typing_extensions import TypeAlias

from .aggregation import AggregateRequest, AggregateResult, Cursor
from .query import Query
from .result import Result

_QueryParams: TypeAlias = Mapping[str, str | float]

NUMERIC: Literal["NUMERIC"]

CREATE_CMD: Literal["FT.CREATE"]
ALTER_CMD: Literal["FT.ALTER"]
SEARCH_CMD: Literal["FT.SEARCH"]
ADD_CMD: Literal["FT.ADD"]
ADDHASH_CMD: Literal["FT.ADDHASH"]
DROP_CMD: Literal["FT.DROP"]
EXPLAIN_CMD: Literal["FT.EXPLAIN"]
EXPLAINCLI_CMD: Literal["FT.EXPLAINCLI"]
DEL_CMD: Literal["FT.DEL"]
AGGREGATE_CMD: Literal["FT.AGGREGATE"]
PROFILE_CMD: Literal["FT.PROFILE"]
CURSOR_CMD: Literal["FT.CURSOR"]
SPELLCHECK_CMD: Literal["FT.SPELLCHECK"]
DICT_ADD_CMD: Literal["FT.DICTADD"]
DICT_DEL_CMD: Literal["FT.DICTDEL"]
DICT_DUMP_CMD: Literal["FT.DICTDUMP"]
GET_CMD: Literal["FT.GET"]
MGET_CMD: Literal["FT.MGET"]
CONFIG_CMD: Literal["FT.CONFIG"]
TAGVALS_CMD: Literal["FT.TAGVALS"]
ALIAS_ADD_CMD: Literal["FT.ALIASADD"]
ALIAS_UPDATE_CMD: Literal["FT.ALIASUPDATE"]
ALIAS_DEL_CMD: Literal["FT.ALIASDEL"]
INFO_CMD: Literal["FT.INFO"]
SUGADD_COMMAND: Literal["FT.SUGADD"]
SUGDEL_COMMAND: Literal["FT.SUGDEL"]
SUGLEN_COMMAND: Literal["FT.SUGLEN"]
SUGGET_COMMAND: Literal["FT.SUGGET"]
SYNUPDATE_CMD: Literal["FT.SYNUPDATE"]
SYNDUMP_CMD: Literal["FT.SYNDUMP"]

NOOFFSETS: Literal["NOOFFSETS"]
NOFIELDS: Literal["NOFIELDS"]
STOPWORDS: Literal["STOPWORDS"]
WITHSCORES: Literal["WITHSCORES"]
FUZZY: Literal["FUZZY"]
WITHPAYLOADS: Literal["WITHPAYLOADS"]

class SearchCommands:
    def batch_indexer(self, chunk_size: int = 100): ...
    def create_index(
        self,
        fields,
        no_term_offsets: bool = False,
        no_field_flags: bool = False,
        stopwords: Incomplete | None = None,
        definition: Incomplete | None = None,
        max_text_fields: bool = False,  # added in 4.1.1
        temporary: Incomplete | None = None,  # added in 4.1.1
        no_highlight: bool = False,  # added in 4.1.1
        no_term_frequencies: bool = False,  # added in 4.1.1
        skip_initial_scan: bool = False,  # added in 4.1.1
    ): ...
    def alter_schema_add(self, fields): ...
    def dropindex(self, delete_documents: bool = False): ...
    def add_document(
        self,
        doc_id,
        nosave: bool = False,
        score: float = 1.0,
        payload: Incomplete | None = None,
        replace: bool = False,
        partial: bool = False,
        language: Incomplete | None = None,
        no_create: bool = False,
        **fields,
    ): ...
    def add_document_hash(self, doc_id, score: float = 1.0, language: Incomplete | None = None, replace: bool = False): ...
    def delete_document(self, doc_id, conn: Incomplete | None = None, delete_actual_document: bool = False): ...
    def load_document(self, id): ...
    def get(self, *ids): ...
    def info(self): ...
    def get_params_args(self, query_params: _QueryParams) -> list[Any]: ...
    def search(self, query: str | Query, query_params: _QueryParams | None = None) -> Result: ...
    def explain(self, query: str | Query, query_params: _QueryParams | None = None): ...
    def explain_cli(self, query): ...
    def aggregate(self, query: AggregateRequest | Cursor, query_params: _QueryParams | None = None) -> AggregateResult: ...
    def profile(
        self, query: str | Query | AggregateRequest, limited: bool = False, query_params: Mapping[str, str | float] | None = None
    ) -> tuple[Incomplete, Incomplete]: ...
    def spellcheck(
        self, query, distance: Incomplete | None = None, include: Incomplete | None = None, exclude: Incomplete | None = None
    ): ...
    def dict_add(self, name, *terms): ...
    def dict_del(self, name, *terms): ...
    def dict_dump(self, name): ...
    def config_set(self, option: str, value: str) -> bool: ...
    def config_get(self, option: str) -> dict[str, str]: ...
    def tagvals(self, tagfield): ...
    def aliasadd(self, alias): ...
    def aliasupdate(self, alias): ...
    def aliasdel(self, alias): ...
    def sugadd(self, key, *suggestions, **kwargs): ...
    def suglen(self, key): ...
    def sugdel(self, key, string): ...
    def sugget(self, key, prefix, fuzzy: bool = False, num: int = 10, with_scores: bool = False, with_payloads: bool = False): ...
    def synupdate(self, groupid, skipinitial: bool = False, *terms): ...
    def syndump(self): ...
