# Cross-platform builds

The plugin targets Godot 4.7 on Windows / Linux / macOS. Each platform's binary
is built on that platform (cross-compiling macOS/Linux from Windows is not
supported by Apple / not practical with godot-cpp). The GitHub Actions workflow
`.github/workflows/build.yml` compiles all three on tag pushes and attaches the
ready-to-use addon zips to the release.

## Locally

```bash
pip install scons
# in a godot-cpp checkout (v10 master):
scons platform=windows target=template_debug arch=x86_64
scons platform=windows target=template_release arch=x86_64

# in the plugin repo:
scons -j 8 platform=windows target=template_debug arch=x86_64 build_library=False godot_cpp_path=<path-to-godot-cpp>
```

For Linux use `platform=linux` (godot-cpp names it `linux`; the artifact and
`.gdextension` key use `linuxbsd`, mapped by `SConstruct`). For macOS use
`platform=macos arch=universal`.

## Installing a platform binary

Drop the artifact into `addons/vortarismodloader/bin/` with the exact name the
`.gdextension` expects, e.g.
`vortarismodloader.linuxbsd.template_release.x86_64.so`.
