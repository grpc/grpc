---
trigger: glob
globs: "*.sh, *.bash"
---

<shell_authoring_safeguards>

# Shell Code Authoring & Formatting Guidelines (Google Style)

Adhere strictly to the [Google Shell Style Guide](https://raw.githubusercontent.com/google/styleguide/refs/heads/gh-pages/shellguide.md) when authoring or modifying shell script files (*.sh, *.bash); does NOT apply to inline terminal execution commands run directly by the agent:

*   **Consistency**: Stay consistent with existing code style and conventions in the file being modified.

## 1. Executable Shells & Interpreter Invocation

*   **Executable Interpreter**: Prefer `#!/usr/bin/env bash` over fixed path `#!/bin/bash` for new executable scripts; preserve existing shebangs in modified files. Set execution flags in-body (`set -euo pipefail`).
*   **Trace Prompt Formatting (`PS4`)**: Recommend setting `PS4='+ \D{[%H:%M:%S %Z]}\011 '` when enabling execution tracing (`set -x`).
*   **Trace Secret Leak Safeguards**: Mandate verifying no sensitive data (access/identity tokens, bearer headers, API credentials) is logged when execution tracing (`set -x`) or verbose output (`curl -v`, `gcloud auth print-access-token`) is enabled; disable tracing (`set +x`) around credential commands or avoid tracing altogether by using alternative approaches.
*   **File Extensions**: Omit extensions for `PATH` executables; use `.sh` for build targets and standalone scripts. Enforce `.sh` and non-executable status (`chmod -x`) for libraries.
*   **Forbidden SUID/SGID**: Prohibit SUID/SGID flags on shell scripts; execute with `sudo` for elevated access.
*   **Language Scope Limits**: Restrict shell usage to small utilities or wrappers (<100 lines); rewrite scripts exceeding 100 lines or complex control flow in a structured language (e.g., Python).

## 2. Environment & Stream Redirection

*   **Error Stream Routing**: Route error and status messages exclusively to `STDERR` (`err() { echo "[$(date +'%Y-%m-%dT%H:%M:%S%z')]: $*" >&2; }`).
*   **Combined Redirection & Piping (`&>` and `|&`)**: Prefer `&>` over `> file 2>&1` or `> /dev/null 2>&1` (`cmd &> file`, `cmd &> /dev/null`) and `|&` over `2>&1 |` (`cmd1 |& cmd2`) when redirecting or piping stdout and stderr together in Bash scripts. Avoid explicit fd duplication (`2>&1`) unless required for specific ordering or command substitution capturing. Single-stream redirections (e.g., `2>/dev/null`) remain unaffected.
*   **Quoted Here-Document Delimiters**: Enforce quoted here-doc delimiters (`<<'EOF'`) when emitting or generating scripts containing literal shell variables to prevent unintended expansion.

## 3. Code Formatting & Control Flow Syntax

*   **Indentation & Line Limits**: Indent 2 spaces with no tabs (except `<<-` here-docs). Enforce 80-character line length limit; use here-docs or embedded newlines for long strings.
*   **Pipeline Splitting**: Fit short pipelines on one line; split long pipelines one segment per line. Prefer placing operators (`|`, `||`, `&&`) at the end of the line (automatic continuation) over trailing backslash `\` continuation. Both styles are acceptable.
*   **Control Flow Formatting**: Place `; then` and `; do` on the same line as header (`if`, `for`, `while`, `until`, `select`). Keep `else`, `fi`, and `done` on dedicated lines. Explicitly iterate positional parameters (`for arg in "$@"; do`).
*   **Case Statement Formatting**: Indent patterns by 2 spaces and actions by 4 spaces. Prohibit leading pattern parenthesis `(` and fallthrough operators (`;&`, `;;&`).

## 4. Variables, Quoting & Expansion

*   **Variable Expansion**: Prefer brace-delimited `"${var}"` over `"$var"`. Omit braces on single-character shell specials and positional parameters (`$1`, `$@`, `$#`, `$?`, `$$`) unless preventing ambiguity (`"${1}0"`).
*   **String Quoting Discipline**: Quote variables, command substitutions, spaces, and meta-characters. Use `"$@"` for passing positional parameters intact; prohibit quoting literal integer assignments.
*   **Function Local Variables**: Declare function variables with `local`. Separate local declaration from command substitution assignment (`local var; var="$(cmd)"`) to avoid masking exit status (`$?`).
*   **Constants & Exports**: Capitalize environment variables and constants (`readonly PATH_TO_FILES='/path'`, `export VAR`). Declare constants at file top and apply `readonly` immediately after assignment.

## 5. Shell Features, Tests & `set -e` Traps

*   **Mandatory ShellCheck**: Verify scripts with `shellcheck` to catch syntax defects, security risks, and portability warnings prior to finalizing changes.
*   **`set -e` Arithmetic Safety**: Avoid standalone `(( ... ))` statements, especially with `set -e`; use standard assignment `i=$(( i + 1 ))` or append `|| true` to prevent zero-evaluation exits.
*   **Conditional Evaluation & RHS Patterns**: Prefer `[[ ... ]]` over `[ ... ]` or `test`. Enforce explicit `-z` / `-n` string tests, `==` equality, and `(( ... ))` for numeric checks. Leave RHS unquoted for regex/glob matching (`[[ $v =~ ^[0-9]+$ ]]`, `[[ $v == f* ]]`); prohibit quoting RHS unless matching literal string.
*   **Wildcard Filename Expansion**: Enforce explicit relative path prefixes (`./*`) during wildcard expansion to prevent hyphenated filenames from parsing as options.
*   **Prohibit `eval` & Aliases**: Prohibit `eval` and script aliases; use functions and arrays exclusively.
*   **Arrays & Process Substitution**: Use arrays (`declare -a flags`) for parameter lists. Prefer process substitution (`while read -r line; do ...; done < <(cmd)`) or `readarray` over piped `while` loops to avoid subshell state loss.

## 6. Builtin Commands & Parameter Expansions

*   **Builtins over External Subprocesses**: Prefer shell builtins over external utility processes (`sed`, `awk`, `cut`, `expr`, `grep`).
*   **Native Parameter Expansions**: Use built-in string expansions for parsing and manipulation (`${var#prefix}`, `${var##*glob}`, `${var%suffix}`, `${var%%suffix*}`), replacement (`${var//pattern/replace}`), and default assignment (`${var:=default}`).
*   **Regex Match Arrays**: Use `${BASH_REMATCH[1]}` for regex capture extraction after `[[ $var =~ pattern ]]` evaluation.

## 7. Naming Conventions & Functions

*   **Naming Conventions**: Enforce snake_case for functions and variables (`my_func`, `my_var`); use `::` for package namespaces (`pkg::my_func`).
*   **Function Syntax & Docstrings**: Place opening brace on declaration line without space (`func() {`). Write header comments explicitly documenting `Globals:`, `Arguments:`, `Outputs:`, and `Returns:`.
*   **Script Main Entry Point**: Encapsulate executable script logic in a `main` function placed as the final function; conclude file with `main "$@"`.
*   **Return Code Verification**: Verify command exit status (`if ! cmd; then`, `(( $? == 0 ))`). Inspect `PIPESTATUS` for pipeline stages (`return_codes=("${PIPESTATUS[@]}")`).

</shell_authoring_safeguards>
