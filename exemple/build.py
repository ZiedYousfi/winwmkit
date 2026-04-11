#!/usr/bin/env python3
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

EXAMPLE_DIR = Path(__file__).resolve().parent
ROOT_DIR = EXAMPLE_DIR.parent
BUILD_DIR = EXAMPLE_DIR / "build"
INCLUDE_DIR = ROOT_DIR / "include"
STATIC_LIB = ROOT_DIR / "build" / "lib" / "winwmkit.lib"
WIN32_LIBS = ["user32.lib", "ole32.lib"]


def run(command: list[str], cwd: Path = EXAMPLE_DIR) -> None:
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


def root_build_command(target: str, sanitizers: str) -> list[str]:
    command = [sys.executable, str(ROOT_DIR / "build.py"), target]
    if sanitizers:
        command.extend(["--sanitizers", sanitizers])
    else:
        command.append("--no-sanitizers")
    return command


def ensure_static_library(sanitizers: str) -> None:
    run(root_build_command("static", sanitizers), cwd=ROOT_DIR)


def build_example(sanitizers: str) -> None:
    require_tool("cl.exe")
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    ensure_static_library(sanitizers)

    flags = compiler_flags(sanitizers)
    object_file = BUILD_DIR / "main.obj"
    executable = BUILD_DIR / "winwmkit_example.exe"
    source = EXAMPLE_DIR / "main.c"

    run(["cl.exe", *flags, "/c", str(source), f"/Fo{object_file}"])
    run(
        [
            "cl.exe",
            *flags,
            str(object_file),
            f"/Fe{executable}",
            "/link",
            str(STATIC_LIB),
            *WIN32_LIBS,
        ]
    )


def clean() -> None:
    if BUILD_DIR.exists():
        shutil.rmtree(BUILD_DIR)
    compdb = ROOT_DIR / "compile_commands.json"
    if compdb.exists():
        compdb.unlink()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build the WinWMKit example with MSVC.")
    parser.add_argument(
        "target",
        nargs="?",
        default="all",
        choices=["all", "compdb", "clangd", "clean"],
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

    if args.target == "all":
        build_example(sanitizers)
        run(root_build_command("compdb", sanitizers), cwd=ROOT_DIR)
        return 0

    run(root_build_command("compdb", sanitizers), cwd=ROOT_DIR)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
