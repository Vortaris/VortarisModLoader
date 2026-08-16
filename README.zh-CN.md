# VortarisModLoader

数据驱动的 Godot 4.7 模组加载系统，用 C++ 写成 GDExtension。开源、无任何运行时依赖。

**核心理念：id 索引一切，重载资源指向即生效。** 所有可 mod 内容用 `namespace:path` 唯一 id 索引（如 `game:units.knight`、`mymod:icons.archer`），借鉴 Minecraft Fabric/Forge 的 Registry 与 Resource/Data Pack 模型。mod 修改 = 替换 id 指向的资源文件，无需改游戏代码。

面向组合/ECS 数据驱动游戏（系统、组件、实体都是数据），也支持传统游戏的贴图重载、模型替换、场景覆盖。

## 特性

- **id 索引一切**：`namespace:path` 唯一标识，命名空间防冲突；隐式路径约定——`assets/<ns>/<path>.<ext>` / `data/<ns>/<path>.<ext>` 放文件即得 id `ns:path`，零声明。
- **统一加载数据库**：可选项，进入游戏时把数据全量预载到内存仓库，id 查询 O(1) 纯内存；支持分批异步预载（`preload_database_async`）；`set_data`/`delete_data` 原地改写，热更可增量刷新。
- **声明式钩子**：hook 点即 id，三种语义——`invoke_hook`（链式改参数/返回值）、`emit_hook`（广播）、`check_hook`（判定拦截）。无正则源码重写。
- **覆盖仲裁**：后加载的 mod 覆盖先加载者（优先级 + 显式注册 + mod id 兜底，确定性）。
- **运行时生命周期**：启动早期扫描（早于 autoload）、运行时动态 enable/disable/load/unload 整个 mod、zip 事务性安装。
- **开发热重载**：mtime+size 轮询，改 mod 文件即时生效（数据/资源刷新 + 信号通知）；`vortarismodloader/general/verbose` 开启详细加载日志。
- **原生 `vml://` 加载**：`load("vml://ns:path")`，C++ 注册的 ResourceFormatLoader 在导出版也可用。
- **易上手 API**：`get`/`load`/`exists`/`get_mod_path` 等便捷别名，几十秒上手。
- **id 元数据与预留**：`get_id_info`（完整状态）、`get_id_data_type`、`set_id_type`/`list_ids_by_type`（按类型过滤）、`reserve`/`unreserve`（预留命名）。
- **ID 内容注册表**：可保存的 `id→资源` 路由（`user://vml/registry.json`，`finish_startup` 自动加载）；mod 提供同 id 即覆盖——`load("vml://main_menu_bg")` 自动切换背景。
- **运行时重路由**：`reroute`/`clear_reroute` 游戏内热切换内容指向。
- **mod 配置**：manifest 声明 `config_schema`，`get_config`/`set_config` 读写 `user://vml/configs/<mod_id>.json`。
- **数据驱动场景**：`build_node(id)` 从 Dictionary 数据递归构建节点树（配合 ECS 数据驱动）。
- **内容校验**：`validate()` 扫描所有数据，报告缺失的 id 引用。
- **EditorPlugin**：右侧 "VML IDs" 面板（与 Inspector 并列）可视化编辑注册表 + Tools > "VML Mods" 打开 mod 管理（Mods/IDs/Hooks 三页，列宽可拖）+ 一键创建 mod 向导；操作在控制台打印反馈日志。
- **多平台**：Windows / Linux / macOS，GitHub Actions 三平台构建与 tag 发布。

## 快速开始

1. 把 `addons/vortarismodloader/` 复制到你的 Godot 4.7 项目（或从 Release 下载 zip）。
2. 在 `Project > Project Settings > Plugins` 启用 VortarisModLoader。
3. 创建一个 bootstrap autoload，在 `_ready()` 调用 `VML.finish_startup()`：

```gdscript
# autoload "Bootstrap"（必须放在其他 autoload 之前）
extends Node

func _ready() -> void:
	VML.finish_startup()
```

4. 游戏侧用 id 读取内容：

```gdscript
var knight: Dictionary = VML.get_data("game:units.knight")     # JSON -> Dictionary
var camp: PackedScene = VML.get_resource("game:scenes.camp")   # 场景
var node: Node = VML.instantiate("game:scenes.camp")           # 实例化
var dmg: float = VML.invoke_hook("game:modify_damage", [10.0], 10.0)  # 钩子
```

5. 把游戏内容放在 `res://assets/game/`、`res://data/game/`（命名空间 `game`），mod 放在 `res://mods-unpacked/<mod_id>/`（开发）或 `user://vml/mods/`（运行时安装的 zip）。

## mod 包格式

```
<mod_id>/
  manifest.json      # namespace/name/version_number/dependencies/...（Thunderstore 兼容）
  mod_main.gd        # 可选入口脚本，extends Node，_init 里注册钩子
  icon.png           # 可选
  assets/<ns>/<path>.<ext>   → id "ns:path"
  data/<ns>/<path>.<ext>     → id "ns:path"
```

`manifest.json` 关键字段：`namespace`（= mod id，`^[a-z0-9_]{1,32}$`）、`name`、`version_number`（semver）、`dependencies`/`optional_dependencies`（`"lib_mod"` 或 `"lib_mod@>=1.0"`）、`load_before`、`incompatibilities`、`extra.godot.main_script`。详见 [docs/mod_format.md](docs/mod_format.md)。

覆盖基础资源：mod 在自身包内放 `data/game/units/knight.json` 即可覆盖 `game:units.knight`。

## 从 0.2.1 迁移

- **`register` 与 `register_id`**：`VML.register(id, path)` 不变。新增的 `VML.register_id(id, value, priority)` 注册直接 Variant 提供者（无文件）。两者别混淆——路径形式仍叫 `register`。
- **`has` 新语义**：`has()` 现在在 id 有实时内存数据库条目时也返回 true（此前仅路由/预留）。用 `has_data(id)` 专门查询数据库。
- **`finish_startup` 与 `finish_startup_auto`**：`finish_startup()` 不变（bootstrap autoload 里调用）。`finish_startup_auto()` 延迟并在场景树就绪前重试，保证 autoload `_ready` 先跑——设置 `vortarismodloader/general/auto_finish_startup` 可自动触发。
- **`get_mod_errors` 与 `get_mod_report`/`get_errors_summary`**：`get_mod_errors` 仍只含 errors。要含 warnings 用 `get_mod_report(id)`；一次拿全部问题 mod 用 `get_errors_summary()`；`get_startup_report()` 给启动聚合。
- **`get_all` 新语义**：现返回注册表 id 与已加载数据库 id 的并集（值惰性解析）。结构 `{ canonical_id: value }` 不变。
- **`set_data` 持久化**：`set_data(id, value, true)` 将值作为项目级 `__registry__` 条目（优先级 0，mod 可覆盖）持久化。注册表默认路径改为 `res://vml/registry.json`（`vortarismodloader/paths/registry_path` 可配置），res:// 只读（导出）时回退 `user://vml/registry.json`。
- **自定义 mod 根**：mod 从两个独立目录设置扫描——`vortarismodloader/paths/mod_dir`（默认 `res://mods`）与 `vortarismodloader/paths/unpacked_dir`（默认 `res://mods-unpacked`）。运行时用 `add_mod_root`/`remove_mod_root` 增删额外根；`rescan()` 尊重自定义根。旧版（0.3.0/0.3.1）`vortarismodloader/paths/mod_paths` / `vortarismodloader/mod_paths` 数组仍会合并读取，保证升级兼容。
- **导出策略**：`vortarismodloader/export/export_mods`（`embedded`/`external`/`none`）+ `vortarismodloader/paths/scan_user_mods` 控制扫描范围；`get_mod_package_plan()` 查询、`set_export_policy()` 设置。

## 文档

- [mod_format.md](docs/mod_format.md) — mod 包格式与 manifest 参考
- [quickstart.md](docs/quickstart.md) — 快速上手
- [registry.md](docs/registry.md) — ID 内容注册表（持久化 + 编辑器面板 + reroute + mod 配置）
- [hooks.md](docs/hooks.md) — 声明式钩子指南
- [database.md](docs/database.md) — 统一加载数据库
- [dev_hot_reload.md](docs/dev_hot_reload.md) — 开发热重载
- [release_mods.md](docs/release_mods.md) — 发布版安装 mod
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

跨平台构建见 [docs/cross_platform.md](docs/cross_platform.md)；CI 在 [.github/workflows/build.yml](.github/workflows/build.yml)（三平台 + tag 发布）。

## 许可

MIT。
