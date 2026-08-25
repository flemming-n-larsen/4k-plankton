# in4k repository instructions

## Project

This repository develops a native PC in4k intro: a Windows executable intended to fit within a 4,096-byte competition limit.

The current bootstrap is a CRT-free Win32/x86 OpenGL rotating triangle. Audio is intentionally deferred and must remain behind the planned audio abstraction.

## Build workflow

- Use the repository-owned `build.ps1`; do not require Visual Studio or Visual Studio Code.
- Run `pwsh -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -VerifyTools` before building when the toolchain may have changed.
- Build both configurations before committing:

  ```powershell
  .\build.ps1 -Configuration Debug
  .\build.ps1 -Configuration Compo
  ```

- The Compo build must use the locked Crinkler version and must remain at or below 4,096 bytes.
- Keep compiler and Crinkler commands visible in build output.
- Do not rely on a preconfigured developer prompt or IDE state.

## Toolchain and reproducibility

- Treat `tools.lock.json` as authoritative for required tool versions and hashes.
- Do not silently upgrade MSVC, the Windows SDK, Crinkler, PowerShell, or other size-sensitive tools.
- Any tool upgrade requires rebuilding Debug and Compo, comparing executable size and hashes, and updating the lockfile deliberately.
- `.tools/` contains local third-party tools and must not be committed.
- `out/` contains generated build artifacts and must not be committed.

## Code guidelines

- Keep the Compo path free of CRT, standard-library, audio, and unnecessary runtime dependencies unless explicitly justified.
- Prefer Win32/OpenGL and small, direct implementations while the bootstrap milestone is being extended.
- Preserve clean boundaries between platform, graphics, audio, generated data, and build/packaging code.
- Do not add CMake, an IDE project, or an external framework without updating the architecture documentation first.

## Documentation

- Update `README.md` when build or run commands change.
- Update `docs/toolchain.md` when installation, version, or reproducibility behavior changes.
- Update `docs/architecture.md` when a subsystem, milestone, graphics approach, audio approach, or build decision changes.

## Validation

- Run `git diff --check` before committing.
- Run the Debug executable to check runtime behavior.
- Run the Compo build and verify the reported byte size and generated report.
- Keep generated binaries, reports, and local installations out of version control.
