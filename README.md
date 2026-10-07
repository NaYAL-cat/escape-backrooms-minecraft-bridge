# Minecraft × Escape the Backrooms

Windows 本机双进程桥接原型：Minecraft 运行玩家、物品、方块和生存逻辑，逃离后室提供场景、局部碰撞及实体。Minecraft 的世界颜色与深度、手持物和 HUD 分层传入 ReShade，UE4SS 的独立摄像机跟随 MC 捕获视角。

**这是开发中的 V2 源码发布。构建、协议、模拟碰撞及离屏渲染检查已执行，真实游戏画面与交互仍需使用者验收。** 项目由 Codex 协助开发，参考 Universal Modder 的 Minecraft/GTA passthrough 示例。仓库不包含游戏本体、提取资产、存档、访问令牌或个人运行日志。

## 功能与范围

- MC 原生第一人称、WASD、左右键、持续挖掘、放置、主副手、背包和容器操作。
- 默认生存，保留合成、食物、工具、弓弩、红石与原生游戏系统；使用当前 MC 账号已加载的皮肤。
- 后室附近的射线采样转换成带小数边界的 MC 碰撞形状；保存已知安全落点，保护玩家免于碰撞数据失效后持续掉入虚空。
- 后室可受伤 Pawn 通过 MC 隐形代理接入攻击判定，伤害回传 UE；是否接受伤害仍由敌人的脚本决定。
- MC 世界像素按捕获深度正向投到后室相机，并与后室深度比较遮挡；HUD/手持物单独点采样。
- 原生无限帧设置，桥接输出时豁免后台降帧；D3D12 使用可复用、fence 保护的上传缓冲。
- 版本握手、单实例、断线释放、F8 交还输入、部署 SHA256 校验及文件回滚。

这不是全地图网格导出或完整引擎融合。复杂楼梯、细柱、移动物体可能漏采样，方块仍使用 MC 整数网格。双进程相机/深度时序、遮挡新显露区域、光照与阴影仍存在限制。未实现后室谜题/门锁到 MC 物品的完整映射或多人支持。只用于本机单人关卡和 MC 集成服务器。

## 开发基线

| 组件 | 版本/要求 |
| --- | --- |
| 系统 | Windows x64 |
| Minecraft | 26.3，Java 25 |
| Fabric Loader / Fabric API | 0.19.5 / 0.161.0+26.3 |
| 构建 | Gradle Wrapper 9.7.1；Fabric Loom 1.18-SNAPSHOT |
| 后室 | 开发时的 Steam build 24997718；其他版本未确认 |
| UE4SS | EscapeTheBackroomsCommunity UE4SS-ETB 1.3.0；日志内核版本 v4.0.0-beta.944 |
| ReShade | 6.8.0 **完整 add-on 支持版**，开发路径为 D3D12 |
| 协调器 | Python 3.12，运行时仅用标准库 |
| 原生构建 | MSVC 2022 x64 与 Windows SDK |
| 离线 Lua 测试 | lupa 2.8，仅测试依赖 |

用户需自行安装正版游戏、Fabric/API、后室 UE4SS loader 和 ReShade。此仓库提供桥接源码，不分发这些游戏或 loader 二进制。

## 构建

从仓库根目录执行。Java 25 应位于 `JAVA_HOME`，初次 Gradle 构建需要联网解析依赖。

```powershell
.\mc\gradlew.bat -p mc build
```

生成 `mc/build/libs/escape-backrooms-minecraft-bridge-0.2.0.jar`。在 **x64 Native Tools Command Prompt for VS 2022** 执行：

```bat
build.bat
```

生成 `host/reshade/build/ETBMinecraft.addon64`。普通终端可先将 `VCVARS64` 设为自己安装的 `vcvars64.bat`，脚本会调用它。

Gradle 初次下载完成后可用 `--offline`。不要将 `.gradle`、Minecraft 构建缓存或游戏 JAR 提交到仓库。仓库只保留可再分发的 Gradle Wrapper 工具 JAR，许可证见 `LICENSE-Gradle.txt`。

## 配置与安装

1. 给 Minecraft 26.3 创建 Fabric 0.19.5 配置，安装匹配的 Fabric API。PCL2 可以使用，但桥接本身不依赖其专用接口。
2. 在后室安装兼容的 UE4SS-ETB 和 ReShade 6.8.0 完整 add-on 支持版。确认其安装目录是游戏的 `EscapeTheBackrooms/Binaries/Win64`。
3. 复制 `paths.example.ps1` 为 **`paths.local.ps1`**，设置 `GameDir` 和 `MinecraftVersionDir`。`GameDir` 指向 Steam 的后室游戏根目录，MC 路径指向含 `mods` 的版本目录。个人配置已被 gitignore。
4. 保存并关闭两个游戏及旧桥接窗口，执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\deploy-v2.ps1
```

部署脚本备份并复制 MC JAR、ReShade addon/FX 与五个 UE4SS 脚本，校验八项 SHA256。它只安装桥接组件，不安装 UE4SS 或 ReShade loader，也不启动或结束游戏进程。

5. 在 `<GameDir>/EscapeTheBackrooms/Binaries/Win64/ue4ss/Mods/mods.txt` 启用 `BackroomsMinecraftBridge : 1`。
6. 在 ReShade 中将 add-on 搜索目录设置为 `addons64`，effect 搜索目录包含 `reshade-shaders/Shaders`，启用 `ETBMinecraft@ETBMinecraft.fx`。`examples/ReShadePreset.ini` 是只启用此 effect 的最小示例，请自行合并到已有 preset。

重建后用 `deploy-v2.ps1 -Refresh` 更新组件。运行中的游戏会阻止安装；不要手动删除已有存档。部署备份与安装清单默认保存在仓库下 `runtime/`，不上传 GitHub。

## 游玩

1. 启动 MC Fabric 配置；模组自动打开/创建独立的 `escape-backrooms-bridge-v2` 存档。
2. 进入后室的**单人实际关卡**，菜单、大厅或旁观角色不会被接管。
3. 运行 `start-bridge.bat`，或执行 `run-bridge.ps1`；也可提前启动，它会等待两个游戏。
4. 操作时让后室窗口获得焦点，MC 持续运行。

默认原生键位为 WASD、空格、Shift、Ctrl、E、Q、F、数字 1–9、左右键和滚轮，T / `/` 打开聊天/命令。MC 设置中的按键映射仍适用。桥接保留键：**F6 生存、F7 创造、F8 交还/恢复后室输入**。关闭桥接后看门狗释放接管。

MC 的走路摇晃、受伤倾斜和动态 FOV 在桥接配置中关闭，以利于对齐。MC 的 260 设置值是该版本的“无限帧”标记，不是 260 FPS 上限。实际 FPS 仍受两个游戏、GPU 和帧传输开销影响。

## 检查、诊断与回滚

```powershell
py -3.12 -m pip install -r tests/requirements.txt
py -3.12 tests/test_v2.py
.\mc\gradlew.bat -p mc --offline build
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\diagnose-v2.ps1
```

Java 构建包含 80 项输入与 240 项碰撞/落点断言。Python/Lua 测试不启动游戏。`diagnose-v2.ps1` 报告写入本地 `logs/`，可能含安装路径；提交 issue 前自行移除个人信息。`runtime/status.json` 的 RUNNING 代表链路就绪，不代表画面验收已通过。

可选的真实着色器离屏测试需要另外取得 ReShade **6.8.0 源码**，并在 x64 构建终端将 `RESHADE_SOURCE` 设置为其 `source` 目录：

```bat
tools\validate-shader.bat
tools\test-upload.bat
tools\test-render.bat
tools\build\test_render.exe --hardware
```

这些工具编译/执行仓库中的 FX，不会启动游戏。检查范围与结果见 [验证说明](docs/VALIDATION.md)。

保存并关闭游戏后，运行 `restore.ps1` 或 `deploy-v2.ps1 -Rollback` 恢复首次部署前的组件文件。它不删除存档，也不还原游戏设置或卸载另行安装的 loader。回滚后需重新部署 V2 才能运行此协调器；仓库不提供旧 V1 协调器。

## 目录

| 路径 | 内容 |
| --- | --- |
| `mc/` | Fabric 模组、原生输入映射、帧导出、碰撞与实体代理 |
| `host/bridge/` | 本机 WebSocket、共享输入环、协议、坐标和协调器 |
| `host/ue4ss/Scripts/` | 摄像机、局部碰撞、实体扫描/伤害与文件 IO |
| `host/reshade/` | 原生 add-on、SDK 头文件及合成 FX |
| `tests/`、`tools/` | 离线回归、shader 编译和离屏渲染工具 |
| `paths.example.ps1` | 可复制的本机路径配置 |
| `runtime/`、`logs/` | 本地生成状态，排除于 Git |

## 来源与许可证

- 基础架构参考 [Universal Modder](https://github.com/rehan-remade/universal-modder) 的 Minecraft/GTA passthrough 示例，原有版权和 MIT 许可保留于 `LICENSE-Universal-Modder.txt` / `LICENSE`。
- [ReShade](https://github.com/crosire/reshade) SDK 头文件沿用其版权声明与 BSD-3-Clause/MIT 条款，见 `LICENSE-ReShade.txt`；`ReShade.fxh` 标记为 CC0。
- 后室 loader 来源于 [EscapeTheBackroomsCommunity](https://github.com/EscapeTheBackroomsCommunity)，UE4SS 相关许可见 `LICENSE-UE4SS.txt`。仓库不打包 loader 二进制。
- Gradle Wrapper 脚本和工具保留 Apache-2.0 声明，见 `LICENSE-Gradle.txt`。

所有游戏文件和资源均由使用者自己的安装提供。此项目不是 Minecraft、Mojang、Microsoft、逃离后室开发商或上述开源项目的官方产品。
