# VortarisModLoader

[English](README.md) | **简体中文**

数据驱动的 Godot 4.7 模组加载系统，用 C++ 写成 GDExtension。开源、无任何运行时依赖。

**核心理念：id 索引一切，重载资源指向即生效。** 所有可 mod 内容用 `namespace:path` 唯一 id 索引（如 `game:units.knight`、`mymod:icons.archer`），借鉴 Minecraft Fabric/Forge 的 Registry 与 Resource/Data Pack 模型。mod 修改 = 替换 id 指向的资源文件，无需改游戏代码。

面向组合/ECS 数据驱动游戏（系统、组件、实体都是数据），也支持传统游戏的贴图重载、模型替换、场景覆盖。

## 特性

- **id 索引一切**：`namespace:path` 唯一标识，命名空间防冲突；隐式路径约定——`assets/<ns>/<path>.<ext>` / `data/<ns>/<path>.<ext>` 放文件即得 id `ns:path`，零声明。
- **统一加载数据库**：可选项，进入游戏时把数据全量预载到内存仓库，id 查询 O(1) 纯内存；支持分批异步预载（`preload_database_async`）；`set_data`/`delete_data` 原地改写，热更可增量刷新。`get_ids_of_type("cards")` / `get_all_of_type("cards")` 按 `ns:cards.*` 前缀跨所有命名空间枚举，免手动拼前缀；`patch_data(id, {"hp": 999})` 做浅层字段级合并，mod 只写要改的字段。
- **声明式钩子**：hook 点即 id，三种语义——`invoke_hook`（链式改参数/返回值）、`emit_hook`（广播）、`check_hook`（判定拦截）。无正则源码重写。`get_hook_contract_health()` / `list_unmatched_hooks()` 暴露契约漂移——有 handler 但未声明的钩子、声明的点却无 handler。
- **覆盖仲裁**：后加载的 mod 覆盖先加载者（优先级 + 显式注册 + mod id 兜底，确定性）。manifest 里的 `extra.godot.id_overrides` 可把文件显式映射到任意 id，胜过路径推断。
- **运行时生命周期**：启动早期扫描（早于 autoload）、运行时动态 enable/disable/load/unload 整个 mod、zip 事务性安装。
- **Pck 分发**：mod 根目录下的 `.pck` 启动时只读挂载，内容按普通扫描规则索引（包内须放在 `mods/<mod_id>/` 下）。
- **错误弹窗 + 控制台**：mod 错误始终打印到控制台；`vortarismodloader/general/show_error_dialogs` 显示模态弹窗（非 headless）。
- **高级调试日志**：`vortarismodloader/general/debug_output` 输出 `[vortarismodloader][dbg]` 行（扫描、注册表、钩子、数据、pck），`get_debug_log()` 取最近行。
- **开发热重载**：mtime+size 轮询，改 mod 文件即时生效（数据/资源刷新 + 信号通知）；`vortarismodloader/general/verbose` 开启详细加载日志。
- **原生 `vml://` 加载**：`load("vml://ns:path")`，C++ 注册的 ResourceFormatLoader 在导出版也可用。
- **易上手 API**：`get`/`load`/`exists`/`get_mod_path` 等便捷别名，几十秒上手。
- **id 元数据与预留**：`get_id_info`（完整状态）、`get_id_data_type`、`set_id_type`/`list_ids_by_type`（按类型过滤）、`reserve`/`unreserve`（预留命名）。
- **ID 内容注册表**：可保存的 `id→资源` 路由（`res://vml/registry.json`，`finish_startup` 自动加载，res:// 只读时回退 `user://vml/registry.json`）；mod 提供同 id 即覆盖——`load("vml://main_menu_bg")` 自动切换背景。
- **运行时重路由**：`reroute`/`clear_reroute` 游戏内热切换内容指向（最高优先级、不持久化）。
- **mod 配置**：manifest 声明 `config_schema`，`get_config`/`set_config` 读写 `user://vml/configs/<mod_id>.json`。
- **数据驱动场景**：`build_node(id)` 从 Dictionary 数据递归构建节点树（配合 ECS 数据驱动）。
- **内容校验**：`validate()` 扫描所有数据，报告缺失的 id 引用。
- **编辑器主屏**："VML" 标签页（与 2D/3D/Script/AssetLib 并列）是完整的 mod 管理工作区——mod 列表支持**拖拽调整优先级**，详情（manifest / 依赖 / 错误 / 配置表单）、hooks 可视化、内容浏览器、Install Zip / Install PCK / Create Mod / Rescan / Reload DB / **Export PCK** / Uninstall。
- **ID 占位符**：在编辑器里声明 id + 默认值，启动自动注册，`load("vml://id")` 在 mod 覆盖前解析到默认值。右侧 "VML IDs" 面板编辑持久化注册表 / 占位符（`res://vml/registry.json`，可 git 提交）。
- **多平台**：Windows / Linux / macOS，GitHub Actions 三平台构建与 tag 发布。

## 目录

1. [快速开始](#快速开始)
2. [mod 包格式](#mod-包格式)
3. [写一个 mod：完整教程](#写一个-mod完整教程)
4. [API 总览](#api-总览)
5. [编辑器：VML 主屏与 VML IDs 面板](#编辑器vml-主屏与-vml-ids-面板)
6. [项目设置参考](#项目设置参考)
7. [分发：文件夹 / pck / zip](#分发文件夹--pck--zip)
8. [0.3.3 新增](#033-新增)
9. [从 0.2.1 迁移](#从-021-迁移)
10. [常见问题](#常见问题)
11. [文档](#文档)
12. [构建](#构建)
13. [许可](#许可)

---

## 快速开始

### 1. 安装插件

1. 把 `addons/vortarismodloader/` 复制进你的 Godot 4.7 项目（或解压发布版 addon zip 到项目根目录）。
2. 启用插件：**Project > Project Settings > Plugins** → 打开 **VortarisModLoader**。
3. 创建一个 bootstrap autoload，并放在 autoload 列表**最前面**（**Project > Project Settings > Autoload**）。它唯一的工作是在场景树就绪后完成加载器的启动：

```gdscript
# Bootstrap.gd
extends Node

func _ready() -> void:
	VML.finish_startup()
```

   `finish_startup()` 幂等：加载持久化注册表、运行启动校验、实例化所有已启用 mod 的 `mod_main.gd`、打印 mod 错误。也可把 `vortarismodloader/general/auto_finish_startup` 设为 `true` 而完全跳过 bootstrap autoload（`finish_startup_auto()` 会延迟并在场景树就绪前重试）。

### 2. 把内容放到 id 下

基础内容放在 `res://assets/`（重资产：场景、贴图、音频）与 `res://data/`（数据：JSON、CSV），按命名空间目录组织。加载器隐式索引每个文件：

```
res://data/game/units/knight.json   ->  id "game:units.knight"
res://assets/game/scenes/camp.tscn  ->  id "game:scenes.camp"
```

### 3. 用 id 读取内容

```gdscript
var knight: Dictionary = VML.get_data("game:units.knight")     # JSON -> Dictionary
var camp: PackedScene = VML.get_resource("game:scenes.camp")   # Resource
var node: Node = VML.instantiate("game:scenes.camp")           # 实例化场景
var dmg: float = VML.invoke_hook("game:modify_damage", [10.0], 10.0)  # 钩子
```

数据 id（`.json`/`.csv`）经 `get_data()` 解析为 `Dictionary`/`Array`；其余一律经 `get_resource()` 作为 `Resource` 加载（`load("vml://game:scenes.camp")` 是原生加载器等价物，导出版同样可用）。

### 4. 在想要 mod 改动行为的地方声明 hook 点

```gdscript
# 游戏侧，在插桩点：
VML.register_hook_point("game:modify_damage", "Rewrite outgoing damage", ["current", "amount", "weapon"])
var final_damage: float = VML.invoke_hook("game:modify_damage", [base_damage, weapon], base_damage)
```

### 5. 运行

编辑器里按 **F5**。demo 项目（`demo/`）正是这么做的——见 `demo/scripts/quickstart.gd`（最简注释版教程，可用 `godot --headless --path demo --script res://scripts/quickstart.gd` 无头运行）。

---

## mod 包格式

mod 是文件夹（开发）或 zip / pck（分发），两者共用同一布局：

```
<mod_id>/
  manifest.json      # 必需——namespace/name/version_number/dependencies/...
  mod_main.gd        # 可选入口脚本（extends Node），_init 里注册钩子
  icon.png           # 可选，编辑器 mod 列表显示
  assets/<ns>/<path>.<ext>   ->  id "ns:path"      （重资产）
  data/<ns>/<path>.<ext>     ->  id "ns:path"      （数据）
```

- `<mod_id>` **等于** manifest 的 `namespace`（如 `my_mod`），且必须匹配 `^[a-z0-9_]{1,32}$`。
- mod 可提供**任意**命名空间的内容：
  - 新内容用**自己的**命名空间（`data/my_mod/units/archer.json` → id `my_mod:units.archer`）；
  - 覆盖基础内容用**目标**命名空间 + 相同相对路径（`data/game/units/knight.json` → id `game:units.knight`，胜过基础游戏与更早加载的 mod）；
  - 覆盖别的 mod 的内容同理，用那个 mod 的命名空间。

### manifest.json

字段名兼容 Thunderstore；Vortaris 专属键放在 `extra.godot` 下。

| 字段 | 类型 | 说明 |
|---|---|---|
| `namespace` | string | **内容命名空间 = mod id**，必需 `^[a-z0-9_]{1,32}$` |
| `name` | string | 显示名（编辑器 / `get_mod_display_name`） |
| `version_number` | string | `"1.2.0"`（semver；`get_mod_version`） |
| `description` / `website_url` | string | 可选 |
| `dependencies` | string[] | `"lib_mod"` 或 `"lib_mod@>=1.0"`；在该 mod 之前加载、启用时必需 |
| `optional_dependencies` | string[] | 存在才启用，缺失忽略 |
| `load_before` / `load_after` | string[] | 额外排序约束 |
| `incompatibilities` | string[] | 互斥；运行时强制（拒绝同时启用） |
| `extra.godot.main_script` | string | 默认 `"mod_main.gd"` |
| `extra.godot.icon` | string | 默认 `"icon.png"` |
| `extra.godot.asset_dirs` / `data_dirs` | string[] | 默认 `["assets"]` / `["data"]` |
| `extra.godot.id_overrides` | object | `{ "<相对路径>": "<完整 id>" }` —— 显式 id 映射（见下） |
| `extra.godot.config_schema` | object | 类 JSON-Schema 的 `{type, properties}`，给编辑器配置表单用 |

完整示例：

```json
{
	"name": "Sample Mod",
	"namespace": "mymod",
	"version_number": "1.0.0",
	"description": "Adds archers and overrides the knight.",
	"dependencies": ["mylib@>=1.0"],
	"incompatibilities": ["other_mod"],
	"extra": {
		"godot": {
			"main_script": "mod_main.gd",
			"id_overrides": {
				"data/mymod/units/archer.json": "game:units.elite_archer"
			},
			"config_schema": {
				"type": "object",
				"properties": { "difficulty": { "type": "number" } }
			}
		}
	}
}
```

逐字段参考、边界情况与 pck 布局见 [docs/mod_format.md](docs/mod_format.md)。

---

## 写一个 mod：完整教程

从空文件夹到一个可分发包的完整流程。

### 第 1 步 —— 脚手架

创建 `res://mods-unpacked/my_mod/`。编辑器里可用 **VML 标签页 → Create Mod**（自动生成合法的 `manifest.json`、`mod_main.gd`、`assets/<id>/` 与 `data/<id>/` 样例），或手写：

```
my_mod/
  manifest.json
  mod_main.gd
  data/my_mod/units/archer.json
  assets/my_mod/icons/archer.png
```

### 第 2 步 —— 内容（数据 + 资产）

`assets/` 或 `data/` 下的每个文件都变成一个 id：

```json
// data/my_mod/units/archer.json
{
	"id": "my_mod:units.archer",
	"name": "Archer",
	"health": 40,
	"attack": 8,
	"speed": 6,
	"cost": { "game:items.wood": 15 }
}
```

```json
// data/game/units/knight.json  —— 覆盖基础骑士，同命名空间 + 同相对路径
{
	"id": "game:units.knight",
	"name": "Knight",
	"health": 150,
	"attack": 15,
	"speed": 3
}
```

JSON 里的 `"id"` 字段只是**可选元数据**——加载器按路径索引，`validate_mod` 会把 JSON `"id"`（若存在）与路径推断 id 交叉核对，不一致给警告。

### 第 3 步 —— 逻辑（mod_main.gd）

`mod_main.gd` 是 mod 的入口。它**必须 `extends Node`**，且钩子/配置要在 **`_init`** 里注册，而不是 `_ready`——`VML` 单例不在场景树里，`_ready` 可能永不触发；`_init` 里注册的钩子会自动归属该 mod（卸载/禁用时一并清理）。

```gdscript
# my_mod/mod_main.gd
extends Node

func _init() -> void:
	# 声明本 mod 依赖的游戏 hook 点（可选，编辑器/文档用）。
	VML.register_hook_point("game:modify_damage", "Rewrite outgoing damage",
			["current", "amount", "weapon"])
	# 注册 handler，带优先级（越大越先执行）。
	VML.add_hook("game:modify_damage", _on_modify_damage, 10)
	# 读自己的配置（编辑器 Config 弹窗里设置）。
	var difficulty: float = VML.get_config("my_mod").get("difficulty", 1.0)

func _on_modify_damage(current: Variant, _amount: int, _weapon: String) -> Variant:
	return current * 2.0
```

### 第 4 步 —— 钩子（游戏可 mod 化的点）

游戏在插桩点调用分发 API；mod 注册 `Callable`。四种语义（详见 [docs/hooks.md](docs/hooks.md)）：

| API | 语义 | mod handler 签名 |
|---|---|---|
| `invoke_hook(id, args, default)` | 管线——每个 handler 改写值 | `func(current, ...args) -> Variant` |
| `invoke_hook_ctx(id, ctx, args)` | 上下文 Dictionary 管线 | `func(ctx: Dictionary, ...args) -> Dictionary` |
| `emit_hook(id, args)` | 广播，无返回值 | `func(...args)` |
| `check_hook(id, args)` | 判定——任一返回 `false` 即否决 | `func(...args) -> bool` |

### 第 5 步 —— 打包与分发

- **开发**：文件夹留在 `res://mods-unpacked/`；运行中改文件即热重载。
- **Zip（开发便捷）**：`VML.install_mod_from_zip("user://downloads/my_mod.zip")` —— 解压到第一个可写 mod 根目录。
- **Pck（发给玩家）**：**VML 标签页 → Export PCK** 从选中 mod 构建命名空间化的 `res://mods/<my_mod>/` 包。见[分发](#分发文件夹--pck--zip)。

---

## API 总览

以下全部位于 `VML` 引擎单例（`Object`，引擎启动时早于任何 autoload 注册）。完整参考见 [doc_classes/VMLModLoader.xml](doc_classes/VMLModLoader.xml)（编辑器内 F1 帮助即 **VMLModLoader**）。

### 读取内容

| API | 返回 | 说明 |
|---|---|---|
| `get_data(id)` / `get(id)` | `Variant` | `.json`/`.csv` → `Dictionary`/`Array`；否则 `Resource`；数据库预载后 O(1) |
| `get_resource(id)` / `load(id)` | `Resource` | 场景、脚本、贴图、字体、音频 |
| `instantiate(id)` | `Node` | 实例化按 id 解析的 `PackedScene` |
| `has(id)` / `exists(id)` | `bool` | 路由提供者 OR 预留 OR 实时数据库条目 |
| `has_data(id)` | `bool` | 专查内存数据库是否有实时条目 |
| `resolve(id)` | `String` | 胜出提供者的物理路径（未知则 `""`） |
| `get_id_info(id)` | `Dictionary` | `{valid, resolved, path, provider_mod, priority, explicit, preloaded, reserved, type, data_type}` |
| `get_id_data_type(id)` | `String` | `data`/`scene`/`script`/`image`/`audio`/`font`/`resource`/`value` |
| `list_ids(prefix)` | `Dictionary` | `{ namespace: PackedStringArray }`（路径不含 `ns:` 前缀） |
| `list_namespaces()` | `PackedStringArray` | 当前索引的全部命名空间 |
| `list_ids_in_namespace(ns)` | `PackedStringArray` | 某命名空间全部完整 `ns:path`，排序 |
| `count_ids(prefix)` | `int` | 以点分隔前缀开头的 id 数量 |
| `list_providers(id)` | `Dictionary` | `{providers: [{mod_id, path, priority, explicit}], best: int}` |

### 注册与预留 id

```gdscript
VML.register("mygame:cheat", "res://data/mygame/cheat.json")  # 显式路径路由
VML.register_id("mygame:value", 42, 0)                        # 直接 Variant 提供者（无文件）
VML.unregister("mygame:cheat")

VML.reserve("mygame:future_content")   # has() 报 true，暂无提供者
VML.unreserve("mygame:future_content")

VML.set_id_type("game:units.knight", "unit")   # 逻辑类型标签
VML.get_id_type("game:units.knight")           # "unit"
VML.list_ids_by_type("unit")                   # 所有打了 "unit" 标签的 id
```

### 内容数据库

```gdscript
VML.get_database_mode()          # "data" | "all" | "off"
VML.set_database_mode("all")     # 切换并重载
VML.preload_database()           # 同步预载
VML.preload_database_async()     # 跨帧分批（发射 preload_progress、database_loaded）
VML.reload_database()            # 清空并重新预载

VML.get_all("game:units.")       # { "game:units.knight": {...}, ... }（前缀）
VML.get_all()                    # 注册表 id + 已加载数据库 id 的并集

# 0.3.3 —— 约定式类型查询（点分隔 ns:type.* 前缀，跨所有命名空间）：
VML.get_ids_of_type("cards")     # PackedStringArray，所有 ns:cards.* id
VML.get_all_of_type("cards")     # { "game:cards.king": {...}, "mod:cards.jester": {...} }

# 原地改写：
VML.set_data("game:units.peasant", {"name": "Peasant MK2", "health": 60})
VML.set_data("mygame:persisted", {"x": 1}, true)   # 持久化为项目级值
VML.patch_data("game:cards.knight", {"hp": 999})   # 0.3.3 浅层字段级合并
VML.delete_data("game:units.peasant")              # 查询回退到文件
```

`patch_data` 只更新 **patch 里出现的字段**；合并是**浅**的（patch 里的嵌套 `Dictionary`/`Array` 值整体替换旧值）。id 没有 `Dictionary` 值或不存在时，`patch_data` 行为完全等价 `set_data`。

### 钩子

```gdscript
VML.register_hook_point("game:modify_damage", "Rewrite outgoing damage", ["current", "amount", "weapon"])
VML.add_hook("game:modify_damage", _on_modify_damage, 10)
VML.remove_hook("game:modify_damage", _on_modify_damage)

var dmg: float = VML.invoke_hook("game:modify_damage", [10.0, "sword"], 10.0)
var ctx: Dictionary = VML.invoke_hook_ctx("game:on_hit", {"damage": 10.0}, [target])
VML.emit_hook("game:on_entity_killed", ["archer"])
var allowed: bool = VML.check_hook("game:can_open_door", [door])

# 内省 + 0.3.3 契约健康：
VML.list_hooks("game:")                       # { hook_id: { count, mods } }
VML.list_hook_points("game:")                 # { hook_id: { description, arg_types } }
VML.list_hook_handlers("game:modify_damage")  # [{ mod_id, priority }, ...] 按调用顺序
VML.get_hook_contract_health()                # { declared, active, unhandled, undeclared, healthy }
VML.list_unmatched_hooks("game:")             # { undeclared: [...], unhandled: [...] }
```

### 注册表、占位符、重路由

```gdscript
VML.set_registry_entry("main_menu_bg", "res://assets/game/menus/bg.png", "image", "menu background")
VML.get_registry_entry("main_menu_bg")   # { path, type, description, value?, placeholder? }
VML.get_registry()                       # { id: {...} }
VML.remove_registry_entry("main_menu_bg")
VML.save_registry()                      # -> 项目级 res://vml/registry.json（可 git 提交）
VML.load_registry()

# ID 占位符（先声明 id + 默认值；mod 可覆盖）：
VML.set_placeholder("mygame:mainmenu.bg", "image", "res://assets/game/menus/bg.png", "menu background")
VML.set_placeholder("mygame:start_health", "data", 100)
VML.get_placeholder_ids("image")         # 按类型过滤（"" 为全部）
var bg: Texture2D = load("vml://mygame:mainmenu.bg")

# 运行时热切换（最高优先级、不持久化）：
VML.reroute("main_menu_bg", "res://assets/game/menus/bg_night.png")
VML.clear_reroute("main_menu_bg")
```

### mod 生命周期与健康

```gdscript
VML.get_mod_ids()            # 所有发现的 mod id
VML.get_load_order()         # 依赖排序的加载顺序
VML.is_mod_enabled(id)       # 内容是否已堆叠进注册表
VML.is_mod_loaded(id)        # 已启用且内容激活（立即反映，含无 mod_main 的纯数据 mod）
VML.enable_mod(id)           # 级联启用所需依赖
VML.disable_mod(id)          # 有启用依赖者时拒绝（级联禁用需 UI 确认）
VML.load_mod(id) / VML.unload_mod(id)   # 别名
VML.reload_mod(id)           # 原地热重载一个 mod（不产生重复钩子）
VML.get_mod_path(id) / get_mod_version(id) / get_mod_display_name(id) / get_mod_description(id)
VML.get_mod_priority(id)     # base=0，第一个 mod=1，……（不在加载顺序中则 -1）
VML.get_mod_dependencies(id) # { dep_id: { exists, enabled } }
VML.get_mod_dependents(id)   # 会级联停用的已启用 mod
VML.get_mod_order() / set_mod_order([...])   # 持久化的用户优先级（load_order.json）

VML.validate_mod(id)         # { valid, errors, warnings, checked }
VML.get_mod_report(id)       # { errors, warnings }
VML.get_errors_summary()     # { mod_id: { errors, warnings } }（有问题 mod）
VML.get_startup_report()     # { broken_mods, errors, warnings }
VML.get_error_summary()      # 人类可读 "<mod>: <err>" 文本（控制台 / 错误弹窗）
```

### zip 安装与 mod 根目录

```gdscript
var err: int = VML.install_mod_from_zip("user://downloads/archer_pack.zip")  # 仅开发
VML.uninstall_mod("my_mod")   # 彻底移除已安装（user://）的 mod
VML.get_mod_roots()           # mod_dir + unpacked_dir + 旧数组 + 附加根
VML.add_mod_root("user://my_mods")   # 持久化到 vortarismodloader/paths/extra_roots
VML.remove_mod_root("user://my_mods")
VML.install_root()            # 第一个可写的已配置根（编辑器 Install PCK 用）
VML.get_mod_package_plan()    # { embedded, external, scan_user_mods }
VML.set_export_policy("embedded", true)
```

### mod 配置、数据驱动场景、校验

```gdscript
VML.get_config_schema("my_mod")   # manifest 里的 config_schema
VML.get_config("my_mod")          # 未设置则 {}
VML.set_config("my_mod", {"difficulty": 2.0})

var node: Node = VML.build_node("game:ui.camp")   # {type, name, properties, children} -> 节点树
var report: Dictionary = VML.validate()           # { valid, checked, missing }
```

### 启动、热重载、调试

```gdscript
VML.finish_startup()          # bootstrap autoload；幂等
VML.finish_startup_auto()     # 延迟并在场景树就绪前重试
VML.is_startup_done()

VML.start_hot_reload(0.5)     # 开发：每 ~0.5 秒轮询
VML.reload_resources(["res://mods-unpacked/sample_mod/data/mymod/units/archer.json"])
VML.get_content_roots()       # 热重载监视的目录
VML.reload_database()

VML.get_debug_log()           # 最近 [vortarismodloader][dbg] 行
VML.clear_debug_log()
VML.get_legacy_mod_migration_notice()
```

---

## 编辑器：VML 主屏与 VML IDs 面板

启用插件后会多出两个编辑器界面：

### "VML" 主屏（mod 管理）

**2D / 3D / Script / AssetLib** 旁边的一个标签页。左侧选中 mod，右侧显示全部信息。界面是刷新友好的：**它在编辑器里自动运行 `finish_startup()`**，因此 mod_main 的钩子可见。

**左侧 —— mod 列表**（`Mod / Namespace / Enabled / Loaded / Priority / Deps`）：

- **拖动行**调整加载优先级。顺序会校验依赖边（依赖必须在依赖者之前），并持久化到 `user://vml/load_order.json`。无效/损坏的 mod（不在加载顺序中）拒绝拖动并说明原因。
- 红 = 有错误，绿 = 已加载，`-` 优先级 = 不在加载顺序（无效或已禁用）。

**工具栏：**

| 按钮 | 动作 |
|---|---|
| **Rescan** | `VML.rescan()` —— 完整重新发现 + 重建 |
| **Install PCK** | 把 `.pck` 复制进可写根目录（下次运行挂载） |
| **Install Zip** | 旧版开发流程：`install_mod_from_zip` |
| **Create Mod** | 一键 mod 骨架向导（`res://mods-unpacked/<id>/`） |
| **Reload DB** | `VML.reload_database()` |

**右侧 —— 详情**（manifest 名称 / 版本 / 描述 / 根目录 / 依赖带 `exists[on]` 标记 / 错误与警告 / **Enable/Disable** 带依赖确认、**Export PCK**、**Uninstall**、**Config**）：

- **Enable** 级联启用缺失依赖（先询问）；**Disable** 级联禁用依赖者（先询问）。缺依赖时拒绝启用。
- **Config** 根据 manifest `config_schema` 生成表单（SpinBox / CheckBox / OptionButton / LineEdit），无 schema 时回退 JSON 编辑。

**标签页：**

- **Hooks** —— 每个 handler 一行（`Hook / Mod / Priority / Description`），外加项目级**契约健康行**（`hook contract: N declared / M active`，漂移时显示 `MISMATCH: X undeclared / Y unhandled`）。
- **Content** —— 选中 mod 命名空间下的每个 id，带过滤框与 `ID / Path / Provider / Type` 列。

### "VML IDs" 面板（右 dock，Inspector 旁）

编辑持久化内容注册表与 ID 占位符（两者统一：占位符就是带默认值的注册表条目）。

- **Registry 标签**：每个条目 `ID / Path-or-Default / Type / Desc`。占位符显示 `[ph]`，持久化的 `set_data` 值显示 `[value]`。
- **Loaded 标签**：当前索引的每个 id（`ID / Path / Provider / Type`）。
- **Browse 标签**：按命名空间分组，可展开到各提供者（胜出者绿色高亮），可按命名空间与类型过滤。
- **New / Edit**：单个弹窗——id、类型（`data`/`scene`/`script`/`image`/`audio`/`font`/`resource`/`custom`）、默认值（资源路径；`data` 类型则 JSON 常量 *或* `res://data/...` 路径作为路由）、描述。
- **Save** 写入 `res://vml/registry.json`（可 git 提交；res:// 只读时回退 `user://vml/registry.json`）。**Reload** 重新读取。

因为 `finish_startup()` 每次启动都加载注册表，保存的路由与占位符自动生效——mod 仍可在运行时覆盖它们。

---

## 项目设置参考

在 **Project > Project Settings** 里按 `vortarismodloader/` 注册（分类 `general` / `paths` / `export`）。所有读取都经 `get_ml_setting()`——旧版扁平键（`vortarismodloader/<name>`，0.3.0）仍被兼容并在启动时迁移。

| 设置 | 取值（默认） | 说明 |
|---|---|---|
| `general/verbose` | bool（`false`） | 详细加载日志 |
| `general/show_error_dialogs` | bool（`false`） | 启动/重扫时弹模态框列出 mod 错误（仅非 headless）——错误始终打印到控制台 |
| `general/debug_output` | bool（`false`） | `[vortarismodloader][dbg]` 高级日志（扫描、注册表、钩子、数据、pck）；`get_debug_log()` |
| `general/auto_finish_startup` | bool（`false`） | 场景树就绪后自动 `finish_startup()`（跳过 bootstrap autoload） |
| `general/validate_on_startup` | bool（`true`） | 启动校验 mod；问题被标记，绝不拒绝启动 |
| `general/database_mode` | `data` / `all` / `off`（`data`） | 预载多少内容进内容数据库 |
| `paths/mod_dir` | 目录字符串（`res://mods`） | 开发 mod 主目录：`.pck` 包 + zip 安装的 mod 在此扫描 |
| `paths/unpacked_dir` | 目录字符串（`res://mods-unpacked`） | 解包开发 mod 文件夹，同样扫描 |
| `paths/registry_path` | 路径字符串（`res://vml/registry.json`） | 项目级注册表文件；res:// 只读（导出）时回退 `user://vml/registry.json` |
| `paths/scan_user_mods` | bool（`true`） | 为 `false` 时启动**不**扫描非 `res://` 根目录 |
| `export/export_mods` | `embedded` / `external` / `none`（`embedded`） | `embedded`：也扫描 res:// 根。`external`：仅用户/自定义根。`none`：完全不扫描 |
| `paths/extra_roots` | PackedStringArray（仅运行时） | `add_mod_root()` 加的附加根；编辑器隐藏，`get_mod_roots()` 合并 |

**注意**：`vortarismodloader/paths/mod_paths`（0.3.1）与扁平 `vortarismodloader/mod_paths`（0.3.0）数组已从编辑器移除，但其值仍作为向后兼容回退被合并读取。

---

## 分发：文件夹 / pck / zip

mod 到达游戏的三种方式；完整指南见 [docs/release_mods.md](docs/release_mods.md)。

| 方式 | 用途 | 只读？ |
|---|---|---|
| **`.pck` 包** | 玩家 / 发布版 | 是（只读挂载） |
| **解包文件夹** | 开发（`res://mods-unpacked/`） | 开发可写 |
| **`.zip` 安装** | 可选开发便捷 | 解压后当文件夹处理 |

**发布推荐 `.pck` 包。** 丢到任何已配置 mod 根目录下的 `.pck` 启动时只读挂载；包内内容须放在 `mods/<mod_id>/` 下，使其落在 `res://mods/<mod_id>/`：

```
sample_mod.pck
  mods/sample_mod/
    manifest.json
    data/sample_mod/units/archer.json
```

无 manifest 的包仍贡献内容（id 从文件夹名推导）。**VML 标签页 → Export PCK** 从选中 mod 构建恰好这样的布局（自动排除 import 元数据）。Install PCK 把包复制进可写根目录，下次运行挂载。

zip 安装（`VML.install_mod_from_zip`）解压到第一个可写根（开发为 `res://mods`）；导出构建里 `res://` 只读，zip 安装返回错误——那里请用 pck。

---

## 0.3.3 新增

- **类型查询**：`VML.get_ids_of_type("cards")` 返回所有 `ns:cards.*` id；`VML.get_all_of_type("cards")` 返回 `{ id: 数据 }`。免去 `list_namespaces()` + `get_all(ns + ":")` 手动拼接。
- **`patch_data`**：`VML.patch_data("game:cards.knight", {"hp": 999})` 对已有 Dictionary 做浅层字段级合并——mod 只写要改的字段。id 不存在时等价 `set_data`。嵌套 Dictionary/Array 整体替换（不做递归合并）。
- **钩子契约健康**：`VML.get_hook_contract_health()` 返回 `{ declared, active, unhandled, undeclared, healthy }`；`VML.list_unmatched_hooks()` 列出具体 id——有 handler 但未声明 hook 点（`undeclared`）、声明了 hook 点却无 handler（`unhandled`）。编辑器 Hooks 页显示健康行。插件无法观测游戏是否真的发射某钩子，故此项检查的是**声明层**契约（声明的点 vs 挂在上面的 handler），用于发现契约漂移（游戏重命名/移除钩子点、mod 监听未声明的钩子）。
- **image 占位符走 `ResourceLoader`**：`set_placeholder(id, "image", path)` 与 `get_resource(id)` 对 `res://` 图片经 `ResourceLoader` 解析，返回导入后的 `CompressedTexture2D`。原始的 `Image::load_from_file` 读不了导出 `.pck` 内的 `.ctex` 导入缓存，导致导出版 image 占位符失效——现已在导出版可用。`user://` mod 图片（无导入缓存）仍回退为原始 `ImageTexture`。

早期发布说明：`dist/vortarismodloader-changes-0.3.0.md` / `RELEASE_NOTES.md`。

---

## 从 0.2.1 迁移

- **`register` 与 `register_id`**：`VML.register(id, path)` 不变。新增的 `VML.register_id(id, value, priority)` 注册直接 Variant 提供者（无文件）。两者别混淆——路径形式仍叫 `register`。
- **`has` 新语义**：`has()` 现在在 id 有实时内存数据库条目时也返回 true（此前仅路由/预留）。用 `has_data(id)` 专门查询数据库。
- **`finish_startup` 与 `finish_startup_auto`**：`finish_startup()` 不变（bootstrap autoload 里调用）。`finish_startup_auto()` 延迟并在场景树就绪前重试，保证 autoload `_ready` 先跑——设置 `vortarismodloader/general/auto_finish_startup` 可自动触发。
- **`get_mod_errors` 与 `get_mod_report`/`get_errors_summary`**：`get_mod_errors` 仍只含 errors。要含 warnings 用 `get_mod_report(id)`；一次拿全部问题 mod 用 `get_errors_summary()`；`get_startup_report()` 给启动聚合。
- **`get_all` 新语义**：现返回注册表 id 与已加载数据库 id 的并集（值惰性解析）。结构 `{ canonical_id: value }` 不变。
- **`set_data` 持久化**：`set_data(id, value, true)` 将值作为项目级 `__registry__` 条目（优先级 0，mod 可覆盖）持久化。注册表默认路径改为 `res://vml/registry.json`（`vortarismodloader/paths/registry_path` 可配置），res:// 只读（导出）时回退 `user://vml/registry.json`。
- **自定义 mod 根**：mod 从两个独立目录设置扫描——`vortarismodloader/paths/mod_dir`（默认 `res://mods`）与 `vortarismodloader/paths/unpacked_dir`（默认 `res://mods-unpacked`）。运行时用 `add_mod_root`/`remove_mod_root` 增删额外根；`rescan()` 尊重自定义根。旧版（0.3.0/0.3.1）`vortarismodloader/paths/mod_paths` / `vortarismodloader/mod_paths` 数组仍会合并读取，保证升级兼容。
- **导出策略**：`vortarismodloader/export/export_mods`（`embedded`/`external`/`none`）+ `vortarismodloader/paths/scan_user_mods` 控制扫描范围；`get_mod_package_plan()` 查询、`set_export_policy()` 设置。

---

## 常见问题

**id 到底是什么？**
`namespace:path`，全小写。`path` 用点分隔，绝不用斜杠：`game:units.knight`，而不是 `game:units/knight`。文件系统分隔符映射为点（`assets/<ns>/<sub>/<name>.<ext>` → `ns:sub.name`）。命名空间 `^[a-z0-9_]{1,32}$`；mod 的 id 等于其内容命名空间。

**两个提供者声明同一 id 时谁赢？**
覆盖仲裁是确定性的：**优先级**（高者赢），然后**显式标志**（`register`、`id_overrides`、注册表/占位符路由是显式；路径推断是隐式）——但只在*同优先级*时，然后**mod id** 升序。后加载的 mod 有更高的加载顺序优先级。

**怎么覆盖基础游戏？**
在 mod 里放**同命名空间 + 同相对路径**的文件（`data/game/units/knight.json` 覆盖 `game:units.knight`）。如果文件的路径应映射到*别的* id，用 `extra.godot.id_overrides`。

**mod 放在哪里？**
`paths/mod_dir`（默认 `res://mods`）与 `paths/unpacked_dir`（默认 `res://mods-unpacked`），加上 `add_mod_root()` 添加的根。`user://vml/mods` 不再是默认根（0.3.0）。

**怎么把 mod 发给玩家？**
打包成 `.pck`，内容放在 `mods/<mod_id>/` 下，丢进游戏的 `mods/` 文件夹。pck 只读、启动挂载。zip 安装只是开发便捷方式。

**为什么我的 mod_main `_ready()` 不执行？**
`VML` 不在场景树里，`add_child` 的 mod_main 永远不触发 `_ready`。在 `_init` 里注册钩子/配置。

**为什么 `load("vml://...")` 对 JSON 数据 id 失败？**
数据 id（`.json`/`.csv`）不是 `Resource`。用 `VML.get_data(id)` 读取。

**怎么枚举所有 mod 的卡片/单位/物品？**
`VML.get_ids_of_type("cards")` / `VML.get_all_of_type("cards")`——点分隔的 `ns:type.*` 前缀跨所有命名空间聚合。

**禁用的 mod 还会覆盖吗？**
不会。`disable_mod` 会移除该 mod 的钩子、内容提供者与数据库条目，并重置 `content_scanned`，重新启用会全新扫描。

**不通过 mod 也能运行时热切换 id 吗？**
`VML.reroute(id, path)`——最高优先级、不持久化，用 `clear_reroute` 撤销。

**持久化注册表文件在哪？**
默认 `res://vml/registry.json`（`paths/registry_path`），res:// 只读导出时回退 `user://vml/registry.json`。建议提交版本控制。

**`export_mods = "external"` 改了什么？**
只扫描用户/自定义（非 `res://`）根目录——打进游戏自身 pck 的内嵌 mod 被忽略。`"none"` 什么都不扫。

---

## 文档

- [mod_format.md](docs/mod_format.md) — mod 包格式与 manifest 参考
- [quickstart.md](docs/quickstart.md) — 快速上手 + 完整 mod 教程
- [registry.md](docs/registry.md) — ID 内容注册表（持久化 + 编辑器面板 + reroute + mod 配置）
- [hooks.md](docs/hooks.md) — 声明式钩子指南（invoke/emit/check/ctx、契约健康）
- [database.md](docs/database.md) — 统一加载数据库（模式、类型查询、patch_data）
- [dev_hot_reload.md](docs/dev_hot_reload.md) — 开发热重载
- [release_mods.md](docs/release_mods.md) — 发布版安装 mod（pck 结构、扫描开关）
- [cross_platform.md](docs/cross_platform.md) — Windows / Linux / macOS 构建
- [AI_DEBUGGING.md](docs/AI_DEBUGGING.md) — AI / headless CLI 调试指南：MCP `run_script`
  API 示例、CLI 参数与退出码

## 构建

```bash
pip install scons
# 先预构建 godot-cpp（v10 master，Godot 4.7）
scons platform=windows target=template_debug arch=x86_64   # 在 godot-cpp 目录

# 构建插件
scons -j 8 platform=windows target=template_debug arch=x86_64 build_library=False \
      godot_cpp_path=<path-to-godot-cpp>

# 冒烟 + 回归测试
godot --headless --path demo --quit
godot --headless --path demo --script res://scripts/regression_test.gd
```

跨平台构建见 [docs/cross_platform.md](docs/cross_platform.md)；CI 在 [.github/workflows/build.yml](.github/workflows/build.yml)（三平台 + tag 发布，单 zip 含全平台二进制）。

## 许可

MIT。
