from _typeshed import Incomplete

class ApiClientAsync:
    PRIMITIVE_TYPES: Incomplete
    NATIVE_TYPES_MAPPING: Incomplete
    configuration: Incomplete
    pool_threads: Incomplete
    rest_client: Incomplete
    default_headers: Incomplete
    cookie: Incomplete
    def __init__(
        self,
        configuration: Incomplete | None = None,
        header_name: Incomplete | None = None,
        header_value: Incomplete | None = None,
        cookie: Incomplete | None = None,
        pool_threads: Incomplete | None = None,
        **kwargs,
    ) -> None: ...
    async def close(self) -> None: ...
    @property
    def pool(self): ...
    @property
    def user_agent(self): ...
    @user_agent.setter
    def user_agent(self, value) -> None: ...
    def set_default_header(self, header_name, header_value) -> None: ...
    def sanitize_for_serialization(self, obj): ...
    def deserialize(self, response, response_type): ...
    def call_api(
        self,
        resource_path,
        method,
        path_params: Incomplete | None = None,
        query_params: Incomplete | None = None,
        header_params: Incomplete | None = None,
        body: Incomplete | None = None,
        post_params: Incomplete | None = None,
        files: Incomplete | None = None,
        response_type: Incomplete | None = None,
        auth_settings: Incomplete | None = None,
        async_req: Incomplete | None = None,
        _return_http_data_only: Incomplete | None = None,
        collection_formats: Incomplete | None = None,
        _preload_content: bool = True,
        _request_timeout: Incomplete | None = None,
        urlopen_kw: Incomplete | None = None,
    ): ...
    def request(
        self,
        method,
        url,
        query_params: Incomplete | None = None,
        headers: Incomplete | None = None,
        post_params: Incomplete | None = None,
        body: Incomplete | None = None,
        _preload_content: bool = True,
        _request_timeout: Incomplete | None = None,
        **urlopen_kw,
    ): ...
    def parameters_to_tuples(self, params, collection_formats): ...
    def prepare_post_parameters(self, post_params: Incomplete | None = None, files: Incomplete | None = None): ...
    def select_header_accept(self, accepts): ...
    def select_header_content_type(self, content_types): ...
    def update_params_for_auth(self, headers, querys, auth_settings) -> None: ...
