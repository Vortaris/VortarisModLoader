# CLAUDE.md

This file provides guidance to Claude Code when working with this repository.

## Build & verify (Windows / MSVC / SCons)

Links godot-cpp (external dependency, v10 master bundling Godot 4.7's API).
Prebuild it once (`scons platform=windows target=template_debug arch=x86_64`),
then point the plugin build at the checkout with `godot_cpp_path=`.

```bash
# Build the plugin DLL (outputs to demo/addons/vortarismodloader/bin/)
scons -j 8 platform=windows target=template_debug arch=x86_64 build_library=False \
      godot_cpp_path=C:/Users/Administrator/Desktop/godot-cpp-master

# Functional smoke (expect "=== VortarisModLoader Demo OK ===", exit 0)
godot --headless --path demo --quit

# Regression suite (T0–T30, 66+ assertions; exit 0 = all pass)
godot --headless --path demo --script res://scripts/regression_test.gd
```

Rules of thumb: **every structural/behavioral change must keep the demo AND the
regression suite green**; run them together. A fresh demo checkout needs
`.godot/extension_list.cfg` (run `godot --headless --import --path demo` once, or
open the project in the editor once). The regression test persists state under
`user://vml/` — it cleans up after itself, so re-running is safe.

Note: `generate_bindings=no` avoids a godot-cpp bindings-regeneration race seen
under parallel SCons builds on this machine. It is safe because the checkout is
already prebuilt.

## Architecture

Two layers, mirroring VortarisCSV/VortarisECS: pure C++ core (`src/core/`,
`namespace vortarismodloader`) + thin GDExtension binding (`src/gdscript/`,
`VML*` classes). The core touches `godot::Variant` only at the
`content_database` / `hook_registry` / `loader_backend` boundaries.

### Layering

- **`src/core/`** — the engine, no Godot class deps beyond types:
  - `resource_id` — `namespace:path` parse/validate/hash (Minecraft ResourceLocation style).
  - `registry_index` — id → sorted provider list, override arbitration
    `(priority desc, explicit, mod_id asc)`.
  - `overlay_stack` — ordered content sources; priority = stack position (base=0).
  - `content_database` — in-memory repository (`id → value + metadata`), the
    unified-preload store; mutable via set/erase.
  - `hook_registry` — `hook_id → sorted Callable chain`; invoke/emit/check semantics.
  - `manifest` / `discovery` / `scanner` / `dependency_graph` / `zip_installer` /
    `change_watcher` — mod parsing, discovery, implicit id scanning, topo sort +
    semver, transactional zip install, mtime+size change tracking.
  - `loader_backend` — the one bridge that constructs Godot resources (raw assets
    built directly so user:// has no import-cache dependency).
- **`src/gdscript/`** — `VMLModLoader` (engine singleton `VML`, registered at
  `MODULE_INITIALIZATION_LEVEL_SCENE`), `VMLResourceRouter` (native
  `ResourceFormatLoader` for `load("vml://ns:path")`), `VMLHotReloader`.
- **`src/register_types.cpp`** — SCENE-level init: classes, `VML` singleton,
  resource router registration/removal.
- **`demo/`** — data-driven mini-game "Vortaria" + mod fixtures + headless tests.
- **`demo/addons/vortarismodloader/`** — `.gdextension`, thin EditorPlugin
  (`editor_plugin.gd` + `mod_manager_panel.gd` + `mod_wizard.gd`).

### Key invariants (violating these is the classic source of bugs here)

- **Id is `namespace:path`, never an extension.** Implicit mapping: a file at
  `assets/<ns>/<path>.<ext>` (or `data/...`) yields id `ns:path`. Namespaces are
  `^[a-z0-9_]{1,32}$`; mod ids equal their content namespace.
- **Override = higher stack position wins.** Same id with providers from several
  mods resolves to max `(priority, explicit_)`, tie-break by mod id. `explicit`
  beats implicit only at *equal* priority.
- **`load_resource_pack` is forbidden here.** Zips extract to `user://vml/mods/<id>/`
  (transactional: temp + manifest validation + rename + rollback) so mods can be
  unloaded. Mounted packs cannot be unmounted.
- **Data lives in `ContentDatabase`, not the filesystem.** After a preload pass,
  `get_data` is an O(1) hash hit; `set_data`/`delete_data` rewrite live entries.
  Mods and hot reload refresh the database incrementally.
  `preload_database_async()` preloads in 32-id batches via `call_deferred`
  (`preload_progress` + `database_loaded`); the `_process_preload_batch` method is
  bound for that deferred chain.
- **Ids are dotted, never slashed.** `game:units.knight`, not `game:units/knight`.
  `ResourceId::is_valid_path` rejects `/`; the implicit Scanner maps filesystem
  separators to dots (`assets/<ns>/<sub>/<name>.<ext>` -> id `ns:sub.name`).
  `list_ids`/`get_all` prefix filters use the dotted form (`"game:units."`).
- **Convenience sugar exists**: `get`/`load`/`exists` are thin aliases of
  `get_data`/`get_resource`/`has`; `get_mod_path` returns a mod's root dir.
- **Hooks are declarative + namespaced, never source rewriting.** The game calls
  `invoke_hook/emit_hook/check_hook` at instrumented points; mods register
  `Callable`s with `add_hook`. Handler signatures: invoke `func(current, ...args)`,
  emit `func(...args)`, check `func(...args) -> bool`.
- **Deactivation must reset state.** `deactivate_mod` must clear hooks
  (`hooks_.remove_mod`), destroy the mod_main node, drop registry providers and
  database entries, and reset `content_scanned = false` — otherwise re-activation
  skips the re-scan and the mod comes back hollow.
- **`content_scanned` is a real flag.** set true after scanning; reset false on
  deactivate and on `reload_resources` so the next scan re-runs.
- **Mod mains instantiate in `_init`, not `_ready`.** The `VML` singleton is not
  in the scene tree, so `add_child` of a mod_main never fires `_ready`; hooks
  registered in `_init` get attributed to the active mod via `active_mod_`.
- **Determinism:** iterate sorted `std::vector`s (never `unordered_map` iteration
  order); tie-break by mod id; `std::map` (not `unordered_map<String>`) because
  godot-cpp ships no `std::hash<String>`.
- **`godot::Error` lives in `namespace godot`.** Core files (inside
  `vortarismodloader`) must write `godot::Error`, `godot::OK`, etc. — `Error` is
  resolved only inside `namespace godot`.
- **String concatenation:** `"literal" + String` works via a free operator, but
  `const String + "literal"` does not (the member `operator+(const char*)` is
  non-const). Use `a + String("/") + b` in const/`const String&` contexts.

### Testing conventions

Headless `extends SceneTree` scripts in `demo/scripts/`. `vml_test_util.gd`
provides `expect/expect_eq`; `regression_test.gd` runs T0–T30 and `quit(0/1)`.
Signal connections to the `VML` singleton **must use named methods, not lambdas**
— a lambda held across engine shutdown crashes at exit. Test order matters: the
M6/M7 sections mutate files and re-scan, so keep tests appended in milestone order.

## Project layout at a glance

```
src/register_types.cpp  SConstruct  .github/workflows/build.yml
src/core/   src/gdscript/   demo/   doc_classes/   docs/   dist/
demo/addons/vortarismodloader/{vortarismodloader.gdextension, editor_plugin.gd,
  mod_manager_panel.gd, mod_wizard.gd, bin/}
```
