from __future__ import annotations

import argparse
import os
import pathlib
import shlex
import subprocess
import sys


def windows_to_wsl(path: pathlib.Path) -> str:
    resolved = path.resolve()
    drive = resolved.drive.rstrip(":").lower()
    rest = resolved.as_posix().split(":", 1)[1]
    return f"/mnt/{drive}{rest}"


def build_command(args: argparse.Namespace) -> list[str]:
    compiler = args.compiler.resolve()
    vertex = args.vertex.resolve()
    pixel = args.pixel.resolve()
    output = args.output.resolve()

    if os.name == "nt":
        cmd = [
            windows_to_wsl(compiler),
            "-vs",
            windows_to_wsl(vertex),
            "-ps",
            windows_to_wsl(pixel),
            "-o",
            windows_to_wsl(output),
        ]
        if args.verbose:
            cmd.append("-v")
        return ["wsl", "sh", "-lc", " ".join(shlex.quote(part) for part in cmd)]

    cmd = [str(compiler), "-vs", str(vertex), "-ps", str(pixel), "-o", str(output)]
    if args.verbose:
        cmd.append("-v")
    return cmd


def write_header(blob: bytes, header_path: pathlib.Path) -> None:
    header_path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "#pragma once",
        "",
        "static const unsigned char kSmokeTriangleGsh[] = {",
    ]
    for index in range(0, len(blob), 12):
        chunk = blob[index:index + 12]
        lines.append("    " + ", ".join(f"0x{byte:02X}" for byte in chunk) + ",")
    lines.extend(
        [
            "};",
            "",
            "static const unsigned int kSmokeTriangleGshSize = sizeof(kSmokeTriangleGsh);",
            "",
        ]
    )
    header_path.write_text("\n".join(lines), encoding="ascii")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Compile the embedded GX2GL smoke shaders to a .gsh blob and header."
    )
    root = pathlib.Path(__file__).resolve().parents[1]
    parser.add_argument(
        "--compiler",
        type=pathlib.Path,
        default=root / "third_party" / "cafeglsl" / "glslcompiler.elf",
    )
    parser.add_argument(
        "--vertex",
        type=pathlib.Path,
        default=root / "tests" / "shaders" / "smoke_triangle.vs",
    )
    parser.add_argument(
        "--pixel",
        type=pathlib.Path,
        default=root / "tests" / "shaders" / "smoke_triangle.ps",
    )
    parser.add_argument(
        "--output",
        type=pathlib.Path,
        default=root / "tests" / "generated" / "smoke_triangle.gsh",
    )
    parser.add_argument(
        "--header",
        type=pathlib.Path,
        default=root / "tests" / "generated" / "smoke_triangle_gsh.inc",
    )
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    args.output.parent.mkdir(parents=True, exist_ok=True)
    command = build_command(args)
    result = subprocess.run(command, cwd=root)
    if result.returncode != 0:
        return result.returncode

    blob = args.output.read_bytes()
    if not blob:
        print("Smoke shader compiler produced an empty .gsh file.", file=sys.stderr)
        return 1

    write_header(blob, args.header)
    print(f"Wrote {args.output}")
    print(f"Wrote {args.header}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
