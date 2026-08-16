# VortarisModLoader — AI 调试指南 / AI Debugging Guide

面向 **AI / 自动化 / CI** 的调试入口。编辑器 GUI（右 dock 的 "VML IDs"
`id_editor_panel.gd`、主屏 "VML Mods" `mods_main_screen.gd`）是**给人看**
的交互工具；AI 请用下面的 **MCP `run_script` 直接调 API** 或 **headless CLI**。

This document is aimed at **AI agents / automation / CI**. The editor GUI
(right-dock "VML IDs" `id_editor_panel.gd`, main-screen "VML Mods"
`mods_main_screen.gd`) is a **human** tool; AI should use the **MCP `run_script`
API snippets** or the **headless CLI** below.

---

## 1. MCP run_script — 直接调用插件 API

Godot MCP 的 `run_script` 工具执行 `extends RefCounted` 的 GDScript，脚本内可访问
`VML` 单例（插件由 `vortarismodloader.gdextension` 加载，与运行模式无关）。建议用
`Engine.get_singleton("VML")` 显式取单例，避免脚本被静态类型检查器挑剔。

The Godot MCP `run_script` tool runs an `extends RefCounted` GDScript with full
access to the `VML` engine singleton. Prefer `Engine.get_singleton("VML")` for
explicitness.

> 前提 / prerequisite: MCP `run_project` / `attach_project` 启动的项目必须已生成
> 扩展缓存 `.godot/extension_list.cfg`（全新 clone 请先跑一次
> `godot --headless --editor --import --quit --path demo`）。

### 1.1 启动报告 + mod 列表 / startup report + mod ids

```gdscript
extends RefCounted

func execute(scene_tree: SceneTree) -> Variant:
	var vml: Object = Engine.get_singleton("VML")
	if vml == null:
		return {"ok": false, "reason": "VML singleton missing (extension not loaded)"}
	return {
		"ok": true,
		"startup_report": vml.get_startup_report(),   # { broken_mods, errors, warnings }
		"mod_ids": vml.get_mod_ids(),
		"load_order": vml.get_load_order(),
	}
```

### 1.2 校验单个 mod / validate one mod

```gdscript
extends RefCounted

func execute(scene_tree: SceneTree) -> Variant:
	var vml: Object = Engine.get_singleton("VML")
	if vml == null:
		return {"ok": false}
	# { valid: bool, errors: Array, warnings: Array, checked: int }
	return {"ok": true, "validate_mymod": vml.validate_mod("mymod"),
			"validate_badjson": vml.validate_mod("badjson_mod")}
```

### 1.3 读取内容 / read content by id

```gdscript
extends RefCounted

func execute(scene_tree: SceneTree) -> Variant:
	var vml: Object = Engine.get_singleton("VML")
	if vml == null:
		return {"ok": false}
	var data: Variant = vml.get_data("game:units.knight")    # JSON -> Dictionary
	return {
		"ok": true,
		"knight": data,
		"has": vml.has("game:units.knight"),
		"resolve": vml.resolve("game:units.knight"),
		"id_info": vml.get_id_info("game:units.knight"),     # source/priority/explicit/path
	}
```

### 1.4 前缀批量查询 / prefixed bulk query

```gdscript
extends RefCounted

func execute(scene_tree: SceneTree) -> Variant:
	var vml: Object = Engine.get_singleton("VML")
	if vml == null:
		return {"ok": false}
	return {
		"ok": true,
		"all_units": vml.get_all("game:units."),       # { "game:units.knight": {...}, ... }
		"namespaces": vml.list_namespaces(),
		"count_units": vml.count_ids("game:units."),
		"list_ids": vml.list_ids("game:units."),
	}
```

### 1.5 钩子调试：注册 → 触发 → 查看处理器 / hook debugging

```gdscript
extends RefCounted

func _double_damage(current: Variant, _amount: int, _weapon: String) -> Variant:
	return current * 2.0

func execute(scene_tree: SceneTree) -> Variant:
	var vml: Object = Engine.get_singleton("VML")
	if vml == null:
		return {"ok": false}
	# 临时注册一个测试钩子（非 mod_main 环境归属 __runtime__），调用后移除。
	vml.add_hook("debug:double", _double_damage, 10)
	var out: Variant = vml.invoke_hook("debug:double", [10, 2, "sword"], 10)
	vml.remove_hook("debug:double", _double_damage)
	return {
		"ok": true,
		"invoke_result": out,                  # 20.0
		"handlers_before_remove": vml.list_hook_handlers("debug:double"),  # [{mod_id, priority}]
		"live_hooks": vml.list_hooks("debug:"),
	}
```

### 1.6 依赖 / 运行时状态 / dependency & runtime state

```gdscript
extends RefCounted

func execute(scene_tree: SceneTree) -> Variant:
	var vml: Object = Engine.get_singleton("VML")
	if vml == null:
		return {"ok": false}
	return {
		"ok": true,
		"mymod_deps": vml.get_mod_dependencies("mymod"),   # { mylib: { exists, enabled } }
		"mymod_enabled": vml.is_mod_enabled("mymod"),
		"mymod_loaded": vml.is_mod_loaded("mymod"),
		"mod_path": vml.get_mod_path("mymod"),
		"errors_summary": vml.get_errors_summary(),
	}
```

### 1.7 0.3.3 约定式类型查询 / convention-based type queries

按点分隔的 `ns:type.*` 前缀跨**所有**命名空间聚合，免去 `list_namespaces()` +
`get_all(ns + ":")` 拼接。二者都含仅数据库条目（`set_data` 但无提供者）。

```gdscript
extends RefCounted

func execute(scene_tree: SceneTree) -> Variant:
	var vml: Object = Engine.get_singleton("VML")
	if vml == null:
		return {"ok": false}
	return {
		"ok": true,
		"units_ids": vml.get_ids_of_type("units"),      # PackedStringArray, 所有 ns:units.* id
		"units_data": vml.get_all_of_type("units"),     # { "game:units.knight": {...}, ... }
		"cards_ids": vml.get_ids_of_type("cards"),
	}
```

### 1.8 0.3.3 patch_data（浅层字段级合并）/ field-level patch

只改 patch 里出现的字段；嵌套 Dictionary/Array 整体替换（不做递归合并）；
id 不存在时等价 `set_data`。`database_entry_changed` 信号会发射。

```gdscript
extends RefCounted

func execute(scene_tree: SceneTree) -> Variant:
	var vml: Object = Engine.get_singleton("VML")
	if vml == null:
		return {"ok": false}
	var patched: bool = vml.patch_data("game:units.knight", {"attack": 999})
	return {
		"ok": true,
		"patched": patched,
		"after": vml.get_data("game:units.knight"),   # attack == 999, 其余字段保留
	}
```

### 1.9 0.3.3 钩子契约健康 / hook contract health

`get_hook_contract_health()` 给聚合计数 `{ declared, active, unhandled, undeclared,
healthy }`；`list_unmatched_hooks()` 给具体 id。`undeclared` = 有 handler 但未用
`register_hook_point` 声明；`unhandled` = 声明了 hook 点但无 handler。二者任一非空
通常意味着游戏重命名/移除了钩子点、或 mod 监听了未声明的钩子。

```gdscript
extends RefCounted

func execute(scene_tree: SceneTree) -> Variant:
	var vml: Object = Engine.get_singleton("VML")
	if vml == null:
		return {"ok": false}
	return {
		"ok": true,
		"contract": vml.get_hook_contract_health(),      # { declared, active, unhandled, undeclared, healthy }
		"unmatched": vml.list_unmatched_hooks("game:"),  # { undeclared: [...], unhandled: [...] }
		"declared_points": vml.list_hook_points(),
	}
```

### 1.10 0.3.3 image 占位符 pck 安全 / pck-safe image placeholders

`set_placeholder(id, "image", res://path)` 在导出版经 `ResourceLoader` 解析，返回
导入后的 `CompressedTexture2D`（原始 `Image::load_from_file` 读不了导出 `.pck`
内的 `.ctex`）。`user://` 无导入缓存的图仍回退为 `ImageTexture`。

```gdscript
extends RefCounted

func execute(scene_tree: SceneTree) -> Variant:
	var vml: Object = Engine.get_singleton("VML")
	if vml == null:
		return {"ok": false}
	vml.set_placeholder("mygame:m4.icon", "image", "res://assets/game/icons/peasant.png")
	return {
		"ok": true,
		"as_resource": vml.get_resource("mygame:m4.icon").get_class(),  # CompressedTexture2D
		"via_vml": load("vml://mygame:m4.icon").get_class(),
	}
```

---

## 2. Headless CLI — 参数表

独立命令行入口：`demo/scripts/cli_entry.gd`（`extends SceneTree`）。所有
`--vortaris-vml-*` 参数必须放在 `--` 之后（由 `OS.get_cmdline_user_args()` 读取）。
输出统一带 `[vortarismodloader]` 前缀，方便 grep / 解析。

`demo/scripts/cli_entry.gd` — a standalone `extends SceneTree` CLI. All
`--vortaris-vml-*` args must come **after `--`** (read via
`OS.get_cmdline_user_args()`). Every output line is prefixed with
`[vortarismodloader]`.

> **一次性前置步骤 / one-time prerequisite（全新 clone）**
> CLI 依赖 GDExtension（`vortarismodloader.gdextension`）。扩展缓存
> `.godot/extension_list.cfg` 被 gitignore，全新 clone 里不存在，此时
> `--script` 模式不会加载扩展，`VML` 单例缺失，CLI 会提示
> `[vortarismodloader] ERROR: GDExtension not loaded` 并退出 1。
> 首次运行 CLI 前，先执行一次：
> ```bash
> godot --headless --editor --import --quit --path demo
> ```
> （或在编辑器中打开一次该项目生成缓存）。之后 CLI 即可正常运行。
>
> CLI 在运行任何命令前会自动调用 `VML.finish_startup()`（幂等），等价于
> `main.gd` 的引导流程：加载持久化注册表、运行启动校验（填充 `get_startup_report`）、
> 实例化所有已启用 mod 的 `mod_main`。

| 参数 | 作用 | 退出码 |
|---|---|---|
| `--vortaris-vml-report` | 打印启动聚合 `{broken_mods, errors, warnings}` + mod id 列表 + load_order | `0` |
| `--vortaris-vml-validate <mod_id>` | 校验单个 mod，打印 `{valid, errors, warnings, checked}` | `0` 有效；`1` 无效 / 未知 mod |
| `--vortaris-vml-list` | 列出已发现 mod（id / namespace / enabled / loaded / deps）+ load_order + namespaces | `0` |
| `--vortaris-vml-get <id>` | 打印解析值 + id 信息（source / priority / explicit / path / data_type / preloaded） | `0` 找到；`1` 未找到 |
| `--vortaris-vml-install <zip>` | 事务性安装 zip mod（开发用，解压到第一个可写 mod 根目录，开发环境为 res://mods） | `0` 成功；`1` 失败 |
| 其他 / 未知参数 / 缺参数 | 打印用法 | `1` |

### 命令行示例 / examples

`godot` 指你的 Godot 控制台可执行文件（Windows 用 `*_console.exe`，Linux/macOS
用 `godot`），把它放进 PATH 或写成绝对路径占位符 `<path-to-godot>`：

```bash
GODOT="<path-to-godot>"

"$GODOT" --headless --path demo --script res://scripts/cli_entry.gd -- --vortaris-vml-report
"$GODOT" --headless --path demo --script res://scripts/cli_entry.gd -- --vortaris-vml-validate mymod
"$GODOT" --headless --path demo --script res://scripts/cli_entry.gd -- --vortaris-vml-list
"$GODOT" --headless --path demo --script res://scripts/cli_entry.gd -- --vortaris-vml-get game:units.knight
"$GODOT" --headless --path demo --script res://scripts/cli_entry.gd -- --vortaris-vml-install res://mods/archer_pack.zip
```

### 退出码约定 / exit-code convention

- `0`：成功（校验通过 / 命令执行完成）。
- `1`：校验失败、id 未找到、安装失败、未知参数、缺参数，或扩展未加载（全新 clone）。
- 配合 shell 可直接判断：`echo $?`（或 `${PIPESTATUS[0]}`）。

---

## 3. 编辑器面板说明 / editor panels

- **"VML" 主屏**（`mods_main_screen.gd`，`editor_plugin.gd` 里
  `_has_main_screen` 注册为 2D/3D/Script/AssetLib 旁的独立标签页）：给人用的 mod
  管理——启停、拖拽排序、安装 zip/PCK、卸载（仅 user://）、热重载、配置表单、
  Export PCK、Hooks/Content 标签页。**它在编辑器里自动运行 `finish_startup()`**，
  因此 mod_main 的钩子可见。旧的左下方 dock 已移除；
  `mod_manager_panel.gd` 只是承载同一主屏的薄包装（保留以过 T29 测试）。AI 请用
  `VML.enable_mod` / `disable_mod` / `install_mod_from_zip` 或 CLI。
- **"VML IDs"**（`id_editor_panel.gd`，右 dock，Inspector 旁）：给人用的注册表编辑。
  AI 请用 `set_registry_entry` / `get_registry_entry` / `save_registry` 或 CLI。
- 两种面板都是薄包装，无任何 AI 不可达的状态；所有底层能力都暴露在 `VML` 单例上。

---

## 4. 常用验证命令 / quick verification

```bash
# 一次性前置步骤（全新 clone）：生成扩展缓存 .godot/extension_list.cfg（gitignore，
# 全新 clone 不存在）。没有它，CLI 会提示 "GDExtension not loaded" 而退出 1，
# 回归脚本也会因类缺失而失败。退出 0 即正常。
godot --headless --editor --import --quit --path demo

# 冒烟：应打印 "=== VortarisModLoader Demo OK ==="，退出 0。
godot --headless --path demo --quit

# 全量回归（T0–T73 + 0.3.0 F1–F8 修复 + 0.3.3 M1–M4，380+ 断言），退出 0 = 全部通过
godot --headless --path demo --script res://scripts/regression_test.gd

# CLI 冒烟：启动报告 + mod 列表，退出 0
godot --headless --path demo --script res://scripts/cli_entry.gd -- --vortaris-vml-report
```
