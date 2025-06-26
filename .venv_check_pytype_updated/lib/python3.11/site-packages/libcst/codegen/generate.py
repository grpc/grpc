# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# Usage:
#
# python -m libcst.codegen.generate --help
# python -m libcst.codegen.generate visitors

import argparse
import os
import os.path
import shutil
import subprocess
import sys
from typing import List

import libcst as cst
from libcst import ensure_type, parse_module
from libcst.codegen.transforms import (
    DoubleQuoteForwardRefsTransformer,
    SimplifyUnionsTransformer,
)


def format_file(fname: str) -> None:
    subprocess.check_call(
        ["ufmt", "format", fname],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )


def clean_generated_code(code: str) -> str:
    """
    Generalized sanity clean-up for all codegen so we can fix issues such as
    Union[SingleType]. The transforms found here are strictly for form and
    do not affect functionality.
    """
    module = parse_module(code)
    module = ensure_type(module.visit(SimplifyUnionsTransformer()), cst.Module)
    module = ensure_type(module.visit(DoubleQuoteForwardRefsTransformer()), cst.Module)
    return module.code


def codegen_visitors() -> None:
    # First, back up the original file, since we have a nasty bootstrap problem.
    # We're in a situation where we want to import libcst in order to get the
    # valid nodes for visitors, but doing so means that we depend on ourselves.
    # So, this attempts to keep the repo in a working state for as many operations
    # as possible.
    base = os.path.abspath(
        os.path.join(os.path.dirname(os.path.abspath(__file__)), "../")
    )
    visitors_file = os.path.join(base, "_typed_visitor.py")
    shutil.copyfile(visitors_file, f"{visitors_file}.bak")

    try:
        # Now that we backed up the file, lets codegen a new version.
        # We import now, because this script does work on import.
        import libcst.codegen.gen_visitor_functions as visitor_codegen

        new_code = clean_generated_code("\n".join(visitor_codegen.generated_code))
        with open(visitors_file, "w") as fp:
            fp.write(new_code)
            fp.close()

        # Now, see if the file we generated causes any import errors
        # by attempting to run codegen again in a new process.
        subprocess.check_call(
            [sys.executable, "-m", "libcst.codegen.gen_visitor_functions"],
            cwd=base,
            stdout=subprocess.DEVNULL,
        )

        # If it worked, lets format the file
        format_file(visitors_file)

        # Since we were successful with importing, we can remove the backup.
        os.remove(f"{visitors_file}.bak")

        # Inform the user
        print(f"Successfully generated a new {visitors_file} file.")
    except Exception:
        # On failure, we put the original file back, and keep the failed version
        # for developers to look at.
        print(
            f"Failed to generated a new {visitors_file} file, failure "
            + f"is saved in {visitors_file}.failed_generate.",
            file=sys.stderr,
        )
        os.rename(visitors_file, f"{visitors_file}.failed_generate")
        os.rename(f"{visitors_file}.bak", visitors_file)

        # Reraise so we can debug
        raise


def codegen_matchers() -> None:
    # Given that matchers isn't in the default import chain, we don't have to
    # worry about generating invalid code that then prevents us from generating
    # again.
    import libcst.codegen.gen_matcher_classes as matcher_codegen

    base = os.path.abspath(
        os.path.join(os.path.dirname(os.path.abspath(__file__)), "../")
    )
    matchers_file = os.path.join(base, "matchers/__init__.py")
    new_code = clean_generated_code("\n".join(matcher_codegen.generated_code))
    with open(matchers_file, "w") as fp:
        fp.write(new_code)
        fp.close()

    # If it worked, lets format the file
    format_file(matchers_file)

    # Inform the user
    print(f"Successfully generated a new {matchers_file} file.")


def codegen_return_types() -> None:
    # Given that matchers isn't in the default import chain, we don't have to
    # worry about generating invalid code that then prevents us from generating
    # again.
    import libcst.codegen.gen_type_mapping as type_codegen

    base = os.path.abspath(
        os.path.join(os.path.dirname(os.path.abspath(__file__)), "../")
    )
    type_mapping_file = os.path.join(base, "matchers/_return_types.py")
    new_code = clean_generated_code("\n".join(type_codegen.generated_code))
    with open(type_mapping_file, "w") as fp:
        fp.write(new_code)
        fp.close()

    # If it worked, lets format the file
    format_file(type_mapping_file)

    # Inform the user
    print(f"Successfully generated a new {type_mapping_file} file.")


def main(cli_args: List[str]) -> int:
    # Parse out arguments, run codegen
    parser = argparse.ArgumentParser(description="Generate code for libcst.")
    parser.add_argument(
        "system",
        choices=["all", "visitors", "matchers", "return_types"],
        help="System to generate code for.",
        type=str,
    )
    args = parser.parse_args(cli_args)
    if args.system == "all":
        codegen_visitors()
        codegen_matchers()
        codegen_return_types()
        return 0
    if args.system == "visitors":
        codegen_visitors()
        return 0
    elif args.system == "matchers":
        codegen_matchers()
        return 0
    elif args.system == "return_types":
        codegen_return_types()
        return 0
    else:
        print(f'Invalid system "{args.system}".')
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
