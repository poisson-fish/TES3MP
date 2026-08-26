# Android ARM64 feasibility assessment

Date assessed: 2026-08-26

Android ARM64 is not a primary-platform gate for ADR-0005. Standalone Meta Quest
3 remains a later conditional stretch target.

The exact OpenSSL 3.5.8 source supports Android ARM64 through its documented NDK
targets, and the pinned Protobuf/Abseil sources are CMake-based and can be
cross-compiled without a host runtime dependency in the game process.
GameNetworkingSockets 1.6.0 contains Android-relevant platform code, but its
stock top-level CMake platform selection does not recognize
`CMAKE_SYSTEM_NAME=Android`; it recognizes Linux, Windows, Darwin, FreeBSD, and
OpenBSD and otherwise stops configuration.

Result: **source-feasible with an unresolved upstream build-system gate**. The
approved no-patch selection proof does not modify that CMake logic, and Android
is not added to the supported matrix. Before any standalone Android/Quest slice,
the project must re-evaluate a newer unmodified GameNetworkingSockets release or
present an owner-reviewed build-integration option. This assessment authorizes
no dependency patch and changes no desktop transport decision.
