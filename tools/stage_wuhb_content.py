import argparse
import shutil
import sys
from pathlib import Path


def parse_args():
    parser = argparse.ArgumentParser(
        description="Stage a WUHB content directory for gx2gl builds."
    )
    parser.add_argument("--template-dir", required=True, help="Template content directory.")
    parser.add_argument("--output-dir", required=True, help="Output staging directory.")
    parser.add_argument(
        "--compiler-rpl",
        help="Optional glslcompiler.rpl path to copy into the content root.",
    )
    return parser.parse_args()


def main():
    args = parse_args()
    template_dir = Path(args.template_dir).resolve()
    output_dir = Path(args.output_dir).resolve()

    if not template_dir.is_dir():
        print(f"Template content directory does not exist: {template_dir}", file=sys.stderr)
        return 1

    if output_dir.exists():
        shutil.rmtree(output_dir)
    output_dir.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(template_dir, output_dir)

    placeholder = output_dir / "_copy_glslcompiler.rpl_here.txt"
    bundled_compiler = output_dir / "glslcompiler.rpl"
    if bundled_compiler.is_file() and placeholder.exists():
        placeholder.unlink()

    if args.compiler_rpl:
        compiler_rpl = Path(args.compiler_rpl).resolve()
        if not compiler_rpl.is_file():
            print(f"glslcompiler.rpl was not found: {compiler_rpl}", file=sys.stderr)
            return 1

        shutil.copy2(compiler_rpl, bundled_compiler)
        if placeholder.exists():
            placeholder.unlink()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
