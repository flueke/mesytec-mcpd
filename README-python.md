# To build a wheel
pip wheel -w dist .[gui]
pipx install dist\mesytec_mcpd_py-0.6.1.dev35-cp314-cp314-win_amd64.whl[gui]

# Editable dev installation
pip install -e .[gui,dev]

# Notes
- To use clang-cl set CMAKE_GENERATOR=Ninja and set both CC and CXX to the full path to clang-cl, e.g.
  set CMAKE_GENERATOR=Ninja
  set CXX=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-cl.exe
  set CC=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-cl.exe

- 'Ninja' could also be set in pyproject.toml under [tool.scikit-build] cmake.args .

- The code currently also builds with msvc, so the above is not required.

- To use Ninja and clang(-cl) CMAKE_GENERATOR and CMAKE_CXX_COMPILER have to be
  set in the env. Figure out if this can be done without having to modify the env.
- CMAKE_CXX_COMPILER has to be the full path to the compiler.
- Everything has to be run in a 'native tools command prompt'.
