#!/usr/bin/env python3
"""Recursively copy and rewrite non-system Mach-O dependencies into an app."""

from __future__ import annotations

import argparse
import collections
import pathlib
import shutil
import stat
import subprocess
import sys


SYSTEM_PREFIXES = ("/usr/lib/", "/System/Library/")


def run(*args: str, check: bool = True) -> str:
    result = subprocess.run(args, check=check, text=True, capture_output=True)
    return result.stdout


def dependencies(binary: pathlib.Path) -> list[str]:
    lines = run("otool", "-L", str(binary)).splitlines()[1:]
    return [line.strip().split(" (", 1)[0] for line in lines if line.strip()]


def dylib_id(binary: pathlib.Path) -> str | None:
    result = subprocess.run(
        ("otool", "-D", str(binary)), text=True, capture_output=True
    )
    lines = result.stdout.splitlines()[1:]
    return lines[0].strip() if result.returncode == 0 and lines else None


def rpaths(binary: pathlib.Path) -> list[str]:
    lines = run("otool", "-l", str(binary)).splitlines()
    found: list[str] = []
    in_rpath = False
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("Load command "):
            in_rpath = False
        elif stripped == "cmd LC_RPATH":
            in_rpath = True
        elif in_rpath and stripped.startswith("path "):
            found.append(stripped[5:].split(" (offset ", 1)[0])
            in_rpath = False
    return found


def ensure_rpath(binary: pathlib.Path, wanted: str) -> None:
    if wanted not in rpaths(binary):
        run("install_name_tool", "-add_rpath", wanted, str(binary))
        print(f"Added LC_RPATH {wanted} to {binary.name}")


class Bundler:
    def __init__(
        self, app: pathlib.Path, search_dirs: list[pathlib.Path]
    ) -> None:
        self.app = app.resolve()
        self.macos = self.app / "Contents" / "MacOS"
        self.frameworks = self.app / "Contents" / "Frameworks"
        self.search_dirs = [path.resolve() for path in search_dirs]
        self.sources: dict[pathlib.Path, pathlib.Path] = {}

    def expand_loader_token(self, value: str, owner: pathlib.Path) -> pathlib.Path:
        if value == "@loader_path":
            return owner.parent
        if value.startswith("@loader_path/"):
            return owner.parent / value.removeprefix("@loader_path/")
        if value == "@executable_path":
            return self.macos
        if value.startswith("@executable_path/"):
            return self.macos / value.removeprefix("@executable_path/")
        return pathlib.Path(value)

    def resolve(self, dependency: str, owner: pathlib.Path) -> pathlib.Path:
        if dependency.startswith("@executable_path") or dependency.startswith(
            "@loader_path"
        ):
            candidate = self.expand_loader_token(dependency, owner)
            if candidate.exists():
                return candidate.resolve()

        if dependency.startswith("@rpath/"):
            suffix = dependency.removeprefix("@rpath/")
            candidates = [self.frameworks / suffix]
            candidates.extend(
                self.expand_loader_token(path, owner) / suffix
                for path in rpaths(owner)
            )
            candidates.extend(path / suffix for path in self.search_dirs)
            candidates.extend(path / pathlib.Path(suffix).name for path in self.search_dirs)
            for candidate in candidates:
                if candidate.exists():
                    return candidate.resolve()

        candidate = pathlib.Path(dependency)
        if candidate.is_absolute() and candidate.exists():
            return candidate.resolve()
        if not candidate.is_absolute() and not dependency.startswith("@"):
            relative = owner.parent / candidate
            if relative.exists():
                return relative.resolve()

        basename = pathlib.Path(dependency).name
        for directory in self.search_dirs:
            fallback = directory / basename
            if fallback.exists():
                return fallback.resolve()

        raise RuntimeError(f"cannot resolve {dependency!r} referenced by {owner}")

    def copy_dependency(self, source: pathlib.Path, name: str) -> pathlib.Path:
        destination = self.frameworks / name
        canonical_source = source.resolve()
        previous = self.sources.get(destination)
        if previous is not None and previous != canonical_source:
            raise RuntimeError(
                f"dependency basename collision for {name}: {previous} and {canonical_source}"
            )
        self.sources[destination] = canonical_source

        if not destination.exists():
            shutil.copy2(canonical_source, destination)
            print(f"Bundled {canonical_source} -> {destination.name}")
        destination.chmod(destination.stat().st_mode | stat.S_IWUSR)
        return destination

    def bundle(self, roots: list[pathlib.Path]) -> None:
        self.frameworks.mkdir(parents=True, exist_ok=True)
        queue = collections.deque(path.resolve() for path in roots)
        visited: set[pathlib.Path] = set()

        while queue:
            owner = queue.popleft()
            if owner in visited:
                continue
            if not owner.is_file():
                raise RuntimeError(f"bundle root does not exist: {owner}")
            visited.add(owner)

            # Every executable root needs a real path to the app Frameworks
            # directory. Framework dylibs also get a loader-relative fallback
            # so their own @rpath dependencies resolve when loaded indirectly.
            if owner.parent == self.macos:
                ensure_rpath(owner, "@executable_path/../Frameworks")
            elif owner.parent == self.frameworks:
                ensure_rpath(owner, "@loader_path")

            own_id = dylib_id(owner)
            if owner.parent == self.frameworks and own_id:
                wanted_id = f"@rpath/{owner.name}"
                if own_id != wanted_id:
                    run("install_name_tool", "-id", wanted_id, str(owner))
                    own_id = wanted_id

            for dependency in dependencies(owner):
                if dependency == own_id or dependency.startswith(SYSTEM_PREFIXES):
                    continue

                source = self.resolve(dependency, owner)
                if source.parent == self.frameworks:
                    destination = source
                else:
                    destination = self.copy_dependency(
                        source, pathlib.Path(dependency).name
                    )

                rewritten = f"@rpath/{destination.name}"
                if dependency != rewritten:
                    run(
                        "install_name_tool",
                        "-change",
                        dependency,
                        rewritten,
                        str(owner),
                    )
                queue.append(destination)

        print(f"Bundled dependency graph for {len(roots)} root Mach-O file(s).")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--app", required=True, type=pathlib.Path)
    parser.add_argument(
        "--search-dir", action="append", default=[], type=pathlib.Path
    )
    parser.add_argument("roots", nargs="+", type=pathlib.Path)
    args = parser.parse_args()

    try:
        Bundler(args.app, args.search_dir).bundle(args.roots)
    except (RuntimeError, subprocess.CalledProcessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
