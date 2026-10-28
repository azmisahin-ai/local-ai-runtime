#!/usr/bin/env python3
"""Create a relocatable macOS tarball by bundling Homebrew dylib dependencies.

System-provided libraries remain linked from macOS. Any non-system dependency
that cannot be resolved from the current Homebrew toolchain fails closed rather
than producing an artifact that only works on the build runner.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import tarfile
from pathlib import Path

BREW_ROOTS = (Path("/opt/homebrew"), Path("/usr/local"))
SYSTEM_PREFIXES = ("/System/Library/", "/usr/lib/", "/Library/Apple/System/Library/")
SYSTEM_NAMES = (
    "libSystem.", "libobjc.", "libc++.", "libc++abi.", "libresolv.", "libz.",
    "libm.", "libiconv.", "libcompression.", "libsqlite3.", "libxml2.",
    "libcharset.", "libncurses.", "libutil.", "libpthread.",
)


def run(*args: str) -> str:
    result = subprocess.run(args, check=True, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return result.stdout


def otool_dependencies(image: Path) -> list[str]:
    lines = run("otool", "-L", str(image)).splitlines()[1:]
    result = []
    for line in lines:
        value = line.strip().split(" (compatibility version", 1)[0]
        if value:
            result.append(value)
    return result


def otool_rpaths(image: Path) -> list[str]:
    lines = run("otool", "-l", str(image)).splitlines()
    result: list[str] = []
    for index, line in enumerate(lines):
        if line.strip() == "cmd LC_RPATH":
            for candidate in lines[index + 1:index + 5]:
                stripped = candidate.strip()
                if stripped.startswith("path "):
                    result.append(stripped[5:].split(" (offset", 1)[0])
                    break
    return result


def under_brew_root(path: Path) -> bool:
    resolved = path.resolve()
    for root in BREW_ROOTS:
        try:
            resolved.relative_to(root)
            return True
        except ValueError:
            pass
    return False


def brew_candidates(name: str):
    for root in BREW_ROOTS:
        direct = root / "lib" / name
        if direct.exists():
            yield direct
        for pattern in (f"opt/*/lib/{name}", f"Cellar/*/*/lib/{name}"):
            for candidate in root.glob(pattern):
                if candidate.exists():
                    yield candidate


def resolve_dependency(dep: str, image: Path, stage: Path) -> tuple[Path | None, str | None]:
    if dep.startswith(SYSTEM_PREFIXES):
        return None, dep
    candidate_strings: list[str] = []
    if dep.startswith("@loader_path/"):
        candidate_strings.append(str(image.parent / dep.removeprefix("@loader_path/")))
    elif dep.startswith("@executable_path/"):
        candidate_strings.append(str(stage / dep.removeprefix("@executable_path/")))
    elif dep.startswith("@rpath/"):
        name = Path(dep).name
        for rpath in otool_rpaths(image):
            expanded = rpath.replace("@loader_path", str(image.parent)).replace("@executable_path", str(stage))
            candidate_strings.append(str(Path(expanded) / name))
    else:
        candidate_strings.append(dep)

    for raw in candidate_strings:
        candidate = Path(raw)
        if candidate.exists() and under_brew_root(candidate):
            return candidate.resolve(), None
    for candidate in brew_candidates(Path(dep).name):
        if under_brew_root(candidate):
            return candidate.resolve(), None

    # Homebrew packages sometimes use @rpath for a dylib supplied by macOS.
    # Convert that reference to its stable system path rather than leaving an
    # @rpath reference that no longer resolves after the app is relocated.
    if dep.startswith("@rpath/") and any(Path(dep).name.startswith(name) for name in SYSTEM_NAMES):
        system_candidate = Path("/usr/lib") / Path(dep).name
        if system_candidate.exists():
            return None, str(system_candidate)

    # The toolchain should only require system dylibs or Homebrew libraries.
    # A missing or non-system dependency is a packaging defect, not a warning.
    return None, None


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def replace_rpaths(image: Path, desired: str) -> None:
    current = otool_rpaths(image)
    for value in current:
        if value != desired:
            run("install_name_tool", "-delete_rpath", value, str(image))
    if desired not in current:
        run("install_name_tool", "-add_rpath", desired, str(image))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--web-dir", required=True, type=Path)
    parser.add_argument("--config", required=True, type=Path)
    parser.add_argument("--license", required=True, type=Path)
    parser.add_argument("--launcher", required=True, type=Path)
    parser.add_argument("--stage", required=True, type=Path)
    parser.add_argument("--package", required=True, type=Path)
    parser.add_argument("--version", required=True)
    parser.add_argument("--arch", required=True, choices=("x64", "arm64"))
    args = parser.parse_args()

    stage: Path = args.stage.resolve()
    if stage.exists():
        shutil.rmtree(stage)
    (stage / "web").mkdir(parents=True)
    (stage / "config").mkdir()
    (stage / "scripts").mkdir()
    lib_dir = stage / "lib"
    lib_dir.mkdir()

    runtime = stage / "local-ai-runtime"
    shutil.copy2(args.binary, runtime)
    shutil.copytree(args.web_dir, stage / "web", dirs_exist_ok=True)
    shutil.copy2(args.config, stage / "config" / "engines.example.json")
    shutil.copy2(args.license, stage / "LICENSE")
    launcher = stage / "scripts" / "start-runtime.sh"
    shutil.copy2(args.launcher, launcher)
    launcher.chmod(launcher.stat().st_mode | 0o111)
    runtime.chmod(runtime.stat().st_mode | 0o111)

    images: list[Path] = [runtime]
    queue = [runtime]
    patches: dict[Path, dict[str, str]] = {}
    destinations: dict[str, Path] = {}
    formulas: dict[str, Path] = {}
    while queue:
        image = queue.pop(0)
        if image in patches:
            continue
        image_patches: dict[str, str] = {}
        for dep in otool_dependencies(image):
            if dep.startswith(SYSTEM_PREFIXES):
                continue
            resolved, system_replacement = resolve_dependency(dep, image, stage)
            if system_replacement is not None:
                if system_replacement != dep:
                    image_patches[dep] = system_replacement
                continue
            if resolved is None or not resolved.exists():
                raise RuntimeError(f"Cannot resolve non-system dylib dependency {dep!r} referenced by {image}")
            name = Path(dep).name
            destination = lib_dir / name
            previous = destinations.get(name)
            if previous is not None:
                if sha256(previous) != sha256(resolved):
                    raise RuntimeError(f"Two different dylibs use the same install name {name!r}: {previous} and {resolved}")
            else:
                shutil.copy2(resolved, destination)
                destinations[name] = destination
                images.append(destination)
                queue.append(destination)
            if len(resolved.parents) > 2 and resolved.parents[2].parent.name == "Cellar":
                formulas[resolved.parents[2].name] = resolved.parents[1]
            image_patches[dep] = f"@rpath/{name}"
        patches[image] = image_patches

    # Patch all install names after discovering the complete transitive closure.
    for image, image_patches in patches.items():
        for original, replacement in image_patches.items():
            if original != replacement:
                run("install_name_tool", "-change", original, replacement, str(image))
        if image != runtime:
            run("install_name_tool", "-id", f"@rpath/{image.name}", str(image))
            replace_rpaths(image, "@loader_path")
        else:
            replace_rpaths(image, "@executable_path/lib")

    # Include license metadata and any license/copyright texts shipped in the
    # Homebrew formula prefixes for the dylibs redistributed with this package.
    third_party_dir = stage / "THIRD-PARTY-LICENSES"
    third_party_dir.mkdir()
    notice_lines = [
        "Third-party dynamic libraries bundled with Local AI Runtime.",
        "Full license/copyright files are copied below when the upstream formula ships them.",
        "System-provided macOS libraries are not bundled by this package.",
        "",
    ]
    for formula, prefix in sorted(formulas.items()):
        license_id = "metadata unavailable"
        upstream = ""
        try:
            formula_info = json.loads(run("brew", "info", "--json=v2", formula))
            info = formula_info.get("formulae", [{}])[0]
            license_id = str(info.get("license") or license_id)
            urls = info.get("urls", {})
            upstream = str(urls.get("stable") or "")
        except (subprocess.CalledProcessError, json.JSONDecodeError, IndexError, AttributeError):
            pass
        version = prefix.name
        notice_lines.append(f"- {formula} {version}: license {license_id}" + (f"; upstream {upstream}" if upstream else ""))
        formula_license_dir = third_party_dir / formula
        copied = 0
        candidates: list[Path] = []
        for subdir in (prefix / "share" / "licenses", prefix / "share" / "doc"):
            if subdir.exists():
                candidates.extend(item for item in subdir.rglob("*") if item.is_file())
        candidates.extend(item for item in prefix.iterdir() if item.is_file() and item.name.lower().startswith(("license", "copying", "copyright", "notice")))
        for source in sorted(set(candidates)):
            lowered = source.name.lower()
            if not any(token in lowered for token in ("license", "copying", "copyright", "notice", "patent")):
                continue
            try:
                if source.stat().st_size > 2 * 1024 * 1024:
                    continue
                relative = source.relative_to(prefix)
                target = formula_license_dir / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, target)
                copied += 1
            except (OSError, ValueError):
                continue
        if copied == 0:
            notice_lines.append("  No standalone license file was found in the installed formula prefix; verify upstream notices before redistribution.")
    (third_party_dir / "NOTICE.txt").write_text("\n".join(notice_lines) + "\n", encoding="utf-8")

    # Ad-hoc signatures are local integrity markers only; this is not notarization.
    for image in sorted((item for item in images if item != runtime), key=lambda item: item.name):
        run("codesign", "--force", "--sign", "-", "--timestamp=none", str(image))
    run("codesign", "--force", "--sign", "-", "--timestamp=none", str(runtime))

    readme = stage / "README-INSTALL.txt"
    readme.write_text(
        f"Local AI Runtime {args.version} — macOS {args.arch}\n\n"
        "Run: ./scripts/start-runtime.sh\n\n"
        "The package includes the runtime, its Homebrew dynamic-library dependencies, and\n"
        "third-party license notices. macOS system libraries remain an operating-system requirement.\n"
        "It does not include\n"
        "AI engines or model weights; install those from the local dashboard.\n\n"
        "This command-line preview is ad-hoc signed, not Developer ID signed or notarized.\n",
        encoding="utf-8",
    )

    args.package.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(args.package, "w:gz") as archive:
        for child in sorted(stage.iterdir()):
            archive.add(child, arcname=child.name, recursive=True)

    # Fail the packaging job if the binary still refers to Homebrew absolute paths.
    for image in [runtime, *sorted(lib_dir.iterdir())]:
        output = run("otool", "-L", str(image))
        bad = [line.strip() for line in output.splitlines()[1:] if "/opt/homebrew/" in line or "/usr/local/Cellar/" in line or "/usr/local/opt/" in line]
        if bad:
            raise RuntimeError(f"Non-relocatable Homebrew dylib references remain in {image}: {bad}")
    print(f"Packaged {args.package} ({args.arch}); bundled dylibs: {len(destinations)}")


if __name__ == "__main__":
    main()
