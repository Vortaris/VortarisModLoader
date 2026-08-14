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
- **开发热重载**：mtime+size 轮询，改 mod 文件即时生效（数据/资源刷新 + 信号通知）。
- **原生 `vml://` 加载**：`load("vml://ns:path")`，C++ 注册的 ResourceFormatLoader 在导出版也可用。
- **易上手 API**：`get`/`load`/`exists`/`get_mod_path` 等便捷别名，几十秒上手。
- **EditorPlugin**：默认关闭，Editor > Tools > "VML Mods" 打开 dock（Mods/IDs/Hooks 三页）+ 一键创建 mod 向导。
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

## 文档

- [mod_format.md](docs/mod_format.md) — mod 包格式与 manifest 参考
- [quickstart.md](docs/quickstart.md) — 快速上手
- [hooks.md](docs/hooks.md) — 声明式钩子指南
- [database.md](docs/database.md) — 统一加载数据库
- [dev_hot_reload.md](docs/dev_hot_reload.md) — 开发热重载

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
