> [!IMPORTANT]
> Since the 09.11.2026 video by [Gamefromscratch](https://www.youtube.com/watch?v=_GahDfiwdVU) was released, this project has received a bit more attention again. A small piece of information regarding the apparent standstill from the outside: the project is not dead. There’s simply not much more to do. Currently, it mainly needs a lot of testing. And testing takes a lot of time, which I don’t have due to my private life. If Godot 4.8 is released, I will of course make sure to make Grit compatible. If you have any questions or find any bugs, or if you can help with testing or find bugs or problems there, please open an issue.

<p align="center">
  <img src="assets/logo.png" alt="Grit logo" width="160">
</p>

<h1 align="center">Grit</h1>

<p align="center">Ahead-of-time compilation of GDScript to native code for Godot 4.7</p>

> [!WARNING]
> Grit is **highly experimental**. It needs a patched engine, custom export templates and has so far only been
> tested on Linux x86_64 and Windows x86_64. Expect rough edges, do not ship games with it yet and always keep a way to fall back to the
> regular GDScript VM.

## What is Grit?

Grit is a Godot engine module that turns the GDScript of your project into C++ when you export it. That C++ is
compiled into your export template, so the game runs the same scripts as native machine code instead of bytecode.

Nothing changes in the editor: you write, run and debug GDScript exactly as before. Native code only exists in the
exported game, and every function Grit cannot handle keeps running in the GDScript VM.

## Features

- **Broad GDScript coverage:** all 1540 functions in Godot's own GDScript test suite compile to native code, and all
  499 test assertions pass when running natively.
- **Unboxed values:** typed `int`, `float` and `bool` as well as value types such as `Vector2`, `Color`, `String` or
  `Transform3D` live in plain C++ variables instead of `Variant`.
- **Direct engine calls:** operations the VM validates at compile time call Godot's pointer APIs directly. Engine
  operators, member accessors and constructors are inlined when Grit can prove they are exactly what the engine uses.
- **Direct script calls:** calls between functions of the same script file skip the engine's dispatch and jump
  straight into the native function, with the same override, object lock and recursion checks as the VM.
- **Engine properties:** reading and writing properties such as `position` or `node.rotation` calls the engine's
  getter or setter directly with unboxed values. Object properties such as `sprite.texture` skip the generic lookup
  too, on reads and on writes where the assigned value's type is known to fit. Each access site caches the resolved
  accessor per class, and objects whose scripts intercept a property with `_get`, `_set` or a member of the same name
  keep the VM's behavior.
- **Script members:** reading and writing member variables of other script instances, such as `enemy.health`, goes
  straight to the instance's member storage whenever no getter, setter or type conversion is involved.
- **Script stripping:** optionally removes the code of natively compiled functions from the exported scripts.
- **Built-in scripts:** scripts embedded in scenes and resources are compiled as well, not only `.gd` files.
- **Containers:** fast element access for `Array`, packed arrays and `Dictionary`, with the same bounds and type checks
  as the VM.
- **Full language support:** lambdas (with captures and `self`), `await` and coroutines, signals, inner classes,
  static functions and variables, default and rest parameters.
- **Same behavior:** runtime errors, their messages and edge cases such as integer overflow or division by zero behave
  like in the VM.
- **Debugging:** native functions appear in script backtraces, `print_stack()` and `get_stack()` with the correct
  lines.
- **Safe fallback:** unsupported functions, edited scripts or mismatching signatures silently use the VM, per function.

## How it works

1. **Engine patch.** A small patch to the GDScript module (see `patches/`) adds a hook to `GDScriptFunction::call` that
   can jump into native code and lets Grit plug in its own code generator.
2. **Recording at export.** When you export, Grit recompiles every `.gd` file of the project and every script
   embedded in a scene or resource. Its code generator forwards everything to the normal bytecode generator and
   records the same instructions into an intermediate representation. The recording mirrors the VM decisions exactly,
   which is how the semantics carry over.
3. **C++ generation.** The emitter turns that representation into C++ and writes it to `.godot/grit/generated`.
4. **Template build.** The generated C++ is compiled into a custom export template through the regular Godot build.
5. **Attaching at runtime.** The exported game still loads and compiles its scripts as usual. Whenever a function is
   compiled, Grit looks up native code for it by script, class, function name and line, checks its signature and
   attaches it. A fingerprint of all script sources makes sure the template and the exported project belong together.

Coroutines are compiled as native coroutines: at an `await`, the native code stores its variables in the engine's
function state and continues natively when the signal fires.

## Benchmarks

Measured with the benchmark project in `tests/projects/benchmark` on an AMD Ryzen 9 7950X3D, Linux, GCC 16 and a
release export template. Both columns come from the same binary; the VM run uses `GRIT_DISABLE=1`.

| Benchmark | What it does | VM | Grit | Speedup |
|---|---|---:|---:|---:|
| `sum_to` | typed integer loop | 196.3 ms | 4.0 ms | 49.1x |
| `collatz_steps` | nested loops with integer math | 1155.8 ms | 27.8 ms | 41.6x |
| `count_primes` | trial division | 202.0 ms | 10.3 ms | 19.6x |
| `mandelbrot` | floating point math | 346.4 ms | 9.8 ms | 35.3x |
| `particles` | `Vector2` math with arrays | 31.7 ms | 7.5 ms | 4.2x |
| `count_keys` | untyped dictionary updates | 66.6 ms | 42.8 ms | 1.6x |
| `build_text` | string building | 20.5 ms | 20.2 ms | 1.0x |
| `spawn_nodes` | creating and freeing nodes | 23.4 ms | 21.6 ms | 1.1x |
| `node_properties` | moving and rotating 1000 nodes | 65.2 ms | 26.5 ms | 2.5x |
| `self_properties` | a node moving itself | 50.6 ms | 21.8 ms | 2.3x |

Typed, computation heavy code gains the most. Code that mostly calls into the engine, such as string building or
node creation, spends its time inside Godot itself, so compiling the script changes little there.

## Getting started

Clone this repository first:

```sh
git clone https://github.com/DasDarki/grit.git
cd grit
```

Grit builds from source. You need everything required to [build Godot from source](https://docs.godotengine.org/en/stable/engine_details/development/compiling/index.html)
(Python 3.9+, SCons 4.0+ and a C++ compiler) plus git. A full build takes a few minutes on a modern machine. Check
your system first:

```sh
python3 tools/grit.py doctor
```

It reports whether Python, SCons, a C++ compiler and git are present, and where the Godot source and editor are.

To type `grit` instead of `python3 tools/grit.py`, install a launcher on your PATH:

```sh
python3 tools/grit.py install
```

On Unix this drops a small `grit` script into `~/.local/bin`; on Windows it writes a `grit.cmd`. The launcher finds a
working Python itself, and the build runs SCons under that same Python, so you do not have to pick an interpreter.
`install --uninstall` removes it again. The rest of this README uses `grit`; without the launcher, prefix the commands
with `python3 tools/grit.py`.

```sh
# Clone Godot 4.7.2-stable next to this repository and apply the Grit patch.
grit setup

# Build the Godot editor with the Grit module.
grit build editor
```

By default the engine lives in `../godot-4.7`; use `--engine` to pick another location, or
`setup --godot-repository <path>` to create a worktree from an existing Godot clone. Additional SCons options can be
appended to any command, for example `linker=lld`.

### One command to a packed game

```sh
grit game path/to/project "Linux"
```

This is the whole flow in one step. It checks out and patches Godot and builds the editor if that has not happened
yet, exports the project (which generates the native C++), builds the export template with that code, and writes a
ready-to-run game plus a `.tar.gz` archive into `build/games/<name>/`. `"Linux"` is the name of an export preset of
the project. Add `--target template_debug` for a debug build, `--output <dir>` to choose the location or `--no-archive`
to skip the archive.

### Exporting a project

If you want the pack and template separately, use `export` instead:

```sh
python3 tools/grit.py export path/to/project "Linux" build/game.pck
./build/game.x86_64
```

This exports the project with the Grit editor, which generates the C++, builds the export template with that code and
places it next to the pack. Add `--target template_debug` for a debug build.

The export log lists every function that stays in the VM together with the reason. To compare against the VM, run
the game with `GRIT_DISABLE=1`.

You can also export from the editor UI. The C++ is then written to `res://.godot/grit/generated`; build a template from
it and select that binary as the custom template in your export preset:

```sh
python3 tools/grit.py build template_release --generated path/to/project/.godot/grit/generated
```

### Settings

| Setting | Default | Description |
|---|---|---|
| `grit/export/enabled` | `true` | Generate native code when exporting. |
| `grit/export/generated_directory` | `res://.godot/grit/generated` | Where the generated C++ is written. |
| `grit/strip_scripts` (export preset option) | `false` | Remove the bodies of natively compiled functions from the exported scripts. |
| `GRIT_DISABLE=1` (environment) | unset | Run the exported game entirely in the VM. |

### Stripping scripts

With the export preset option `grit/strip_scripts`, release exports no longer ship the code of functions that run
natively. Grit replaces their bodies, including inline property setters and getters, with a stub that only keeps the
declaration valid, both in `.gd` files and in scripts embedded in scenes and resources. Class structure, members,
signals, constants and function signatures stay, because the engine and other scripts still need them. Functions that
stay in the VM or contain lambdas keep their bodies.

Stripping also replaces the readable `res://<file>::<function>:<line>` keys that map native code to scripts with short
hashes, so the export template no longer reveals that map. Script paths and inner-class names can still appear where the
engine needs them to load a class.

A stripped game cannot fall back to the VM. It only runs with the export template that was built together with the
export: with any other template it stops at startup with an error, and `GRIT_DISABLE` is ignored. Debug exports are
never stripped.

## Testing

```sh
# Run Godot's GDScript test suite with native code.
python3 tools/grit.py conformance

# Export the benchmark project, then run it in the VM and natively and compare results and timings.
python3 tools/grit.py export tests/projects/benchmark Linux build/benchmark.pck
python3 tools/grit.py compare build/benchmark.pck

# Export with stripped scripts and compare it against the VM run of the normal export.
python3 tools/grit.py export tests/projects/benchmark "Linux Stripped" build/stripped/benchmark.pck
python3 tools/grit.py compare build/stripped/benchmark.pck --reference build/benchmark.pck
```

To profile a single benchmark, run the exported game with `-- --only=<name>` and repeat it with `--repeat=<count>`,
for example `./build/benchmark.x86_64 --headless -- --only=node_properties --repeat=20`. Exporting with
`extra_suffix=profile debug_symbols=yes` builds a separate template with symbols for tools such as `perf`.

`grit package <dir>` copies the built editor and templates into a directory, for example to share a build with
someone else.

### Flappy test game

`tests/projects/flappy` is a small Flappy Bird clone that uses Grit the way a real game would: an inner class for the
pipes, typed arrays, vector math, signals with lambda handlers and an `await` on a timer. It needs no assets, everything
is drawn in `_draw()`. It ships `Linux`, `Linux Stripped` and `Windows Desktop` export presets, so on a Windows machine
you can build it the same way with `grit game tests/projects/flappy "Windows Desktop"`.

```sh
python3 tools/grit.py export tests/projects/flappy Linux build/flappy.pck

# Play it with space, a mouse click or touch.
./build/flappy.x86_64

# Watch the autopilot play.
./build/flappy.x86_64 -- --autoplay

# Compare the VM and native code.
python3 tools/grit.py compare build/flappy.pck
```

When started headless, the game lets the autopilot play 120000 physics steps with a fixed seed and prints the result
and the time, which is what `compare` uses. The simulation currently runs about 2.5x faster natively.

## Limitations

- Developed and tested on Linux x86_64 with GCC and Clang, and verified on Windows x86_64 with MSVC: the whole GDScript
  test suite compiles and passes natively there (1540/1540 functions, 499/499 assertions) and the benchmark project
  produces identical results to the VM. macOS, mobile and web are untested.
- Requires Godot 4.7.2 with the Grit patch and custom export templates.
- Native code only runs in exported games; the editor always uses the VM.
- Breakpoints and stepping in native code are implemented for debug templates but have not been tested with the
  remote debugger yet. The debugger shows no local variables for native functions, and native functions do not appear
  in the profiler.
- A few rare constructs stay in the VM, for example lambdas or `await` inside `assert`.
