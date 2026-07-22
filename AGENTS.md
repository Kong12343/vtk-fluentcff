# Agent Instructions

VTK `IO/FLUENTCFF` 模块源码仓：Fluent Common Fluid Format（`.cas.h5` / `.dat.h5`）Reader。本仓不独立构建；由消费方 CMake 集成。

## Global Skills

- 完整目录（1439 个）：`SKILLS.md`（由 `npx openskills sync -y -o SKILLS.md` 生成，安装新 skill 后重跑）
- 按需加载：`npx openskills read <skill-name>`（多个：`npx openskills read skill-one,skill-two`）
- 安装位置：`~/.claude/skills/`；Cursor 亦扫描此路径，但 catalog 会截断，故以 `SKILLS.md` + `read` 为准

## Documentation

先读 `docs/`，再改代码：

| 主题 | 文件 |
|------|------|
| 模块改动与 API | `docs/fluent-cff-modified-modules.md` |
| Reader 读取流程 | `docs/fluent-cff-dat-cas-structure.md` |
| HDF5 字段树 | `docs/cff-v21-cas-dat-hdf5-field-tree.md` |
| Python 绑定 | `docs/vtk-addon/` |
| 已知 HDF5 源文件问题 | `docs/hdf5-errors.md` |

## Build / Integration

- **VTK 模块**：`cmake/IOFLUENTCFF/CMakeLists.txt` → `VTK::IOFLUENTCFF`（C++17，依赖 HDF5）
- **本地静态库**：`include(cmake/FluentVtk.cmake)` + `fluent_vtk_add_local_reader(target)`
- **根路径**：`-DFLUENT_VTK_ROOT=<本仓路径>`（默认 `FluentVtk.cmake` 所在目录的上一级）
- **消费方**：`fluent-ml`（CMake 引用）、`vtk-build`（同步 `IO/FLUENTCFF/` 源码）
- **默认工具链（必须）**：**vcpkg + MSVC**（`x64-windows`），**不要**用 MSYS2 MinGW VTK 作为本仓改动的主验证路径（与 pip PyTorch / FluentCFFGNNPy ABI 一致）
- **vcpkg 前缀**：`E:/klj/vcpkg-work/fluentcff-gnn/installed/x64-windows`（仓库根 `../vcpkg-work/...`；vcpkg 本体在 `../vcpkg/`）
- **验证构建**：在 **VS Developer PowerShell** 下构建 `fluent-ml/cmake/FluentCFFGNNPy`（preset `fluentcff-gnn-py-msvc`），目标 `LocalFLUENTCFFReader` 或完整 `fluentcff_gnn`
- **编译宏**：`FLUENT_CFF_ENABLE_GRAPH_DATA` / `FLUENT_CFF_ENABLE_DELTA_CACHE` 默认 OFF；fluent-ml 默认 ON；见 `cmake/FluentVtk.cmake` 与 `docs/fluent-cff-modified-modules.md` §11
- **编译宏**：`FLUENT_CFF_ENABLE_GRAPH_DATA` / `FLUENT_CFF_ENABLE_DELTA_CACHE` 默认 OFF；fluent-ml 默认 ON；见 `cmake/FluentVtk.cmake` 与 `docs/fluent-cff-modified-modules.md` §11

## File-Scoped Commands

在 **VS Developer PowerShell** 中执行（PowerShell 不支持 `&&`；用 `;` 或分行）：

| Task | Command |
|------|---------|
| 配置 GNN/Reader（vcpkg） | `$env:FLUENTCFF_MSVC_VCPKG_ROOT='E:/klj/vcpkg-work/fluentcff-gnn/installed/x64-windows'; cmake -S ../fluent-ml/cmake/FluentCFFGNNPy -B ../fluent-ml/build/fluentcff_gnn_py --preset fluentcff-gnn-py-msvc` |
| 配置 Reader（flags OFF） | `cmake -S ../fluent-ml/cmake/FluentCFFGNNPy -B ../fluent-ml/build/fluentcff_gnn_py_off -DFLUENTCFF_MSVC_VCPKG_ROOT=E:/klj/vcpkg-work/fluentcff-gnn/installed/x64-windows -DFLUENT_VTK_ROOT=E:/klj/vtk-fluentcff -DFLUENT_CFF_ENABLE_GRAPH_DATA=OFF -DFLUENT_CFF_ENABLE_DELTA_CACHE=OFF` |
| 编译 Reader 静态库 | `cmake --build ../fluent-ml/build/fluentcff_gnn_py -j 8 --target LocalFLUENTCFFReader` |
| 编译完整扩展 | `cmake --build ../fluent-ml/build/fluentcff_gnn_py -j 8` |

MSYS2 / `FluentCFFZoneViewer` 仅作历史参考，**新改动与 agent 编译验证一律走 vcpkg 路径**。

## Key Conventions

- **源码位置**：`vtk/IO/FLUENTCFF/`（`vtkFLUENTCFFReader.*`、`vtkFLUENTCFFInternal.*`）
- **模板实例化**：MSYS2 预编译 VTK 缺 `vtkAOSDataArrayTemplate::SetValue` 符号 → 用 `cmake/vtkAOSDataArrayTemplateInstantiate.cxx`；数组写入优先连续缓冲区 + 指针，勿回退 `InsertNextValue` 路径（见 `docs/fluent-cff-modified-modules.md` §2）
- **语义不变**：拓扑与 CFF 字段含义保持一致；构建兼容与 perf 优化分开描述、分开验证
- **双文件输入**：`.cas.h5` + `.dat.h5`；本 fork 支持 case/data 独立指定
- **改动范围**：最小 diff；不增无关文件；文档更新与代码同步

## Commit Attribution

AI commits MUST include:

```
Co-Authored-By: Auto <noreply@cursor.com>
```
