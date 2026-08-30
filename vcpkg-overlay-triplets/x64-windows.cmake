set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)

# This machine's installed MSVC toolset (14.44.35207) emits calls to the
# vectorized STL helper __std_find_first_not_of_trivial_pos_1 (used by
# std::string::find_first_not_of, pulled in via ixwebsocket's URL parsing)
# without exporting it from its own msvcprt/libcpmt libs - a real skew in
# that toolset install, confirmed by grepping every CRT lib on this
# machine. Disabling the vectorized algorithm fast path sidesteps it with
# no semantic change, and needs to apply to every vcpkg-built dependency
# that touches std::string, not just ixwebsocket.
set(VCPKG_CXX_FLAGS "-D_USE_STD_VECTOR_ALGORITHMS=0")
set(VCPKG_C_FLAGS "-D_USE_STD_VECTOR_ALGORITHMS=0")
