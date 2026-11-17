#!/usr/bin/env python3
import argparse
import glob
import mmap
import os
import platform
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GODOT_TAG = "4.7.2-stable"
GODOT_REMOTE = "https://github.com/godotengine/godot.git"
PATCH_DIRECTORY = ROOT / "patches" / GODOT_TAG
DEFAULT_ENGINE = ROOT.parent / "godot-4.7"
GENERATED_DIRECTORY = Path(".godot") / "grit" / "generated"
FINGERPRINT_FILE = Path(".godot") / "grit" / "fingerprint"
VM_FALLBACK_MESSAGE = "using the GDScript VM"
STRIPPED_MESSAGE = "scripts of this game were stripped at export"
SCRIPT_FAILURE_MESSAGES = ("Parse Error", "Compile Error", "Failed to load script")
CONFORMANCE_DIRECTORY = ROOT / "build" / "conformance" / "generated"
CONFORMANCE_BUILD_ARGUMENTS = ["tests=yes", "grit_conformance=yes", "extra_suffix=conformance"]
GAME_TIMEOUT_SECONDS = 300
SANITIZER_OPTIONS = ("use_asan", "use_ubsan", "use_lsan", "use_tsan", "use_msan")


def run(command, cwd=None):
    print("$ " + " ".join(str(part) for part in command), flush=True)
    subprocess.run([str(part) for part in command], cwd=cwd, check=True)


def host_platform():
    if sys.platform.startswith("linux"):
        return "linuxbsd"
    if sys.platform == "win32":
        return "windows"
    if sys.platform == "darwin":
        return "macos"
    sys.exit(f"Unsupported host platform: {sys.platform}")


def host_architecture():
    machine = platform.machine().lower()
    if machine in ("amd64", "x86_64"):
        return "x86_64"
    if machine in ("arm64", "aarch64"):
        return "arm64"
    return machine


def scons_value(scons_arguments, name):
    value = ""
    for argument in scons_arguments:
        key, separator, current = argument.partition("=")
        if separator and key == name:
            value = current
    return value


def is_enabled(value):
    return value.lower() in ("yes", "true", "y", "1")


def binary_suffix(scons_arguments):
    suffix = ".llvm" if is_enabled(scons_value(scons_arguments, "use_llvm")) else ""
    extra_suffix = scons_value(scons_arguments, "extra_suffix")
    if extra_suffix:
        suffix += "." + extra_suffix
    if any(is_enabled(scons_value(scons_arguments, name)) for name in SANITIZER_OPTIONS):
        suffix += ".san"
    return suffix


def binary_path(engine, target, scons_arguments=()):
    suffix = ".exe" if sys.platform == "win32" else ""
    return engine / "bin" / f"godot.{host_platform()}.{target}.{host_architecture()}{binary_suffix(scons_arguments)}{suffix}"


def get_binary(arguments):
    binary = binary_path(Path(arguments.engine).resolve(), arguments.target, arguments.scons_arguments)
    if not binary.exists():
        sys.exit(f"{binary} does not exist. Run: python tools/grit.py build {arguments.target}")
    sys.exit(binary)

def find_binary(engine, target, scons_arguments=()):
    binary = binary_path(engine, target, scons_arguments)
    if not binary.exists():
        sys.exit(f"{binary} does not exist. Run: python tools/grit.py build {target}")
    return binary


def setup(arguments):
    engine = Path(arguments.engine).resolve()
    if not engine.exists():
        if arguments.godot_repository:
            run(["git", "-C", arguments.godot_repository, "worktree", "add", "--detach", engine, GODOT_TAG])
        else:
            run(["git", "clone", "--depth", "1", "--branch", GODOT_TAG, GODOT_REMOTE, engine])

    for patch in sorted(PATCH_DIRECTORY.glob("*.patch")):
        already_applied = subprocess.run(
            ["git", "-C", str(engine), "apply", "--reverse", "--check", str(patch)], capture_output=True
        )
        if already_applied.returncode == 0:
            print(f"Patch already applied: {patch.name}")
            continue
        run(["git", "-C", engine, "apply", patch])


def scons_command():
    import importlib.util

    if sys.version_info >= (3, 9) and importlib.util.find_spec("SCons") is not None:
        return [sys.executable, "-m", "SCons"]
    return ["scons"]


def build(arguments):
    command = [
        *scons_command(),
        f"platform={host_platform()}",
        f"target={arguments.target}",
        f"custom_modules={ROOT / 'modules'}",
        f"-j{arguments.jobs}",
    ]
    if arguments.generated:
        command.append(f"grit_generated={Path(arguments.generated).resolve()}")
    command += arguments.scons_arguments
    engine_path = Path(arguments.engine).resolve()
    run(command, cwd=engine_path)
    return find_binary(engine_path, arguments.target, arguments.scons_arguments)


def perform_export(engine, project, preset, output, target, jobs, scons_arguments):
    output.parent.mkdir(parents=True, exist_ok=True)

    editor = find_binary(engine, "editor")
    command = [str(part) for part in [editor, "--headless", "--path", project, "--export-pack", preset, output]]
    print("$ " + " ".join(command), flush=True)
    exported = subprocess.run(command, capture_output=True, text=True)
    print(exported.stdout + exported.stderr, end="")
    if exported.returncode != 0 or "native code generation failed" in exported.stdout + exported.stderr:
        sys.exit("Exporting the project with native code failed.")

    build_arguments = argparse.Namespace(
        engine=str(engine),
        target=target,
        jobs=jobs,
        generated=project / GENERATED_DIRECTORY,
        scons_arguments=scons_arguments,
    )
    build(build_arguments)
    binary = find_binary(engine, target, scons_arguments)
    fingerprint = (project / FINGERPRINT_FILE).read_text(encoding="utf-8").strip()
    if not contains_bytes(binary, fingerprint.encode()):
        sys.exit(f"{binary} does not contain the native code of {project}. Delete it and export again.")
    return install_template(binary, output)


def export(arguments):
    engine = Path(arguments.engine).resolve()
    project = Path(arguments.project).resolve()
    output = Path(arguments.output).resolve()
    executable = perform_export(engine, project, arguments.preset, output, arguments.target, arguments.jobs, arguments.scons_arguments)
    print(f"Run the game with: {executable}")


def project_name(project):
    settings = project / "project.godot"
    if settings.exists():
        for line in settings.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.strip().startswith("config/name"):
                _, _, value = line.partition("=")
                name = value.strip().strip('"')
                sanitized = "".join(character if character.isalnum() or character in "-_" else "_" for character in name).strip("_")
                if sanitized:
                    return sanitized
    return project.name


def editor_arguments(scons_arguments):
    suffix_options = SANITIZER_OPTIONS + ("use_llvm", "extra_suffix")
    return [argument for argument in scons_arguments if argument.split("=", 1)[0] not in suffix_options]


def ensure_engine_ready(engine, jobs, scons_arguments):
    if not engine.exists():
        print("Grit: the Godot source is missing, setting it up.", flush=True)
        setup(argparse.Namespace(engine=str(engine), godot_repository=None))
    if not binary_path(engine, "editor").exists():
        print("Grit: the Grit editor is not built yet, building it once.", flush=True)
        build(argparse.Namespace(engine=str(engine), target="editor", jobs=jobs, generated=None, scons_arguments=editor_arguments(scons_arguments)))


def game(arguments):
    engine = Path(arguments.engine).resolve()
    project = Path(arguments.project).resolve()
    if not (project / "project.godot").exists():
        sys.exit(f"{project} is not a Godot project (no project.godot).")

    ensure_engine_ready(engine, arguments.jobs, arguments.scons_arguments)

    name = arguments.name or project_name(project)
    destination = Path(arguments.output).resolve() / name
    if destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True)

    pack = destination / f"{name}.pck"
    executable = perform_export(engine, project, arguments.preset, pack, arguments.target, arguments.jobs, arguments.scons_arguments)

    print(f"\nGrit: packed game ready in {destination}")
    print(f"  run it with: {executable}")
    if not arguments.no_archive:
        archive = shutil.make_archive(str(destination), "gztar", root_dir=destination.parent, base_dir=name)
        print(f"  distributable archive: {archive}")


def contains_bytes(path, needle):
    with open(path, "rb") as file, mmap.mmap(file.fileno(), 0, access=mmap.ACCESS_READ) as data:
        return data.find(needle) >= 0


def install_template(binary, pack):
    executable = pack.with_suffix(binary.suffix)
    shutil.copy2(binary, executable)
    return executable


def run_game(executable, use_vm):
    environment = dict(os.environ)
    if use_vm:
        environment["GRIT_DISABLE"] = "1"
    try:
        completed = subprocess.run([str(executable), "--headless"], env=environment, capture_output=True, text=True, timeout=GAME_TIMEOUT_SECONDS)
    except subprocess.TimeoutExpired:
        sys.exit(f"{executable} did not quit within {GAME_TIMEOUT_SECONDS} seconds ({'VM' if use_vm else 'native'} run).")
    results = {}
    times = {}
    for line in completed.stdout.splitlines():
        kind, _, rest = line.partition(" ")
        name, _, value = rest.partition(" = ")
        if kind == "result":
            results[name] = value
        elif kind == "time":
            times[name] = float(value.removesuffix(" ms"))
    return completed, results, times


def installed_executable(pack):
    candidates = [
        path
        for path in pack.parent.glob(glob.escape(pack.stem) + ".*")
        if path != pack and path.is_file() and os.access(path, os.X_OK)
    ]
    if len(candidates) != 1:
        found = ", ".join(sorted(path.name for path in candidates)) or "none"
        sys.exit(f"Expected exactly one executable exported next to {pack} (found: {found}). Run: python tools/grit.py export")
    return candidates[0]


def compare(arguments):
    executable = installed_executable(Path(arguments.pack).resolve())
    vm_executable = installed_executable(Path(arguments.reference).resolve()) if arguments.reference else executable
    print(f"Comparing {executable} with the VM run of {vm_executable}")
    vm_run, vm_results, vm_times = run_game(vm_executable, use_vm=True)
    if STRIPPED_MESSAGE in vm_run.stdout + vm_run.stderr:
        sys.exit("The pack was exported with stripped scripts and cannot run in the VM. Compare it against a normal export with --reference.")
    native_run, native_results, native_times = run_game(executable, use_vm=False)

    mismatches = [
        name
        for name in sorted(vm_results.keys() | native_results.keys())
        if vm_results.get(name) != native_results.get(name)
    ]
    for name in mismatches:
        print(f"MISMATCH {name}: vm={vm_results.get(name)} native={native_results.get(name)}")
    print(f"{len(vm_results) - len(mismatches)} of {len(vm_results)} results identical")

    print(f"\n{'benchmark':<16}{'vm':>12}{'native':>12}{'speedup':>10}")
    for name, vm_time in vm_times.items():
        native_time = native_times.get(name, float("nan"))
        speedup = vm_time / native_time if native_time > 0 else float("inf")
        print(f"{name:<16}{vm_time:>10.1f}ms{native_time:>10.1f}ms{speedup:>9.1f}x")

    fell_back = VM_FALLBACK_MESSAGE in native_run.stdout + native_run.stderr
    if fell_back:
        print("The native run fell back to the GDScript VM, the export template does not match the pack.")

    script_failures = [
        line
        for run_result in (vm_run, native_run)
        for line in (run_result.stdout + run_result.stderr).splitlines()
        if any(message in line for message in SCRIPT_FAILURE_MESSAGES)
    ]
    for line in script_failures:
        print(f"SCRIPT FAILURE {line.strip()}")

    for label, run_result in (("VM", vm_run), ("native", native_run)):
        if run_result.returncode != 0:
            print(f"{label} run exited with code {run_result.returncode}:")
            print("\n".join(run_result.stderr.splitlines()[-40:]))

    if fell_back or script_failures or mismatches or vm_run.returncode != 0 or native_run.returncode != 0:
        sys.exit(1)


def conformance(arguments):
    engine = Path(arguments.engine).resolve()

    def build_conformance_editor(generated):
        build(
            argparse.Namespace(
                engine=arguments.engine,
                target="editor",
                jobs=arguments.jobs,
                generated=generated,
                scons_arguments=CONFORMANCE_BUILD_ARGUMENTS + arguments.scons_arguments,
            )
        )
        return find_binary(engine, "editor", CONFORMANCE_BUILD_ARGUMENTS + arguments.scons_arguments)

    if not arguments.skip_generation:
        editor = build_conformance_editor(None)
        environment = dict(os.environ, GRIT_CONFORMANCE_OUTPUT=str(CONFORMANCE_DIRECTORY))
        generation = subprocess.run(
            [str(editor), "--headless", "--test", "--test-case=[Modules][Grit] Generate conformance code", "--no-skip"],
            cwd=engine,
            env=environment,
            capture_output=True,
            text=True,
        )
        for line in generation.stdout.splitlines():
            if line.startswith("Grit"):
                print(line)
        if generation.returncode != 0 or not (CONFORMANCE_DIRECTORY / "grit_registry.gen.cpp").exists():
            print(generation.stdout[-4000:])
            sys.exit("Generating conformance code failed.")

    editor = build_conformance_editor(CONFORMANCE_DIRECTORY)
    suite = subprocess.run(
        [
            str(editor),
            "--headless",
            "--test",
            "--test-case=Script compilation and runtime,[Modules][Grit] Native coverage",
            "--order-by=name",
        ],
        cwd=engine,
        capture_output=True,
        text=True,
    )
    log = ROOT / "build" / "conformance" / "run.log"
    log.write_text(suite.stdout + suite.stderr)

    failed_scripts = sorted(
        {line.split("tests/scripts/", 1)[1] for line in suite.stdout.splitlines() if "tests/scripts/" in line and line.strip().endswith(".gd")}
    )
    for script in failed_scripts:
        print(f"FAILED {script}")
    for line in suite.stdout.splitlines():
        if line.startswith("Grit coverage") or line.startswith("[doctest] assertions"):
            print(line)
    print(f"Full log: {log}")
    if suite.returncode != 0:
        sys.exit(1)


def package(arguments):
    engine = Path(arguments.engine).resolve()
    output = Path(arguments.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    targets = ["editor", "template_release", "template_debug"]
    for target in targets:
        binary = find_binary(engine, target, arguments.scons_arguments)
        destination = output / binary.name
        shutil.copy2(binary, destination)
        os.chmod(destination, 0o755)
        print(f"Packaged {destination}")
    patch_directory = output / "patches" / GODOT_TAG
    patch_directory.mkdir(parents=True, exist_ok=True)
    for patch in sorted(PATCH_DIRECTORY.glob("*.patch")):
        shutil.copy2(patch, patch_directory / patch.name)
    (output / "GODOT_VERSION").write_text(GODOT_TAG + "\n", encoding="utf-8")


def which_version(executable, arguments):
    path = shutil.which(executable)
    if not path:
        return None, None
    try:
        result = subprocess.run([executable, *arguments], capture_output=True, text=True, timeout=30)
    except (OSError, subprocess.SubprocessError):
        return path, None
    output = (result.stdout + result.stderr).strip().splitlines()
    return path, output[0] if output else ""


def detect_msvc():
    if shutil.which("cl"):
        return "cl (MSVC on PATH)"
    program_files = os.environ.get("ProgramFiles(x86)") or os.environ.get("ProgramFiles") or ""
    vswhere = Path(program_files) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if not vswhere.exists():
        return None
    try:
        result = subprocess.run(
            [str(vswhere), "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "displayName"],
            capture_output=True,
            text=True,
            timeout=30,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    lines = result.stdout.strip().splitlines()
    return f"MSVC: {lines[0]}" if lines else None


MINIMUM_PYTHON = (3, 9)


def python_version(executable):
    if executable == sys.executable:
        return sys.version_info[:3]
    try:
        result = subprocess.run(
            [executable, "-c", "import sys; print('%d %d %d' % sys.version_info[:3])"],
            capture_output=True,
            text=True,
            timeout=30,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    parts = result.stdout.split()
    return tuple(int(part) for part in parts[:3]) if len(parts) >= 3 else None


def find_python():
    candidates = [sys.executable, "python3", "python"] + [f"python3.{minor}" for minor in range(13, 8, -1)]
    if sys.platform == "win32":
        candidates = ["py", "python", "python3"] + candidates
    seen = set()
    for candidate in candidates:
        path = candidate if candidate == sys.executable else shutil.which(candidate)
        if not path or path in seen:
            continue
        seen.add(path)
        version = python_version(path if candidate != sys.executable else sys.executable)
        if version and version >= MINIMUM_PYTHON:
            return path, version
    return None, None


def doctor(arguments):
    engine = Path(arguments.engine).resolve()
    checks = []

    running = sys.version_info[:3]
    checks.append(("python (running Grit)", running >= MINIMUM_PYTHON, "{}.{}.{} at {}".format(*running, sys.executable)))

    launcher_python, launcher_python_version = find_python()
    if launcher_python:
        launcher_detail = "{}.{}.{} at {}".format(*launcher_python_version, launcher_python)
    else:
        launcher_detail = "no python 3.9+ found on PATH"
    checks.append(("python 3.9+ on PATH", launcher_python is not None, launcher_detail))

    git_path, git_version = which_version("git", ["--version"])
    checks.append(("git", git_path is not None, git_version or "not found"))

    scons_invocation = scons_command()
    scons_ok = scons_invocation == ["scons"] and shutil.which("scons") is not None
    scons_ok = scons_ok or scons_invocation != ["scons"]
    scons_detail = "not found"
    if scons_ok:
        scons_output = subprocess.run([*scons_invocation, "--version"], capture_output=True, text=True).stdout
        match = re.search(r"v(\d+)\.(\d+)\.(\d+)", scons_output)
        via = "" if scons_invocation == ["scons"] else " (via python -m SCons)"
        scons_detail = (f"SCons {match.group(0)}" if match else "installed") + via
        if match and (int(match.group(1)), int(match.group(2))) < (4, 0):
            scons_ok = False
            scons_detail += " (need 4.0 or newer)"
    checks.append(("scons", scons_ok, scons_detail))

    if sys.platform == "win32":
        compiler_detail = detect_msvc()
        checks.append(("C++ compiler", compiler_detail is not None, compiler_detail or "no MSVC found (install Visual Studio Build Tools with the C++ workload)"))
    else:
        compiler = None
        for candidate in ("g++", "clang++", "c++"):
            compiler_path, compiler_version = which_version(candidate, ["--version"])
            if compiler_path:
                compiler = (candidate, compiler_version)
                break
        checks.append(("C++ compiler", compiler is not None, f"{compiler[0]}: {compiler[1]}" if compiler else "no g++, clang++ or c++ found"))
        pkg_path, pkg_version = which_version("pkg-config", ["--version"])
        checks.append(("pkg-config", pkg_path is not None, pkg_version or "not found (needed on Linux)"))

    required_names = ("python 3.9+ on PATH", "git", "scons", "C++ compiler")
    required_ok = all(ok for name, ok, _ in checks if name in required_names)

    engine_detail = str(engine) if engine.exists() else f"{engine} (run: grit setup)"
    checks.append(("Godot source", engine.exists(), engine_detail))
    editor_built = binary_path(engine, "editor").exists()
    checks.append(("Grit editor built", editor_built, "yes" if editor_built else "no (run: grit build editor)"))

    print(f"Grit doctor - host: {host_platform()} {host_architecture()}\n")
    for name, ok, detail in checks:
        print(f"  [{'OK' if ok else 'XX'}] {name:<20} {detail}")

    print()
    if required_ok:
        print("All build prerequisites are present. Grit can be used.")
    else:
        print("Missing prerequisites. Install git, SCons (4.0+) and a C++ compiler, then run grit doctor again.")
        sys.exit(1)


def launcher_directory(explicit):
    if explicit:
        return Path(explicit).expanduser().resolve()
    if sys.platform == "win32":
        base = os.environ.get("LOCALAPPDATA") or str(Path.home())
        return Path(base) / "Grit" / "bin"
    return Path.home() / ".local" / "bin"


def install(arguments):
    tool = (ROOT / "tools" / "grit.py").resolve()
    bin_directory = launcher_directory(arguments.bin_dir)
    launcher = bin_directory / ("grit.cmd" if sys.platform == "win32" else "grit")

    if arguments.uninstall:
        if launcher.exists() or launcher.is_symlink():
            launcher.unlink()
            print(f"Removed {launcher}")
        else:
            print(f"Nothing to remove at {launcher}")
        return

    bin_directory.mkdir(parents=True, exist_ok=True)
    if launcher.exists() or launcher.is_symlink():
        launcher.unlink()
    if sys.platform == "win32":
        launcher.write_text(
            "@echo off\r\n"
            "where py >nul 2>nul\r\n"
            "if %errorlevel%==0 (\r\n"
            f'  py -3 "{tool}" %*\r\n'
            ") else (\r\n"
            f'  python "{tool}" %*\r\n'
            ")\r\n",
            encoding="utf-8",
        )
    else:
        os.chmod(tool, 0o755)
        launcher.write_text(
            "#!/bin/sh\n"
            'python="$(command -v python3 || command -v python)"\n'
            'if [ -z "$python" ]; then echo "grit: no python3 found on PATH" >&2; exit 1; fi\n'
            f'exec "$python" "{tool}" "$@"\n',
            encoding="utf-8",
        )
        os.chmod(launcher, 0o755)
    print(f"Installed the grit launcher at {launcher}")

    path_entries = [entry for entry in os.environ.get("PATH", "").split(os.pathsep) if entry]
    on_path = any(Path(entry).expanduser().resolve() == bin_directory for entry in path_entries)
    if on_path:
        print('You can now run "grit <command>" from anywhere.')
    elif sys.platform == "win32":
        print(f'Add it to your PATH, then open a new terminal:')
        print(f'  powershell -Command "[Environment]::SetEnvironmentVariable(\'Path\', [Environment]::GetEnvironmentVariable(\'Path\', \'User\') + \';{bin_directory}\', \'User\')"')
    else:
        print(f'Add {bin_directory} to your PATH, for example in your shell profile:')
        print(f'  export PATH="{bin_directory}:$PATH"')


def main():
    parser = argparse.ArgumentParser(description="Grit development tool")
    parser.add_argument("--engine", default=str(DEFAULT_ENGINE), help="Godot source tree used for building")
    commands = parser.add_subparsers(dest="command", required=True)

    setup_parser = commands.add_parser("setup", help="Check out Godot and apply the Grit patches")
    setup_parser.add_argument("--godot-repository", help="Existing Godot clone to create a worktree from")
    setup_parser.set_defaults(handler=setup)

    build_parser = commands.add_parser("build", help="Build Godot with the Grit module")
    build_parser.add_argument("target", choices=["editor", "template_debug", "template_release"])
    build_parser.add_argument("--generated", help="Directory with generated C++ to link into the build")
    build_parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    build_parser.set_defaults(handler=build)

    export_parser = commands.add_parser("export", help="Export a project and build its native export template")
    export_parser.add_argument("project")
    export_parser.add_argument("preset")
    export_parser.add_argument("output", help="Path of the exported .pck")
    export_parser.add_argument("--target", default="template_release", choices=["template_debug", "template_release"])
    export_parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    export_parser.set_defaults(handler=export)

    compare_parser = commands.add_parser("compare", help="Run an exported pack with the VM and natively and compare")
    compare_parser.add_argument("pack", help="Path of a .pck exported with the export command")
    compare_parser.add_argument("--reference", help="Pack whose VM run serves as reference, needed for packs with stripped scripts")
    compare_parser.set_defaults(handler=compare)

    doctor_parser = commands.add_parser("doctor", help="Check whether this system can build and use Grit")
    doctor_parser.set_defaults(handler=doctor)

    install_parser = commands.add_parser("install", help="Install a \"grit\" launcher so you can skip \"python3 tools/grit.py\"")
    install_parser.add_argument("--bin-dir", help="Directory to place the launcher in (default: ~/.local/bin on Unix)")
    install_parser.add_argument("--uninstall", action="store_true", help="Remove the launcher instead of installing it")
    install_parser.set_defaults(handler=install)

    game_parser = commands.add_parser("game", help="Build everything needed and produce a ready-to-run native game")
    game_parser.add_argument("project")
    game_parser.add_argument("preset")
    game_parser.add_argument("--output", default="build/games", help="Directory to place the packed game in")
    game_parser.add_argument("--name", help="Name of the game (defaults to the project name)")
    game_parser.add_argument("--target", default="template_release", choices=["template_debug", "template_release"])
    game_parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    game_parser.add_argument("--no-archive", action="store_true", help="Skip building the .tar.gz archive")
    game_parser.set_defaults(handler=game)

    package_parser = commands.add_parser("package", help="Copy the built editor and templates into a directory for release")
    package_parser.add_argument("output", help="Directory to copy the binaries into")
    package_parser.set_defaults(handler=package)

    conformance_parser = commands.add_parser("conformance", help="Run the GDScript test suite with native code")
    conformance_parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    conformance_parser.add_argument("--skip-generation", action="store_true", help="Reuse previously generated code")
    conformance_parser.set_defaults(handler=conformance)

    template_parser = commands.add_parser("template", help="Show template build")
    template_parser.add_argument("--target", default="template_release", choices=["editor", "template_debug", "template_release"])
    template_parser.set_defaults(handler=get_binary)

    arguments, scons_arguments = parser.parse_known_args()
    arguments.scons_arguments = scons_arguments
    arguments.handler(arguments)


if __name__ == "__main__":
    main()  