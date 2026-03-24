# To build a wheel

    pip wheel -w dist .[gui]

# Installation from wheel

    pip install dist\mesytec_mcpd_py-0.6.1.dev35-cp314-cp314-win_amd64.whl[gui]

# Editable dev installation

    # Optional:
    export SKBUILD_CMAKE_BUILD_TYPE=Debug
    set CMAKE_GENERATOR=Ninja
    set CC=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin\clang-cl.exe
    set CXX=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin\clang-cl.exe
    # Build with fast rebuilds:
    pip install -e ..[dev,gui] --no-deps --no-build-isolation -v

  This will create and resuse a dir under build for the cmake part. Everything
  can be inspected and cmake can be run manually or from vscode if needed. The
  build deps need to be installed in the active venv otherwise the build will
  fail.

# Notes
- To use clang-cl set CMAKE_GENERATOR=Ninja and set both CC and CXX to the full path to clang-cl, e.g.
  set CMAKE_GENERATOR=Ninja
  set CXX=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-cl.exe
  set CC=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-cl.exe

- 'Ninja' could also be set in pyproject.toml under [tool.scikit-build] cmake.args .

- Check vs code settings.json for cmake.generator too in case msbuild is still used!

- The code currently also builds with msvc, so the above is not required.

- Everything has to be run in a 'native tools command prompt' under windows.
