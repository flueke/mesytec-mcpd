# Development notes

## Python package

Build a wheel:

    pip wheel -w dist .[gui]

Install from a wheel:

    pip install 'dist/mesytec_mcpd-<version>-<tags>.whl[gui]'

Editable dev installation with fast rebuilds:

    # Optional:
    export SKBUILD_CMAKE_BUILD_TYPE=Debug
    pip install -e .[dev,gui] --no-deps --no-build-isolation -v

This creates and reuses a directory under `build/` for the cmake part. Everything
can be inspected and cmake can be run manually or from vscode if needed. The
build deps need to be installed in the active venv, otherwise the build fails.

Note: a non-editable install in the venv shadows the source tree. pytest then
runs against the installed copy.

## Tests

    pytest                  # python tests, uses the installed package
    ctest --test-dir build  # C++ unit tests

## Windows

- Everything has to be run in a 'native tools command prompt'.
- The code builds with msvc. To use clang-cl instead:

      set CMAKE_GENERATOR=Ninja
      set CC=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-cl.exe
      set CXX=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-cl.exe

- 'Ninja' could also be set in pyproject.toml under `[tool.scikit-build] cmake.args`.
- Check the vscode settings.json for `cmake.generator` too in case msbuild is still used.
- pybind11-stubgen is not run on windows, the wheels contain no type stubs.

## uv and the lock file

- Generate uv.lock: `uv lock` from the project root
- Install from the lock file: `uv sync --extra gui --extra dev`

## Packaging for (test)pypi

Versions come from git tags via setuptools-scm (`no-guess-dev` scheme).

Local build and upload:

    cibuildwheel
    python3 -m twine upload --repository testpypi wheelhouse/*

The `Publish package` github workflow builds wheels and the sdist for a given
tag and publishes to testpypi or pypi.

Install from testpypi (dependencies come from pypi):

    pip install -i https://test.pypi.org/simple/ --extra-index-url https://pypi.org/simple/ 'mesytec-mcpd[gui]'
