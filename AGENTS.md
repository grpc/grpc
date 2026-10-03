# AGENTS.md

Context file for AI agents working on grpc.

## Project Overview

grpc is a C project using npm/Node.js.

**Key Info:**
- **Primary Language:** C
- **Build System:** npm/Node.js
- **Test Framework:** JUnit, RSpec
- **Total Files:** 10461
- **Test Files:** 2847
- **AI Readiness Score:** 98/100 (Agent-Optimized)

## Prerequisites

- **C:** 3.9+ (or applicable language version)
- **Package Manager:** pip or uv (recommended)
- **Test Runner:** JUnit, RSpec

## Project Structure

```
grpc/
├── Makefile
├── pyproject.toml
├── setup.py
├── src/                  # Source code
├── tests/                # Test suite (2847 files)
└── README.md             # Project documentation
```

## Architecture Overview

### Key Components
- **Main Entry:** main.py, server.py, server.py, server.py, server.py
- **Test Suite:** 2847 test files
- **Build Configuration:** Makefile, pyproject.toml, setup.py

### Design Principles

1. **Modularity** - Code organized by functionality with clear separation of concerns
2. **Testability** - Comprehensive test coverage across critical paths
3. **Clarity** - Explicit naming and structure for AI agent understanding
4. **Consistency** - Uniform patterns and conventions throughout codebase
5. **Maintainability** - Well-documented code with clear intent

## Testing Patterns

- **Frameworks**: RSpec, JUnit
- **Test Files**: 2847
- **Structure**: Tests organized in: CFStreamTests, CFStreamTests.xcodeproj, ConnectivityTestingApp, ConnectivityTestingApp.xcodeproj, DistribTest, EventEngineTests, GRPCCppTests.xcodeproj, GrpcIosTest.xcodeproj, GrpcIosTestUITests, InteropTests, MacTests, PerfTests, PersistentChannelTests, PluginTest, RemoteTestClient, TestCertificates.bundle, Testing, Tests.xcodeproj, TvTests, UnitTests, bazelify_tests, build_test, bzlmod_test, distribtest, grpcio_tests, http2_test, interoptest, invocation_testing, lb_interop_tests, manual_tests, python_second_test_repo, run_tests, spec, test, test_bundles, test_creds, test_data, test_policies, test_roots, test_suite, test_util, testdata, testing, tests, tests_aio, tests_gevent, tests_py3_only, unit_tests, xds_k8s_test_driver
- **Coverage Tools**: Yes


## Development Workflow

### Initial Setup

```bash
git clone https://github.com/<owner>/grpc.git
cd grpc
pip install -e .              # Install in development mode
# or
uv sync --all-groups          # Using uv (recommended)
```

### Development Commands

#### Running Tests
```bash
pytest                        # Run all tests
pytest tests/                 # Run specific test directory
pytest -v                     # Verbose output with test names
pytest -x                     # Stop on first failure
pytest --cov                  # With coverage report
```

#### Code Quality
```bash
ruff check .                  # Lint with ruff
ruff format .                 # Format code
mypy .                        # Type checking (if configured)
```

## Code Style & Conventions

- **Naming:** Use C conventions (snake_case for functions, PascalCase for classes)
- **Type Hints:** Yes (strongly encouraged)
- **Error Handling:** Yes
- **Logging:** Yes
- **Testing:** Yes - write tests alongside code changes

## Testing Strategy

**Framework:** JUnit, RSpec
**Test Files:** 2847 found

Before committing:
1. Run the full test suite: `pytest`
2. Ensure all tests pass
3. Check type hints: `mypy .`
4. Format code: `ruff format .`

## Common Patterns

When contributing to this project:
1. Read existing code in the area you're modifying
2. Follow the established patterns and style
3. Write tests for new functionality
4. Use clear, descriptive variable and function names
5. Add docstrings for public APIs
6. Update tests when changing behavior

## What We Value

✅ Well-tested code with clear intent
✅ Consistent code style and naming conventions
✅ Code that is easy for AI agents to understand
✅ Clear, descriptive commit messages
✅ Modular, reusable components
✅ Comprehensive documentation

## What We Avoid

❌ Large functions doing multiple things
❌ Commented-out dead code
❌ Inconsistent naming or patterns
❌ Unclear error messages
❌ Unexplained magic numbers or strings
❌ Skipped tests or test TODOs

## AI Readiness Dimensions (Scoring)

This project is evaluated across 8 dimensions:

1. **Architecture** (20/100) - Code organization and modularity
2. **Testing** (15/100) - Test coverage and quality
3. **Dependencies** (12/100) - Dependency management
4. **Conventions** (8/100) - Consistent patterns
5. **Entry Points** (10/100) - Clear main/start locations
6. **Security** (15/100) - Input validation and error handling
7. **Build** (10/100) - Clear build/setup instructions
8. **Documentation** (8/100) - Code and project documentation

## Next Steps

Before making changes:
1. Read relevant source files to understand the existing code
2. Look at existing tests for similar functionality
3. Follow the patterns you see in the codebase
4. Write tests for your changes
5. Run `pytest` to verify nothing breaks
6. Run code quality checks: `ruff check . && mypy .`
7. Format your code: `ruff format .`

---

*Generated by Braxis - keeping AI agents in sync with your code*
