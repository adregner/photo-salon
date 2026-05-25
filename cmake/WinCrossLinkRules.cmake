# Override CMake's vs_link_exe manifest-embedding wrapper for cross-compilation.
# mt.exe and rc.exe are not available on macOS; call lld-link directly instead.
# Windows will supply a default manifest at runtime when none is embedded.
set(CMAKE_CXX_LINK_EXECUTABLE
    "<CMAKE_LINKER> /nologo <OBJECTS> /out:<TARGET> /implib:<TARGET_IMPLIB> /pdb:<TARGET_PDB> <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> <LINK_LIBRARIES>")
set(CMAKE_C_LINK_EXECUTABLE
    "<CMAKE_LINKER> /nologo <OBJECTS> /out:<TARGET> /implib:<TARGET_IMPLIB> /pdb:<TARGET_PDB> <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> <LINK_LIBRARIES>")
