import contextlib
from distutils import log as distutils_log
import io
import os
import tempfile
import unittest
from pathlib import Path
from textwrap import dedent
from unittest import mock

from grpc_tools import auto_generate

SOME_STRING = "some test data"
SOME_NON_STRING = 1
SOME_PROTO_DIR = "test_proto_dir"
SOME_FILE_PATTERN = "*_test.proto"
SOME_MATCHED_PROTO_FILE = "foo_test.proto"
SOME_OTHER_MATCHED_PROTO_FILE = "bar_test.proto"
SOME_DEST_DIR = "test_dest_dir"
SOME_ADDITIONAL_ARGS = "--test-arg foo"
SOME_ADDITIONAL_ARGS_PIECES = ["--test-arg", "foo"]
SOME_ERROR_CODE = 1
SOME_SUCCESS_CODE = 0


class AutoGenerateTest(unittest.TestCase):
    def setUp(self) -> None:
        self._temp_dir = TemporaryWorkingDirectory(suffix=self.id())
        self._temp_dir.set_up()

        self._original_distutils_log_level = distutils_log.set_threshold(
            distutils_log.DEBUG
        )

    def tearDown(self) -> None:
        distutils_log.set_threshold(self._original_distutils_log_level)

        self._temp_dir.tear_down()

    def test_generate_files__no_config_file__no_generation(self):
        with contextlib.redirect_stdout(io.StringIO()) as stdout:
            auto_generate.generate_files(mock.Mock())

        self.assertIn("Doing nothing", stdout.getvalue())

    def test_generate_files__file_exists_no_section__no_generation(self):
        self._temp_dir.dir.joinpath(auto_generate.CONFIG_FILE_NAME).touch()

        with contextlib.redirect_stdout(io.StringIO()) as stdout:
            auto_generate.generate_files(mock.Mock())

        self.assertIn("Doing nothing", stdout.getvalue())

    def test_generate_files__file_exists_missing_value__exception(self):
        self._temp_dir.dir.joinpath(auto_generate.CONFIG_FILE_NAME).write_text(
            "[tool.grpcio_tools]\n"
        )

        with self.assertRaises(ValueError):
            auto_generate.generate_files(mock.Mock())

    def test_generate_files__file_exists_value_wrong_type__exception(self):
        data = [
            (SOME_NON_STRING, SOME_STRING, SOME_STRING, SOME_STRING),
            (SOME_STRING, SOME_NON_STRING, SOME_STRING, SOME_STRING),
            (SOME_STRING, SOME_STRING, SOME_NON_STRING, SOME_STRING),
            (SOME_STRING, SOME_STRING, SOME_STRING, SOME_NON_STRING),
        ]
        for proto_dir, proto_file_pattern, dest_dir, additional_args in data:
            with self.subTest(
                proto_dir=proto_dir,
                proto_file_pattern=proto_file_pattern,
                dest_dir=dest_dir,
                additional_args=additional_args,
            ):
                _write_config_file(
                    self._temp_dir,
                    proto_dir,
                    proto_file_pattern,
                    dest_dir,
                    additional_args,
                )

                with self.assertRaises(ValueError):
                    auto_generate.generate_files(mock.Mock())

    def test_generate_files__generation_failure__exception(self):
        _write_config_file(
            self._temp_dir, SOME_PROTO_DIR, SOME_FILE_PATTERN, SOME_DEST_DIR
        )
        proto_dir = _make_proto_dir(self._temp_dir.dir)
        proto_dir.joinpath(SOME_MATCHED_PROTO_FILE).touch()

        with mock.patch("grpc_tools.protoc.main", return_value=SOME_ERROR_CODE):
            with self.assertRaises(auto_generate.GenerationException):
                auto_generate.generate_files(mock.Mock())

    def test_generate_files__no_matched_proto_files__exception(self):
        _write_config_file(
            self._temp_dir, SOME_PROTO_DIR, SOME_FILE_PATTERN, SOME_DEST_DIR
        )

        with self.assertRaises(auto_generate.GenerationException):
            auto_generate.generate_files(mock.Mock())

    def test_generate_files__generation_success__args_passed(self):
        _write_config_file(
            self._temp_dir, SOME_PROTO_DIR, SOME_FILE_PATTERN, SOME_DEST_DIR
        )
        proto_dir = _make_proto_dir(self._temp_dir.dir)
        proto_dir.joinpath(SOME_MATCHED_PROTO_FILE).touch()

        with mock.patch(
            "grpc_tools.protoc.main", return_value=SOME_SUCCESS_CODE
        ) as mock_main:
            with mock.patch(
                "pkg_resources.resource_filename", return_value=SOME_STRING
            ):
                auto_generate.generate_files(mock.Mock())

        mock_main.assert_called_once_with(
            [
                f"-I={SOME_STRING}",
                f"-I={SOME_PROTO_DIR}",
                f"--python_out={SOME_DEST_DIR}",
                f"--grpc_python_out={SOME_DEST_DIR}",
                f"{SOME_PROTO_DIR}/{SOME_MATCHED_PROTO_FILE}",
            ]
        )

    def test_generate_files__additional_args__args_passed_split(self):
        _write_config_file(
            self._temp_dir,
            SOME_PROTO_DIR,
            SOME_FILE_PATTERN,
            SOME_DEST_DIR,
            SOME_ADDITIONAL_ARGS,
        )
        proto_dir = _make_proto_dir(self._temp_dir.dir)
        proto_dir.joinpath(SOME_MATCHED_PROTO_FILE).touch()

        with mock.patch(
            "grpc_tools.protoc.main", return_value=SOME_SUCCESS_CODE
        ) as mock_main:
            with mock.patch(
                "pkg_resources.resource_filename", return_value=SOME_STRING
            ):
                auto_generate.generate_files(mock.Mock())

        mock_main.assert_called_once_with(
            [
                f"-I={SOME_STRING}",
                f"-I={SOME_PROTO_DIR}",
                f"--python_out={SOME_DEST_DIR}",
                f"--grpc_python_out={SOME_DEST_DIR}",
                *SOME_ADDITIONAL_ARGS_PIECES,
                f"{SOME_PROTO_DIR}/{SOME_MATCHED_PROTO_FILE}",
            ]
        )

    def test_generate_files__multiple_files__multiple_calls(self):
        _write_config_file(
            self._temp_dir, SOME_PROTO_DIR, SOME_FILE_PATTERN, SOME_DEST_DIR
        )
        proto_dir = _make_proto_dir(self._temp_dir.dir)
        proto_dir.joinpath(SOME_MATCHED_PROTO_FILE).touch()
        proto_dir.joinpath(SOME_OTHER_MATCHED_PROTO_FILE).touch()

        with mock.patch(
            "grpc_tools.protoc.main", return_value=SOME_SUCCESS_CODE
        ) as mock_main:
            with mock.patch(
                "pkg_resources.resource_filename", return_value=SOME_STRING
            ):
                auto_generate.generate_files(mock.Mock())

        for proto_file in (SOME_MATCHED_PROTO_FILE, SOME_OTHER_MATCHED_PROTO_FILE):
            mock_main.assert_any_call(
                [
                    f"-I={SOME_STRING}",
                    f"-I={SOME_PROTO_DIR}",
                    f"--python_out={SOME_DEST_DIR}",
                    f"--grpc_python_out={SOME_DEST_DIR}",
                    f"{SOME_PROTO_DIR}/{proto_file}",
                ]
            )


class TemporaryWorkingDirectory:
    def __init__(self, **kwargs) -> None:
        self._temp_dir = tempfile.TemporaryDirectory(**kwargs)
        self.dir = Path(self._temp_dir.name)
        self._original_workdir = os.getcwd()

    def set_up(self):
        os.chdir(self._temp_dir.name)

    def tear_down(self):
        os.chdir(self._original_workdir)
        self._temp_dir.cleanup()


def _write_config_file(
    config_location: TemporaryWorkingDirectory,
    proto_dir,
    proto_file_pattern,
    dest_dir,
    additional_args=None,
) -> None:
    def _quote_if_str(value):
        return f"'{value}'" if isinstance(value, str) else value

    text = dedent(
        f"""\
    [tool.grpcio_tools]
    proto_dir={_quote_if_str(proto_dir)}
    proto_file_pattern={_quote_if_str(proto_file_pattern)}
    dest_dir={_quote_if_str(dest_dir)}
    """
    )
    if additional_args is not None:
        text += dedent(
            f"""\
            additional_args={_quote_if_str(additional_args)}
            """
        )
    config_location.dir.joinpath(auto_generate.CONFIG_FILE_NAME).write_text(text)


def _make_proto_dir(base_dir: Path) -> Path:
    proto_dir = base_dir / SOME_PROTO_DIR
    proto_dir.mkdir()
    return proto_dir


if __name__ == "__main__":
    unittest.main()
