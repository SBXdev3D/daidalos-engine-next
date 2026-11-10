# ADR-0038: Releases

- **Status:** Accepted
- **Date:** 2026-11-10

## Context

The sandbox could only be run from a build tree. Sharing a version of the demo needs an archive that starts on a machine without the repository, and a repeatable way to produce it.

## Decision

- **A tag `vX.Y.Z` releases.** `.github/workflows/release.yml` runs on that tag. It first checks that `project(VERSION)` in `CMakeLists.txt`, `version-string` in `vcpkg.json` and the tag agree, and stops if they do not. The version is bumped by hand in both files; CI never writes commits.
- **Windows and Linux archives.** The workflow builds `windows-msvc-shipping` on a Windows runner and `linux-clang-shipping` on Ubuntu 24.04, packages each with `cpack --preset <name>`, and attaches `daidalos-engine-next-<version>-windows-x64.zip`, `…-linux-x64.tar.gz` and `SHA256SUMS.txt` to the release.
- **Draft releases, published by hand.** The workflow creates a draft with notes generated from the merged pull requests. Nothing becomes visible to anyone outside the repository's collaborators until the draft is reviewed and published.
- **Shipping presets:** Release, without the developer tools (`DAI_ENABLE_DEV_TOOLS=OFF`) or the tests. The executables are self-contained: the vcpkg libraries are linked statically and the shaders are compiled into them.
- **Packaging lives in CMake**, in `install()` rules and `cmake/DaidalosPackaging.cmake`, so the archive is the same when built locally. Its root holds the executable, `daidalos.cfg`, `sandbox.cfg`, a player's `README.txt`, `LICENSE`, `assets/Factory/` and `assets/Fonts/` with their licences, and `THIRD_PARTY_NOTICES/` with the licence of every library vcpkg built.
- **No tests in the release workflow.** Pull request CI ran them on the commit being tagged (ADR-0014), and GitHub's Windows runners have no GPU.
- **A dry run** is the same workflow started by hand: it builds both archives and keeps them as workflow artifacts, without creating a release.

## Consequences

- Each release costs one Windows and one Linux build. Windows minutes are billed double on a private repository, and a build without a vcpkg cache takes much longer; the binary cache is kept per preset as in `ci.yml`.
- The Linux archive needs a C library at least as recent as Ubuntu 24.04's.
- The archive includes the Factory's free textures and sounds. Their source and exact licences are not recorded yet (`assets/Factory/LICENSE.md`): they must be checked before a release is published outside the repository's collaborators.

## Alternatives considered

- Copying files into the archive from the workflow: the archive could then only be produced in CI, and would drift from the build.
- Publishing releases automatically: a release is outward-facing, and the generated notes and the archives deserve a look first.
- Running the tests again in the release workflow: they already ran on the commit, and the Windows runner could not run the GPU tests.
- An installer (NSIS, WiX) or Linux packages (.deb, AppImage): an archive is enough for a demo, and needs nothing installed.
