#!/usr/bin/env python3
# Copyright 2026 The gRPC Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Backfills the XSLT polyfill script tag into archived build index.xml files.

Iterates through YYYY/MM partitions in chronological order (either in a local
archive mirror or directly in GCS), validates each build-root index.xml,
inserts the XHTML <script> element for /web-assets/xslt-polyfill.min.js if not
already present, verifies the resulting XML, and writes or uploads it safely.

Prerequisites:
  Requires the `google-cloud-storage` package:
    pip install google-cloud-storage

Examples:
  1. Local archive mirror testing (never touches GCS):
     # Dry-run across the entire local mirror:
     python3 tools/package_hosting/backfill/backfill_xslt_polyfill.py \
       --local-dir="/path/to/local/archive" \
       --dry-run

     # Dry-run a single month on the local mirror with per-file debug logs:
     python3 tools/package_hosting/backfill/backfill_xslt_polyfill.py \
       --local-dir="/path/to/local/archive" \
       --only="2018/07" \
       --dry-run \
       --verbose

     # Modify local mirror files in-place for a single month (no GCS upload):
     python3 tools/package_hosting/backfill/backfill_xslt_polyfill.py \
       --local-dir="/path/to/local/archive" \
       --only="2018/07"

     # Modify local mirror files across a range of months (no GCS upload):
     python3 tools/package_hosting/backfill/backfill_xslt_polyfill.py \
       --local-dir="/path/to/local/archive" \
       --start-from="2018/08" \
       --end-at="2018/12"

  2. Read-only GCS dry-runs (downloads and validates in memory; NO uploads):
     # Dry-run a single month on GCS:
     python3 tools/package_hosting/backfill/backfill_xslt_polyfill.py \
       --only="2026/11" \
       --dry-run \
       --verbose

     # Dry-run a range of months on GCS:
     python3 tools/package_hosting/backfill/backfill_xslt_polyfill.py \
       --start-from="2018/07" \
       --end-at="2018/12" \
       --dry-run

  3. LIVE GCS uploads (MODIFIES AND UPLOADS index.xml objects in GCS):
     # Upload backfilled index.xml files for a single month in GCS
     # (prompts [yN] before uploading):
     python3 tools/package_hosting/backfill/backfill_xslt_polyfill.py \
       --only="2018/07"

     # Upload backfilled index.xml files for a range of months in GCS:
     python3 tools/package_hosting/backfill/backfill_xslt_polyfill.py \
       --start-from="2018/07" \
       --end-at="2018/12"

     # Resume a live GCS upload from a specific YYYY/MM onward:
     python3 tools/package_hosting/backfill/backfill_xslt_polyfill.py \
       --start-from="2019/01"
"""

import argparse
from collections.abc import Callable, Generator, Iterator, Sequence
from concurrent import futures
import contextlib
import dataclasses
import enum
import logging
import os
import pathlib
import re
import shutil
import sys
import tempfile
import textwrap
import time
from types import TracebackType
from typing import Final, Protocol, Self, cast
from xml.etree import ElementTree as ET

from google.api_core import exceptions as gcs_exceptions
from google.auth import credentials as auth_credentials
from google.auth import exceptions as auth_exceptions
from google.auth.transport import requests as auth_requests
from google.cloud import storage  # pyright: ignore[reportMissingTypeStubs]
import google.cloud.storage.exceptions  # pyright: ignore[reportMissingTypeStubs]
import requests
from requests import adapters
from requests import exceptions as requests_exceptions

_LOGGER: Final = logging.getLogger(__name__)

DEFAULT_BUCKET: Final = "packages.grpc.io"
DEFAULT_PREFIX: Final = "archive"
DEFAULT_WORKERS: Final = 16
INDEX_FILENAME: Final = "index.xml"
XML_CONTENT_TYPE: Final = "application/xml"
# Cap well below google-cloud-storage's 8 MiB multipart upload threshold so
# uploads are always single-request atomic multipart operations.
MAX_INDEX_XML_BYTES: Final = 4 * 1024 * 1024

XHTML_NAMESPACE: Final = "http://www.w3.org/1999/xhtml"
XHTML_SCRIPT_TAG: Final = f"{{{XHTML_NAMESPACE}}}script"
POLYFILL_SRC: Final = "/web-assets/xslt-polyfill.min.js"
POLYFILL_LINE: Final = (
    f'  <script src="{POLYFILL_SRC}" xmlns="{XHTML_NAMESPACE}"></script>'
)

_YEAR_RE: Final = re.compile(r"^[0-9]{4}$")
_MONTH_RE: Final = re.compile(r"^(0[1-9]|1[0-2])$")
_BUILD_OPEN_TAG_RE: Final = re.compile(
    r"^(<build\b[^>]*(?<!/)>)(\r?\n)",
    re.MULTILINE,
)
_STYLESHEET_PI_RE: Final = re.compile(
    r"<\?xml-stylesheet\b[^>]*href=[\"']/web-assets/build-201807\.xsl[\"'][^>]*\?>",
)
_DTD_RE: Final = re.compile(r"<!(DOCTYPE|ENTITY)\b", re.IGNORECASE)

_GCS_OPERATION_ERRORS: Final = (
    gcs_exceptions.GoogleAPIError,
    google.cloud.storage.exceptions.DataCorruption,
    google.cloud.storage.exceptions.InvalidResponse,
    auth_exceptions.GoogleAuthError,
    requests_exceptions.RequestException,
)


class Outcome(enum.Enum):
    """Result classification for processing a single build index.xml."""

    PATCHED = "patched"
    ALREADY_PATCHED = "already_patched"
    MISSING = "missing"
    FAILED = "failed"


@dataclasses.dataclass(frozen=True, order=True)
class YearMonth:
    """Represents a YYYY/MM archive partition."""

    year: int
    month: int

    def __str__(self) -> str:
        return f"{self.year:04d}/{self.month:02d}"


@dataclasses.dataclass(frozen=True)
class PartitionBounds:
    """Inclusive lower and upper YYYY/MM bounds."""

    start_from: YearMonth | None
    end_at: YearMonth | None


@dataclasses.dataclass(frozen=True)
class ItemResult:
    """Outcome details for a single build index.xml target."""

    target: str
    outcome: Outcome
    error: str | None = None


@dataclasses.dataclass
class Stats:
    """Mutable counters tracking progress across partitions."""

    patched: int = 0
    already_patched: int = 0
    missing: int = 0
    failed: int = 0

    def record(self, outcome: Outcome) -> None:
        """Increments the counter matching outcome."""
        if outcome is Outcome.PATCHED:
            self.patched += 1
        elif outcome is Outcome.ALREADY_PATCHED:
            self.already_patched += 1
        elif outcome is Outcome.MISSING:
            self.missing += 1
        elif outcome is Outcome.FAILED:
            self.failed += 1

    def add(self, other: "Stats") -> None:
        """Accumulates counts from another Stats instance."""
        self.patched += other.patched
        self.already_patched += other.already_patched
        self.missing += other.missing
        self.failed += other.failed

    @property
    def total(self) -> int:
        """Total items processed."""
        return self.patched + self.already_patched + self.missing + self.failed


def parse_year_month_bound(value: str, *, is_end: bool) -> YearMonth:
    """Parses a 'YYYY' or 'YYYY/MM' CLI boundary into a YearMonth.

    Args:
      value: Boundary string in 'YYYY' or 'YYYY/MM' format.
      is_end: When True and only 'YYYY' is given, defaults month to 12;
        otherwise defaults month to 1.

    Returns:
      Parsed YearMonth boundary.

    Raises:
      ValueError: If value does not match 'YYYY' or 'YYYY/MM'.
    """
    cleaned = value.strip().strip("/")
    parts = cleaned.split("/")
    if len(parts) == 1 and _YEAR_RE.match(parts[0]):
        return YearMonth(year=int(parts[0]), month=12 if is_end else 1)
    if (
        len(parts) == 2
        and _YEAR_RE.match(parts[0])
        and _MONTH_RE.match(parts[1])
    ):
        return YearMonth(year=int(parts[0]), month=int(parts[1]))
    msg = (
        f"Invalid year/month '{value}': expected 'YYYY' or 'YYYY/MM' "
        "(with MM in 01..12)."
    )
    raise ValueError(msg)


def _parse_xml_root(text: str, source_label: str) -> ET.Element:
    """Parses XML text after rejecting DTD/entity declarations."""
    if _DTD_RE.search(text):
        msg = (
            f"{source_label}: unexpected DOCTYPE/ENTITY declaration "
            "(not a build XML manifest)"
        )
        raise ValueError(msg)
    try:
        # Trusted gRPC build manifests with DTD/ENTITY declarations rejected.
        return ET.fromstring(text)  # noqa: S314
    except ET.ParseError as exc:
        msg = f"{source_label}: invalid XML: {exc}"
        raise ValueError(msg) from exc


def patch_build_index_xml(raw_bytes: bytes, source_label: str) -> bytes | None:
    """Validates and inserts the XSLT polyfill script into a build index.xml.

    Args:
      raw_bytes: Original UTF-8 XML bytes.
      source_label: Identifier used in error messages.

    Returns:
      Patched UTF-8 XML bytes if modified, or None if already patched.

    Raises:
      ValueError: If the document is not a valid build index.xml, exceeds the
        expected manifest size limit, or has an unexpected structure.
    """
    if len(raw_bytes) > MAX_INDEX_XML_BYTES:
        msg = (
            f"{source_label}: size {len(raw_bytes)} bytes exceeds limit "
            f"({MAX_INDEX_XML_BYTES} bytes)"
        )
        raise ValueError(msg)

    try:
        text = raw_bytes.decode("utf-8")
    except UnicodeDecodeError as exc:
        msg = f"{source_label}: invalid UTF-8 encoding: {exc}"
        raise ValueError(msg) from exc

    root = _parse_xml_root(text, source_label)
    if root.tag != "build":
        msg = (
            f"{source_label}: unexpected root element {root.tag!r} "
            "(expected 'build')"
        )
        raise ValueError(msg)

    polyfill_scripts = [
        child
        for child in root
        if child.tag == XHTML_SCRIPT_TAG and child.get("src") == POLYFILL_SRC
    ]
    any_scripts = [
        child for child in root if child.tag in (XHTML_SCRIPT_TAG, "script")
    ]
    if polyfill_scripts:
        if len(any_scripts) > 1:
            msg = (
                f"{source_label}: found multiple <script> elements "
                f"({len(any_scripts)}) under <build>"
            )
            raise ValueError(msg)
        return None

    if any_scripts or POLYFILL_SRC in text:
        msg = (
            f"{source_label}: found unexpected existing script reference "
            "under <build>"
        )
        raise ValueError(msg)

    matches = list(_BUILD_OPEN_TAG_RE.finditer(text))
    if len(matches) != 1:
        msg = (
            f"{source_label}: expected exactly 1 non-empty opening "
            f"<build ...> line, found {len(matches)}"
        )
        raise ValueError(msg)

    match = matches[0]
    if not _STYLESHEET_PI_RE.search(text, 0, match.start()):
        msg = (
            f"{source_label}: missing expected <?xml-stylesheet?> prolog for "
            "/web-assets/build-201807.xsl"
        )
        raise ValueError(msg)

    newline = match.group(2)
    insert_pos = match.end()
    patched_text = (
        f"{text[:insert_pos]}{POLYFILL_LINE}{newline}{text[insert_pos:]}"
    )

    patched_root = _parse_xml_root(patched_text, f"{source_label} (patched)")
    if patched_root.tag != "build" or len(patched_root) != len(root) + 1:
        msg = f"{source_label}: patched XML child count mismatch"
        raise ValueError(msg)

    first_child = patched_root[0]
    if (
        first_child.tag != XHTML_SCRIPT_TAG
        or first_child.get("src") != POLYFILL_SRC
    ):
        msg = f"{source_label}: patched XML missing expected first <script>"
        raise ValueError(msg)

    patched_bytes = patched_text.encode("utf-8")
    if len(patched_bytes) > MAX_INDEX_XML_BYTES:
        msg = (
            f"{source_label}: patched size {len(patched_bytes)} bytes exceeds "
            f"limit ({MAX_INDEX_XML_BYTES} bytes)"
        )
        raise ValueError(msg)
    return patched_bytes


class LocalArchiveBackend:
    """Operates on a local archive directory tree (archive/YYYY/MM/<build>/)."""

    def __init__(self, archive_dir: pathlib.Path, *, dry_run: bool) -> None:
        self._archive_dir = archive_dir.resolve()
        self._dry_run = dry_run

    def __enter__(self) -> Self:
        return self

    def __exit__(
        self,
        exc_type: type[BaseException] | None,
        exc_val: BaseException | None,
        exc_tb: TracebackType | None,
    ) -> None:
        return None

    @property
    def is_remote_gcs(self) -> bool:
        """Whether this backend uploads to remote GCS."""
        return False

    @property
    def description(self) -> str:
        """Human-readable target description."""
        return str(self._archive_dir)

    def list_year_months(self, bounds: PartitionBounds) -> list[YearMonth]:
        """Returns sorted YYYY/MM partitions in archive_dir matching bounds."""
        partitions: list[YearMonth] = []
        for year_dir in sorted(self._archive_dir.iterdir()):
            if not year_dir.is_dir() or not _YEAR_RE.match(year_dir.name):
                continue
            year = int(year_dir.name)
            if bounds.start_from is not None and year < bounds.start_from.year:
                continue
            if bounds.end_at is not None and year > bounds.end_at.year:
                continue
            for month_dir in sorted(year_dir.iterdir()):
                if not month_dir.is_dir() or not _MONTH_RE.match(
                    month_dir.name
                ):
                    continue
                partitions.append(
                    YearMonth(
                        year=year,
                        month=int(month_dir.name),
                    )
                )
        return filter_partitions(sorted(partitions), bounds)

    def list_targets(self, ym: YearMonth) -> list[str]:
        """Returns sorted build-root index.xml paths for a YYYY/MM partition."""
        month_dir = self._archive_dir / f"{ym.year:04d}" / f"{ym.month:02d}"
        if not month_dir.is_dir():
            return []
        return [
            str(child / INDEX_FILENAME)
            for child in sorted(month_dir.iterdir())
            if child.is_dir()
        ]

    def process_target(self, target: str) -> ItemResult:
        """Reads, validates, patches, and atomically writes one local file."""
        file_path = pathlib.Path(target)
        try:
            if not file_path.is_file():
                _LOGGER.warning("Missing build index: %s", file_path)
                return ItemResult(target=target, outcome=Outcome.MISSING)

            raw_bytes = file_path.read_bytes()
            patched_bytes = patch_build_index_xml(raw_bytes, str(file_path))
            if patched_bytes is None:
                return ItemResult(
                    target=target, outcome=Outcome.ALREADY_PATCHED
                )

            if not self._dry_run:
                _atomic_write_file(file_path, patched_bytes)
            return ItemResult(target=target, outcome=Outcome.PATCHED)
        except (OSError, ValueError) as exc:
            _LOGGER.error("Failed processing %s: %s", file_path, exc)
            return ItemResult(
                target=target,
                outcome=Outcome.FAILED,
                error=str(exc),
            )


def _atomic_write_file(target_path: pathlib.Path, data: bytes) -> None:
    """Writes data to a sibling temp file and atomically replaces target."""
    tmp_path: pathlib.Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="wb",
            prefix=f".{target_path.name}.",
            suffix=".tmp",
            dir=target_path.parent,
            delete=False,
        ) as handle:
            tmp_path = pathlib.Path(handle.name)
            handle.write(data)
            handle.flush()
            os.fsync(handle.fileno())
        shutil.copymode(target_path, tmp_path)
        tmp_path.replace(target_path)
    except BaseException:
        if tmp_path is not None:
            try:
                tmp_path.unlink(missing_ok=True)
            except OSError:
                _LOGGER.warning("Failed to remove temporary file %s", tmp_path)
        raise


class _GcsBlobProtocol(Protocol):
    """Typed subset of google.cloud.storage.Blob."""

    @property
    def generation(self) -> int | None: ...

    def download_as_bytes(self, *, checksum: str = "auto") -> bytes: ...

    def upload_from_string(
        self,
        data: bytes | str,
        content_type: str = "text/plain",
        *,
        if_generation_match: int | None = None,
        checksum: str = "auto",
    ) -> None: ...


class _GcsBucketProtocol(Protocol):
    """Typed subset of google.cloud.storage.Bucket."""

    def blob(self, blob_name: str) -> _GcsBlobProtocol: ...


class _GcsBlobIteratorProtocol(Iterator[object], Protocol):
    """Typed subset of google.api_core.page_iterator.HTTPIterator."""

    @property
    def prefixes(self) -> set[str]: ...


class _GcsClientProtocol(Protocol):
    """Typed subset of google.cloud.storage.Client."""

    def bucket(self, bucket_name: str) -> _GcsBucketProtocol: ...

    def list_blobs(
        self,
        bucket_or_name: str,
        *,
        prefix: str | None = None,
        delimiter: str | None = None,
    ) -> _GcsBlobIteratorProtocol: ...

    def close(self) -> None: ...


class _MtlsSessionProtocol(Protocol):
    """Typed subset of google.auth.transport.requests.AuthorizedSession."""

    @property
    def is_mtls(self) -> bool: ...

    def configure_mtls_channel(
        self,
        client_cert_callback: Callable[[], tuple[bytes, bytes]] | None = None,
    ) -> None: ...


@dataclasses.dataclass(frozen=True)
class _DownloadedBlob:
    """Downloaded GCS object payload paired with its generation number."""

    raw_bytes: bytes
    generation: int


def _create_gcs_client(*, pool_size: int) -> _GcsClientProtocol:
    """Creates a GCS Client with an HTTP pool sized for worker threads."""
    raw_client = storage.Client()
    creds = getattr(raw_client, "_credentials", None)
    if isinstance(creds, auth_credentials.Credentials):
        creds.with_non_blocking_refresh()

    session = getattr(raw_client, "_http", None)
    if isinstance(session, requests.Session):
        for scheme in ("https://", "http://"):
            old_adapter = session.adapters.get(scheme)
            new_adapter = adapters.HTTPAdapter(
                pool_connections=pool_size,
                pool_maxsize=pool_size,
            )
            session.mount(scheme, new_adapter)
            if old_adapter is not None and old_adapter is not new_adapter:
                old_adapter.close()
        if isinstance(session, auth_requests.AuthorizedSession):
            mtls_session = cast("_MtlsSessionProtocol", session)
            if mtls_session.is_mtls:
                cert_source = cast(
                    "Callable[[], tuple[bytes, bytes]] | None",
                    getattr(raw_client, "_client_cert_source", None),
                )
                mtls_session.configure_mtls_channel(cert_source)
    return cast("_GcsClientProtocol", raw_client)


class GcsArchiveBackend:
    """Operates directly on gs://<bucket>/<prefix>/YYYY/MM/<build>/index.xml."""

    def __init__(
        self,
        bucket_name: str,
        prefix: str,
        *,
        workers: int,
        dry_run: bool,
    ) -> None:
        self._bucket_name = bucket_name
        self._prefix = prefix.strip("/")
        self._dry_run = dry_run
        self._client = _create_gcs_client(pool_size=max(workers + 4, 16))
        self._bucket = self._client.bucket(bucket_name)

    def __enter__(self) -> Self:
        return self

    def __exit__(
        self,
        exc_type: type[BaseException] | None,
        exc_val: BaseException | None,
        exc_tb: TracebackType | None,
    ) -> None:
        self._client.close()

    @property
    def is_remote_gcs(self) -> bool:
        """Whether this backend uploads to remote GCS."""
        return True

    @property
    def description(self) -> str:
        """Human-readable target description."""
        if self._prefix:
            return f"gs://{self._bucket_name}/{self._prefix}/"
        return f"gs://{self._bucket_name}/"

    def list_year_months(self, bounds: PartitionBounds) -> list[YearMonth]:
        """Discovers sorted YYYY/MM partitions in GCS matching bounds."""
        root_prefix = f"{self._prefix}/" if self._prefix else ""
        year_iter = self._client.list_blobs(
            self._bucket_name,
            prefix=root_prefix,
            delimiter="/",
        )
        for _ in year_iter:
            pass

        partitions: list[YearMonth] = []
        for year_prefix in sorted(year_iter.prefixes):
            year_str = year_prefix.rstrip("/").rsplit("/", maxsplit=1)[-1]
            if not _YEAR_RE.match(year_str):
                continue
            year = int(year_str)
            if bounds.start_from is not None and year < bounds.start_from.year:
                continue
            if bounds.end_at is not None and year > bounds.end_at.year:
                continue
            month_iter = self._client.list_blobs(
                self._bucket_name,
                prefix=year_prefix,
                delimiter="/",
            )
            for _ in month_iter:
                pass
            for month_prefix in sorted(month_iter.prefixes):
                month_str = month_prefix.rstrip("/").rsplit("/", maxsplit=1)[-1]
                if not _MONTH_RE.match(month_str):
                    continue
                partitions.append(YearMonth(year=year, month=int(month_str)))
        return filter_partitions(sorted(partitions), bounds)

    def list_targets(self, ym: YearMonth) -> list[str]:
        """Lists build-root index.xml blob names under YYYY/MM via delimiter."""
        base = f"{self._prefix}/" if self._prefix else ""
        month_prefix = f"{base}{ym.year:04d}/{ym.month:02d}/"
        build_iter = self._client.list_blobs(
            self._bucket_name,
            prefix=month_prefix,
            delimiter="/",
        )
        for _ in build_iter:
            pass
        return [
            f"{build_prefix}{INDEX_FILENAME}"
            for build_prefix in sorted(build_iter.prefixes)
        ]

    def _download_target(
        self,
        target: str,
        uri: str,
    ) -> _DownloadedBlob | ItemResult:
        """Downloads target bytes and returns generation or failure result."""
        download_blob = self._bucket.blob(target)
        try:
            raw_bytes = download_blob.download_as_bytes(checksum="auto")
        except gcs_exceptions.NotFound:
            _LOGGER.warning("Missing build index object: %s", uri)
            return ItemResult(target=uri, outcome=Outcome.MISSING)
        except _GCS_OPERATION_ERRORS as exc:
            _LOGGER.error("Failed downloading %s: %s", uri, exc)
            return ItemResult(
                target=uri, outcome=Outcome.FAILED, error=str(exc)
            )

        generation = download_blob.generation
        if generation is None:
            msg = f"{uri}: missing object generation after download"
            _LOGGER.error("%s", msg)
            return ItemResult(target=uri, outcome=Outcome.FAILED, error=msg)
        return _DownloadedBlob(raw_bytes=raw_bytes, generation=generation)

    def process_target(self, target: str) -> ItemResult:
        """Downloads, validates, patches, and conditionally uploads one blob."""
        uri = f"gs://{self._bucket_name}/{target}"
        downloaded = self._download_target(target, uri)
        if isinstance(downloaded, ItemResult):
            return downloaded

        try:
            patched_bytes = patch_build_index_xml(downloaded.raw_bytes, uri)
            if patched_bytes is None:
                return ItemResult(target=uri, outcome=Outcome.ALREADY_PATCHED)
            if not self._dry_run:
                self._upload_patched(
                    target,
                    patched_bytes,
                    downloaded.generation,
                )
        except gcs_exceptions.PreconditionFailed as exc:
            return self._recover_precondition_failure(target, uri, exc)
        except (ValueError, *_GCS_OPERATION_ERRORS) as exc:
            _LOGGER.error("Failed updating %s: %s", uri, exc)
            return ItemResult(
                target=uri, outcome=Outcome.FAILED, error=str(exc)
            )

        return ItemResult(target=uri, outcome=Outcome.PATCHED)

    def _recover_precondition_failure(
        self,
        target: str,
        uri: str,
        original_exc: gcs_exceptions.PreconditionFailed,
    ) -> ItemResult:
        """Checks whether a 412 retry error occurred after our upload landed."""
        try:
            latest_bytes = self._bucket.blob(target).download_as_bytes(
                checksum="auto"
            )
            if patch_build_index_xml(latest_bytes, uri) is None:
                _LOGGER.warning(
                    "Upload retry for %s received 412 PreconditionFailed, "
                    "but remote object is already validly patched; "
                    "counting as uploaded.",
                    uri,
                )
                return ItemResult(target=uri, outcome=Outcome.PATCHED)
        except (
            gcs_exceptions.NotFound,
            ValueError,
            *_GCS_OPERATION_ERRORS,
        ) as verify_exc:
            _LOGGER.warning(
                "Post-412 verification failed for %s: %s", uri, verify_exc
            )
        _LOGGER.error("Failed updating %s: %s", uri, original_exc)
        return ItemResult(
            target=uri, outcome=Outcome.FAILED, error=str(original_exc)
        )

    def _upload_patched(
        self,
        target: str,
        patched_bytes: bytes,
        generation: int,
    ) -> None:
        """Uploads patched XML via a fresh Blob with generation precondition."""
        if len(patched_bytes) > MAX_INDEX_XML_BYTES:
            msg = (
                f"Refusing to upload {len(patched_bytes)} bytes "
                f"(exceeds {MAX_INDEX_XML_BYTES} byte limit)"
            )
            raise ValueError(msg)

        uri = f"gs://{self._bucket_name}/{target}"
        _LOGGER.info(
            "UPLOADING patched XML to %s (if_generation_match=%d)",
            uri,
            generation,
        )
        # Use a fresh Blob instance so HTTP download response headers (such as
        # Cache-Control: no-cache from ?alt=media) are not copied into stored
        # GCS object metadata.
        upload_blob = self._bucket.blob(target)
        upload_blob.upload_from_string(
            patched_bytes,
            content_type=XML_CONTENT_TYPE,
            if_generation_match=generation,
            checksum="auto",
        )


def filter_partitions(
    partitions: Sequence[YearMonth],
    bounds: PartitionBounds,
) -> list[YearMonth]:
    """Filters sorted YearMonth partitions to bounds."""
    selected: list[YearMonth] = []
    for ym in partitions:
        if bounds.start_from is not None and ym < bounds.start_from:
            continue
        if bounds.end_at is not None and ym > bounds.end_at:
            continue
        selected.append(ym)
    return selected


def _build_epilog() -> str:
    """Returns formatted CLI examples for --help output."""
    return textwrap.dedent(
        """\
        Examples:
          # 1. Local archive mirror (never touches GCS):
          %(prog)s --local-dir="/path/to/archive" --dry-run
          %(prog)s --local-dir="/path/to/archive" --only="2018/07" \\
            --dry-run --verbose
          %(prog)s --local-dir="/path/to/archive" --only="2018/07"
          %(prog)s --local-dir="/path/to/archive" \\
            --start-from="2018/08" --end-at="2018/12"

          # 2. Read-only GCS dry-run (validates in memory; NO uploads):
          %(prog)s --only="2026/11" --dry-run --verbose
          %(prog)s --start-from="2018/07" --end-at="2018/12" --dry-run

          # 3. LIVE GCS upload (UPLOADS modified index.xml; prompts [yN]):
          %(prog)s --only="2018/07"
          %(prog)s --start-from="2018/07" --end-at="2018/12"
          %(prog)s --start-from="2019/01"
        """
    )


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    """Parses command-line arguments."""
    parser = argparse.ArgumentParser(
        description=(
            "Backfill XSLT polyfill <script> tag into archived build "
            "index.xml manifests. When --local-dir and --dry-run are both "
            "omitted, modified index.xml files are UPLOADED to GCS."
        ),
        epilog=_build_epilog(),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    target_group = parser.add_mutually_exclusive_group()
    target_group.add_argument(
        "--local-dir",
        type=pathlib.Path,
        default=None,
        help=(
            "Operate on a local archive directory (e.g. ./archive) instead "
            "of GCS (never uploads to GCS)."
        ),
    )
    target_group.add_argument(
        "--bucket",
        default=DEFAULT_BUCKET,
        help=f"GCS bucket name (default: {DEFAULT_BUCKET}).",
    )
    parser.add_argument(
        "--prefix",
        default=None,
        help=(
            "Archive prefix inside the GCS bucket "
            f"(default: {DEFAULT_PREFIX})."
        ),
    )

    range_group = parser.add_mutually_exclusive_group()
    range_group.add_argument(
        "--only",
        default=None,
        metavar="YYYY[/MM]",
        help="Process only a single year ('YYYY') or month ('YYYY/MM').",
    )
    range_group.add_argument(
        "--start-from",
        default=None,
        metavar="YYYY[/MM]",
        help="Resume/start inclusive from 'YYYY' or 'YYYY/MM'.",
    )
    parser.add_argument(
        "--end-at",
        default=None,
        metavar="YYYY[/MM]",
        help=(
            "Stop inclusive at 'YYYY' or 'YYYY/MM' "
            "(cannot combine with --only)."
        ),
    )

    parser.add_argument(
        "--workers",
        type=int,
        default=DEFAULT_WORKERS,
        help=(
            "Concurrent worker threads per month "
            f"(default: {DEFAULT_WORKERS})."
        ),
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help=(
            "Read-only simulation: validate and test patching in memory "
            "without writing local files or uploading to GCS."
        ),
    )
    parser.add_argument(
        "-y",
        "--yes",
        action="store_true",
        help=(
            "Skip interactive [yN] confirmation prompt when "
            "writing/uploading."
        ),
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Log every individual file outcome.",
    )

    args = parser.parse_args(argv)
    if args.only is not None and args.end_at is not None:
        parser.error("argument --end-at: not allowed with argument --only")
    if args.local_dir is not None and args.prefix is not None:
        parser.error("argument --prefix: not allowed with argument --local-dir")
    if args.prefix is None:
        args.prefix = DEFAULT_PREFIX
    if args.local_dir is None:
        bucket = args.bucket.strip()
        if not bucket or "/" in bucket:
            parser.error("argument --bucket: must be a valid GCS bucket name")
        args.bucket = bucket
    if args.workers < 1:
        parser.error("argument --workers: must be >= 1")
    return args


def resolve_bounds(args: argparse.Namespace) -> PartitionBounds:
    """Resolves PartitionBounds from parsed CLI args."""
    if args.only is not None:
        return PartitionBounds(
            start_from=parse_year_month_bound(args.only, is_end=False),
            end_at=parse_year_month_bound(args.only, is_end=True),
        )
    start_from = (
        parse_year_month_bound(args.start_from, is_end=False)
        if args.start_from is not None
        else None
    )
    end_at = (
        parse_year_month_bound(args.end_at, is_end=True)
        if args.end_at is not None
        else None
    )
    if start_from is not None and end_at is not None and start_from > end_at:
        msg = f"--start-from ({start_from}) is after --end-at ({end_at})."
        raise ValueError(msg)
    return PartitionBounds(start_from=start_from, end_at=end_at)


@dataclasses.dataclass(frozen=True)
class ModeDisplay:
    """Labels describing the current execution mode in logs."""

    banner: str
    action_label: str


def _describe_mode(*, is_remote_gcs: bool, dry_run: bool) -> ModeDisplay:
    """Returns ModeDisplay labels for clear logging."""
    if is_remote_gcs:
        if dry_run:
            return ModeDisplay(
                banner="DRY-RUN (GCS READ-ONLY, NO UPLOADS)",
                action_label="would_upload",
            )
        return ModeDisplay(
            banner="LIVE GCS UPLOAD (WRITES TO REMOTE BUCKET)",
            action_label="uploaded",
        )
    if dry_run:
        return ModeDisplay(
            banner="DRY-RUN (LOCAL MIRROR, NO WRITES)",
            action_label="would_write",
        )
    return ModeDisplay(
        banner="LOCAL MIRROR WRITE (NO GCS UPLOADS)",
        action_label="written_locally",
    )


def confirm_execution(
    backend: LocalArchiveBackend | GcsArchiveBackend,
    partitions: Sequence[YearMonth],
    workers: int,
) -> bool:
    """Prompts the user before mutating local files or uploading to GCS."""
    first_ym = partitions[0]
    last_ym = partitions[-1]
    if backend.is_remote_gcs:
        prompt = (
            f"LIVE GCS UPLOAD: Upload modified index.xml files to "
            f"{backend.description} across {len(partitions)} month(s) "
            f"({first_ym}..{last_ym}) with {workers} worker(s)? [yN] "
        )
    else:
        prompt = (
            f"LOCAL WRITE (no GCS upload): Modify local index.xml files in "
            f"{backend.description} across {len(partitions)} month(s) "
            f"({first_ym}..{last_ym}) with {workers} worker(s)? [yN] "
        )
    try:
        response = input(prompt)
    except EOFError:
        return False
    except KeyboardInterrupt:
        sys.stderr.write("\n")
        raise
    return response.strip().lower() == "y"


def _format_outcome_label(
    outcome: Outcome,
    *,
    patched_label: str,
) -> str:
    """Formats an Outcome using the mode-specific action label for PATCHED."""
    if outcome is Outcome.PATCHED:
        return patched_label
    return outcome.value


@contextlib.contextmanager
def _managed_thread_pool(
    workers: int,
) -> Generator[futures.ThreadPoolExecutor]:
    """Yields a ThreadPoolExecutor that cancels queued futures on exception."""
    executor = futures.ThreadPoolExecutor(max_workers=workers)
    try:
        yield executor
    except BaseException:
        executor.shutdown(wait=True, cancel_futures=True)
        raise
    else:
        executor.shutdown(wait=True)


def _process_partition(
    executor: futures.ThreadPoolExecutor,
    backend: LocalArchiveBackend | GcsArchiveBackend,
    ym: YearMonth,
    *,
    cumulative: Stats,
    verbose: bool,
    patched_label: str,
) -> Stats:
    """Processes all build targets in a single YYYY/MM partition."""
    targets = backend.list_targets(ym)
    month_stats = Stats()

    future_map = {
        executor.submit(backend.process_target, target): target
        for target in targets
    }
    for future in futures.as_completed(future_map):
        result = future.result()
        month_stats.record(result.outcome)
        if verbose:
            _LOGGER.debug(
                "[%s] %s: %s",
                ym,
                _format_outcome_label(
                    result.outcome,
                    patched_label=patched_label,
                ),
                result.target,
            )

    cumulative.add(month_stats)
    _LOGGER.info(
        "Completed %s (%d builds): %s=%d, already_patched=%d, "
        "missing=%d, failed=%d | cumulative %s=%d, "
        "already_patched=%d, failed=%d",
        ym,
        len(targets),
        patched_label,
        month_stats.patched,
        month_stats.already_patched,
        month_stats.missing,
        month_stats.failed,
        patched_label,
        cumulative.patched,
        cumulative.already_patched,
        cumulative.failed,
    )
    return month_stats


def _execute_partitions(
    backend: LocalArchiveBackend | GcsArchiveBackend,
    partitions: Sequence[YearMonth],
    *,
    workers: int,
    verbose: bool,
    mode_label: str,
    patched_label: str,
) -> int:
    """Processes partitions sequentially with concurrent per-month workers."""
    cumulative = Stats()
    start_time = time.monotonic()
    current_ym: YearMonth | None = None

    try:
        with _managed_thread_pool(workers) as executor:
            for ym in partitions:
                current_ym = ym
                month_stats = _process_partition(
                    executor,
                    backend,
                    ym,
                    cumulative=cumulative,
                    verbose=verbose,
                    patched_label=patched_label,
                )
                if month_stats.failed > 0:
                    _LOGGER.error(
                        "Stopping after failures in %s. Re-run with "
                        '--start-from="%s" to retry.',
                        ym,
                        ym,
                    )
                    return 1
    except (*_GCS_OPERATION_ERRORS, OSError) as exc:
        _LOGGER.error(
            "Failed processing partition %s: %s. Re-run with "
            '--start-from="%s" to retry.',
            current_ym,
            exc,
            current_ym,
        )
        return 1
    except KeyboardInterrupt:
        if current_ym is not None:
            _LOGGER.warning(
                'Interrupted during %s. Resume safely with --start-from="%s".',
                current_ym,
                current_ym,
            )
        else:
            _LOGGER.warning("Interrupted.")
        return 130

    elapsed = time.monotonic() - start_time
    _LOGGER.info(
        "Finished [%s] in %.1fs: total=%d, %s=%d, already_patched=%d, "
        "missing=%d, failed=%d",
        mode_label,
        elapsed,
        cumulative.total,
        patched_label,
        cumulative.patched,
        cumulative.already_patched,
        cumulative.missing,
        cumulative.failed,
    )
    return 0


def _run_with_backend(
    backend: LocalArchiveBackend | GcsArchiveBackend,
    args: argparse.Namespace,
    bounds: PartitionBounds,
) -> int:
    """Discovers partitions, confirms execution, and runs the backfill."""
    _LOGGER.info(
        "Discovering YYYY/MM partitions in %s ...", backend.description
    )
    partitions = backend.list_year_months(bounds)
    if not partitions:
        _LOGGER.warning("No matching YYYY/MM partitions found.")
        return 0

    mode_display = _describe_mode(
        is_remote_gcs=backend.is_remote_gcs,
        dry_run=args.dry_run,
    )
    _LOGGER.info(
        "Mode=%s | Target=%s | Partitions=%d (%s..%s) | Workers=%d",
        mode_display.banner,
        backend.description,
        len(partitions),
        partitions[0],
        partitions[-1],
        args.workers,
    )
    if backend.is_remote_gcs and not args.dry_run:
        _LOGGER.warning(
            "LIVE GCS UPLOAD ENABLED: Modified index.xml manifests "
            "WILL BE UPLOADED to %s",
            backend.description,
        )

    if (
        not args.dry_run
        and not args.yes
        and not confirm_execution(backend, partitions, args.workers)
    ):
        _LOGGER.info("Cancelled.")
        return 1

    return _execute_partitions(
        backend,
        partitions,
        workers=args.workers,
        verbose=args.verbose,
        mode_label=mode_display.banner,
        patched_label=mode_display.action_label,
    )


def main(argv: Sequence[str] | None = None) -> int:
    """Main entry point returning process exit status."""
    args = parse_args(argv)
    # absl-style logging.
    logging.basicConfig(
        level=logging.INFO,
        style="{",
        format=(
            "{levelname[0]}{asctime}.{msecs:03.0f} {thread} "
            "{filename}:{lineno}] {message}"
        ),
        datefmt="%m%d %H:%M:%S",
    )
    if args.verbose:
        _LOGGER.setLevel(logging.DEBUG)

    try:
        bounds = resolve_bounds(args)
    except ValueError as exc:
        _LOGGER.error("%s", exc)
        return 2

    try:
        backend: LocalArchiveBackend | GcsArchiveBackend
        if args.local_dir is not None:
            if not args.local_dir.is_dir():
                _LOGGER.error(
                    "Local archive directory not found: %s", args.local_dir
                )
                return 2
            backend = LocalArchiveBackend(args.local_dir, dry_run=args.dry_run)
        else:
            backend = GcsArchiveBackend(
                args.bucket,
                args.prefix,
                workers=args.workers,
                dry_run=args.dry_run,
            )

        with backend:
            return _run_with_backend(backend, args, bounds)
    except (*_GCS_OPERATION_ERRORS, OSError) as exc:
        _LOGGER.error("%s", exc)
        return 1
    except KeyboardInterrupt:
        _LOGGER.warning("Interrupted.")
        return 130


if __name__ == "__main__":
    sys.exit(main())
