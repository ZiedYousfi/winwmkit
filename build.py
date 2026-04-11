#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT_DIR = Path(__file__).resolve().parent
SRC_DIR = ROOT_DIR / "src"
INCLUDE_DIR = ROOT_DIR / "include"
BUILD_DIR = ROOT_DIR / "build"
LIB_BUILD_DIR = BUILD_DIR / "lib"
COMPDB_PATH = ROOT_DIR / "compile_commands.json"
WIN32_LIBS = ["user32.lib", "kernel32.lib", "ole32.lib"]


def run(command: list[str], cwd: Path = ROOT_DIR) -> None:
    print(subprocess.list2cmdline(command))
    subprocess.run(command, cwd=str(cwd), check=True)


def require_tool(name: str) -> None:
    if shutil.which(name) is None:
        raise SystemExit(
            f"{name} was not found in PATH. Run this script from a Developer PowerShell "
            "or a Visual Studio command prompt."
        )


def normalize_sanitizers(args: argparse.Namespace) -> str:
    if args.no_sanitizers:
        return ""
    return args.sanitizers.strip()


def compiler_flags(sanitizers: str) -> list[str]:
    flags = [f"/I{INCLUDE_DIR}", "/nologo", "/W4"]
    if sanitizers:
        flags.extend(["/MD", f"/fsanitize={sanitizers}", "/Zi", "/Od"])
    return flags


def library_sources() -> list[Path]:
    return sorted(SRC_DIR.glob("*.c"))


def object_path(source: Path) -> Path:
    return LIB_BUILD_DIR / f"{source.stem}.obj"


def target_arch() -> str:
    return os.environ.get("VSCMD_ARG_TGT_ARCH", "unknown")


def stamp_path(arch: str) -> Path:
    return LIB_BUILD_DIR / f".arch-{arch}.stamp"


def ensure_library_build_dir(arch: str) -> None:
    LIB_BUILD_DIR.mkdir(parents=True, exist_ok=True)
    active_stamp = stamp_path(arch)
    if active_stamp.exists():
        return

    for pattern in ("*.obj", "*.lib", "*.exp", "*.dll", "*.pdb"):
        for path in LIB_BUILD_DIR.glob(pattern):
            path.unlink()
    for old_stamp in LIB_BUILD_DIR.glob(".arch-*.stamp"):
        old_stamp.unlink()
    active_stamp.write_text(f"{arch}\n", encoding="ascii")


def build_static_library(sanitizers: str) -> None:
    require_tool("cl.exe")
    require_tool("lib.exe")

    arch = target_arch()
    ensure_library_build_dir(arch)
    sources = library_sources()
    if not sources:
        raise SystemExit("No library sources were found under src/.")

    flags = compiler_flags(sanitizers)
    objects: list[Path] = []
    for source in sources:
        output = object_path(source)
        run(["cl.exe", *flags, "/c", str(source), f"/Fo{output}"])
        objects.append(output)

    run(["lib.exe", "/nologo", f"/OUT:{LIB_BUILD_DIR / 'winwmkit.lib'}", *map(str, objects)])


def build_shared_library(sanitizers: str) -> None:
    require_tool("cl.exe")

    arch = target_arch()
    ensure_library_build_dir(arch)
    sources = library_sources()
    if not sources:
        raise SystemExit("No library sources were found under src/.")

    flags = compiler_flags(sanitizers)
    run(
        [
            "cl.exe",
            *flags,
            "/DWWMK_BUILD_DLL",
            "/LD",
            *map(str, sources),
            f"/Fe{LIB_BUILD_DIR / 'winwmkit.dll'}",
            "/link",
            *WIN32_LIBS,
            f"/IMPLIB:{LIB_BUILD_DIR / 'winwmkit_dll.lib'}",
        ]
    )


def compile_db_entries(sanitizers: str) -> list[dict[str, object]]:
    flags = compiler_flags(sanitizers)
    entries: list[dict[str, object]] = []

    for source in library_sources():
        output = object_path(source)
        entries.append(
            {
                "directory": str(ROOT_DIR),
                "arguments": ["cl.exe", *flags, "/c", str(source), f"/Fo{output}"],
                "file": str(source),
                "output": str(output),
            }
        )

    example_source = ROOT_DIR / "exemple" / "main.c"
    if example_source.exists():
        example_output = ROOT_DIR / "exemple" / "build" / "main.obj"
        entries.append(
            {
                "directory": str(example_source.parent),
                "arguments": [
                    "cl.exe",
                    *flags,
                    "/c",
                    str(example_source),
                    f"/Fo{example_output}",
                ],
                "file": str(example_source),
                "output": str(example_output),
            }
        )

    return entries


def write_compile_commands(sanitizers: str) -> None:
    entries = compile_db_entries(sanitizers)
    COMPDB_PATH.write_text(json.dumps(entries, indent=2), encoding="utf-8")
    print(f"Wrote {COMPDB_PATH}")


def clean() -> None:
    if BUILD_DIR.exists():
        shutil.rmtree(BUILD_DIR)
    if COMPDB_PATH.exists():
        COMPDB_PATH.unlink()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build WinWMKit with MSVC.")
    parser.add_argument(
        "target",
        nargs="?",
        default="all",
        choices=["all", "build", "static", "shared", "compdb", "clangd", "clean"],
        help="Build target to execute.",
    )
    parser.add_argument(
        "--sanitizers",
        default="address",
        help="Sanitizer set passed to cl.exe, for example 'address'.",
    )
    parser.add_argument(
        "--no-sanitizers",
        action="store_true",
        help="Build without sanitizer flags.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    sanitizers = normalize_sanitizers(args)

    if args.target == "clean":
        clean()
        return 0

    if args.target in {"all", "build", "static"}:
        build_static_library(sanitizers)

    if args.target in {"all", "build", "shared"}:
        build_shared_library(sanitizers)

    if args.target in {"all", "compdb", "clangd"}:
        write_compile_commands(sanitizers)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
