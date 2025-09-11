# Contributing to Wolf

Thank you for your interest in contributing to Wolf! This guide will help you get started and ensure your contributions can be merged smoothly.

## Quick Start

1. Fork the repository
2. Create a feature branch: `git checkout -b my-feature`
3. Make your changes following the guidelines below
4. Run `./format.sh` to format your code
5. Test your changes thoroughly
6. Submit a pull request with a conventional commit title

## Conventional Commits

**Your PR title should follow [Conventional Commits](https://www.conventionalcommits.org/) format** for automated releases to work properly. It can also be helpful to write individual commit messages Conventionally, if a commit is atomic and stands alone, but this is not strictly necessary.

## Development Setup

### Prerequisites
- CMake 3.21+ (can be installed via VC2022)
- A C++23 compiler
- Vcpkg (can be installed via VC2022)
- Ninja (can be installed via VC2022)
- Python 3.x with PyYAML (`pip install PyYAML`) if generating new gamestate headers
- clang-format

### Building
```bash
# Configure
cmake --preset x64-clang-debug

# Build
cmake --build build/x64-clang-debug
```

### Code Formatting
Always run the formatter before submitting (in Git Bash if on Windows). This helps simplify merges.
```bash
./format.sh
```

## Project Structure

- `src/api/` - Source of the API headers for mods to use. These get flattened into the distributed `wolf_framework.hpp`.
- `src/runtime/` - Core runtime (dinput8.dll)
- `src/devtools/` - Devtools / trainer mod for exploring Okami's memory space
- `src/loader/` - Injector and bootstrapper for WOLF
- `include/okami/` - Game-specific headers, describing various types and structs Okami uses.
- `scripts/` - Build and generation scripts
- `cmake/` - CMake modules and functions

## Guidelines

## Release Process

This project uses [release-please](https://github.com/googleapis/release-please) for automated releases:

- Conventional commit messages automatically generate changelogs
- Version bumps are calculated from commit types:
  - `feat:` → minor version bump
  - `fix:` → patch version bump
  - `feat!:` or `fix!:` → major version bump
- Release PRs are automatically created or updated when PRs are merged to master. These special PRs track the outstanding changes slated for the next release.

---

Thank you for contributing to Wolf! Your help makes this project better for everyone.
