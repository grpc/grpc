from _typeshed import Incomplete
from collections.abc import Callable
from typing import Any, overload

class Key:
    DefaultContentType: str
    RestoreBody: str
    BufferSize: Any
    base_user_settable_fields: Any
    base_fields: Any
    bucket: Any
    name: str
    metadata: Any
    cache_control: Any
    content_type: Any
    content_encoding: Any
    content_disposition: Any
    content_language: Any
    filename: Any
    etag: Any
    is_latest: bool
    last_modified: Any
    owner: Any
    path: Any
    resp: Any
    mode: Any
    size: Any
    version_id: Any
    source_version_id: Any
    delete_marker: bool
    encrypted: Any
    ongoing_restore: Any
    expiry_date: Any
    local_hashes: Any
    def __init__(self, bucket: Incomplete | None = None, name: Incomplete | None = None) -> None: ...
    def __iter__(self): ...
    @property
    def provider(self): ...
    key: Any
    md5: Any
    base64md5: Any
    storage_class: Any
    def get_md5_from_hexdigest(self, md5_hexdigest): ...
    def handle_encryption_headers(self, resp): ...
    def handle_version_headers(self, resp, force: bool = False): ...
    def handle_restore_headers(self, response): ...
    def handle_addl_headers(self, headers): ...
    def open_read(
        self,
        headers: dict[str, str] | None = None,
        query_args: str = "",
        override_num_retries: Incomplete | None = None,
        response_headers: dict[str, str] | None = None,
    ): ...
    def open_write(self, headers: dict[str, str] | None = None, override_num_retries: Incomplete | None = None): ...
    def open(
        self,
        mode: str = "r",
        headers: dict[str, str] | None = None,
        query_args: Incomplete | None = None,
        override_num_retries: Incomplete | None = None,
    ): ...
    closed: bool
    def close(self, fast: bool = False): ...
    def next(self): ...
    __next__: Any
    def read(self, size: int = 0): ...
    def change_storage_class(self, new_storage_class, dst_bucket: Incomplete | None = None, validate_dst_bucket: bool = True): ...
    def copy(
        self,
        dst_bucket,
        dst_key,
        metadata: Incomplete | None = None,
        reduced_redundancy: bool = False,
        preserve_acl: bool = False,
        encrypt_key: bool = False,
        validate_dst_bucket: bool = True,
    ): ...
    def startElement(self, name, attrs, connection): ...
    def endElement(self, name, value, connection): ...
    def exists(self, headers: dict[str, str] | None = None): ...
    def delete(self, headers: dict[str, str] | None = None): ...
    def get_metadata(self, name): ...
    def set_metadata(self, name, value): ...
    def update_metadata(self, d): ...
    def set_acl(self, acl_str, headers: dict[str, str] | None = None): ...
    def get_acl(self, headers: dict[str, str] | None = None): ...
    def get_xml_acl(self, headers: dict[str, str] | None = None): ...
    def set_xml_acl(self, acl_str, headers: dict[str, str] | None = None): ...
    def set_canned_acl(self, acl_str, headers: dict[str, str] | None = None): ...
    def get_redirect(self): ...
    def set_redirect(self, redirect_location, headers: dict[str, str] | None = None): ...
    def make_public(self, headers: dict[str, str] | None = None): ...
    def generate_url(
        self,
        expires_in,
        method: str = "GET",
        headers: dict[str, str] | None = None,
        query_auth: bool = True,
        force_http: bool = False,
        response_headers: dict[str, str] | None = None,
        expires_in_absolute: bool = False,
        version_id: Incomplete | None = None,
        policy: Incomplete | None = None,
        reduced_redundancy: bool = False,
        encrypt_key: bool = False,
    ): ...
    def send_file(
        self,
        fp,
        headers: dict[str, str] | None = None,
        cb: Callable[[int, int], object] | None = None,
        num_cb: int = 10,
        query_args: Incomplete | None = None,
        chunked_transfer: bool = False,
        size: Incomplete | None = None,
    ): ...
    def should_retry(self, response, chunked_transfer: bool = False): ...
    def compute_md5(self, fp, size: Incomplete | None = None): ...
    def set_contents_from_stream(
        self,
        fp,
        headers: dict[str, str] | None = None,
        replace: bool = True,
        cb: Callable[[int, int], object] | None = None,
        num_cb: int = 10,
        policy: Incomplete | None = None,
        reduced_redundancy: bool = False,
        query_args: Incomplete | None = None,
        size: Incomplete | None = None,
    ): ...
    def set_contents_from_file(
        self,
        fp,
        headers: dict[str, str] | None = None,
        replace: bool = True,
        cb: Callable[[int, int], object] | None = None,
        num_cb: int = 10,
        policy: Incomplete | None = None,
        md5: Incomplete | None = None,
        reduced_redundancy: bool = False,
        query_args: Incomplete | None = None,
        encrypt_key: bool = False,
        size: Incomplete | None = None,
        rewind: bool = False,
    ): ...
    def set_contents_from_filename(
        self,
        filename,
        headers: dict[str, str] | None = None,
        replace: bool = True,
        cb: Callable[[int, int], object] | None = None,
        num_cb: int = 10,
        policy: Incomplete | None = None,
        md5: Incomplete | None = None,
        reduced_redundancy: bool = False,
        encrypt_key: bool = False,
    ): ...
    def set_contents_from_string(
        self,
        string_data: str | bytes,
        headers: dict[str, str] | None = None,
        replace: bool = True,
        cb: Callable[[int, int], object] | None = None,
        num_cb: int = 10,
        policy: Incomplete | None = None,
        md5: Incomplete | None = None,
        reduced_redundancy: bool = False,
        encrypt_key: bool = False,
    ) -> None: ...
    def get_file(
        self,
        fp,
        headers: dict[str, str] | None = None,
        cb: Callable[[int, int], object] | None = None,
        num_cb: int = 10,
        torrent: bool = False,
        version_id: Incomplete | None = None,
        override_num_retries: Incomplete | None = None,
        response_headers: dict[str, str] | None = None,
    ): ...
    def get_torrent_file(
        self, fp, headers: dict[str, str] | None = None, cb: Callable[[int, int], object] | None = None, num_cb: int = 10
    ): ...
    def get_contents_to_file(
        self,
        fp,
        headers: dict[str, str] | None = None,
        cb: Callable[[int, int], object] | None = None,
        num_cb: int = 10,
        torrent: bool = False,
        version_id: Incomplete | None = None,
        res_download_handler: Incomplete | None = None,
        response_headers: dict[str, str] | None = None,
    ): ...
    def get_contents_to_filename(
        self,
        filename,
        headers: dict[str, str] | None = None,
        cb: Callable[[int, int], object] | None = None,
        num_cb: int = 10,
        torrent: bool = False,
        version_id: Incomplete | None = None,
        res_download_handler: Incomplete | None = None,
        response_headers: dict[str, str] | None = None,
    ): ...
    @overload
    def get_contents_as_string(
        self,
        headers: dict[str, str] | None = None,
        cb: Callable[[int, int], object] | None = None,
        num_cb: int = 10,
        torrent: bool = False,
        version_id: Incomplete | None = None,
        response_headers: dict[str, str] | None = None,
        encoding: None = None,
    ) -> bytes: ...
    @overload
    def get_contents_as_string(
        self,
        headers: dict[str, str] | None = None,
        cb: Callable[[int, int], object] | None = None,
        num_cb: int = 10,
        torrent: bool = False,
        version_id: Incomplete | None = None,
        response_headers: dict[str, str] | None = None,
        *,
        encoding: str,
    ) -> str: ...
    def add_email_grant(self, permission, email_address, headers: dict[str, str] | None = None): ...
    def add_user_grant(
        self, permission, user_id, headers: dict[str, str] | None = None, display_name: Incomplete | None = None
    ): ...
    def set_remote_metadata(self, metadata_plus, metadata_minus, preserve_acl, headers: dict[str, str] | None = None): ...
    def restore(self, days, headers: dict[str, str] | None = None): ...
