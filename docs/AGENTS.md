# Agent Instructions

VTK `IO/FLUENTCFF` 源码：Fluent Common Fluid Format（`.cas.h5` / `.dat.h5`）Reader。公开行为与 C++ 接口以仓库根 [`README.md`](../README.md) 和 [`vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.h`](../vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.h) 为准。

本仓用 [`cmake/FluentVtk.cmake`](../cmake/FluentVtk.cmake) 里的 `fluent_vtk_add_local_reader` 编成静态库。仓库内没有可执行目标。

## 先读

| 主题 | 文件 |
|------|------|
| 公开 API 与接入 | [`README.md`](../README.md) |
| Reader 读取流程 | [`docs/fluent-cff-dat-cas-structure.md`](fluent-cff-dat-cas-structure.md) |
| HDF5 字段树 | [`docs/cff-v21-cas-dat-hdf5-field-tree.md`](cff-v21-cas-dat-hdf5-field-tree.md) |
| 已知 HDF5 源文件问题 | [`docs/hdf5-errors.md`](hdf5-errors.md) |
| 模块改动记录 | [`docs/fluent-cff-modified-modules.md`](fluent-cff-modified-modules.md) |
| 同目录下的 Python 接口摘录 | [`docs/vtk-addon/`](vtk-addon/) |

## 源码与构建入口

| 路径 | 内容 |
|------|------|
| `vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.h` / `.cxx` | 网格、字段、区域、派生量 |
| `vtk/IO/FLUENTCFF/vtkFLUENTCFFReaderTransient.cxx` | `LoadCaseMesh`、`LoadDataTimeStep`、释放与按步取值 |
| `vtk/IO/FLUENTCFF/vtkFLUENTCFFInternal.h` / `.cxx` | HDF5 辅助与字段名映射 |
| `vtk/IO/FLUENTCFF/vtkFLUENTCFFConfig.h` | `FLUENT_CFF_ENABLE_GRAPH_DATA`、`FLUENT_CFF_ENABLE_DELTA_CACHE`，未由 CMake 传入时均为 `0` |
| `vtk/IO/FLUENTCFF/vtk.module` | 嵌入 VTK 树时的模块依赖：`CommonCore`、`CommonExecutionModel`，私有依赖 `CommonMisc`、`hdf5` |
| `cmake/FluentVtk.cmake` | `FLUENT_VTK_ROOT`（默认是该文件的上一级目录）、两个 option（默认 `OFF`）、`fluent_vtk_add_local_reader` |
| `cmake/vtkAOSDataArrayTemplateInstantiate.cxx` | 显式实例化 `vtkAOSDataArrayTemplate<int>` 与 `vtkAOSDataArrayTemplate<long long>` |
| `cmake/IOFLUENTCFF/CMakeLists.txt` | 模块 `VTK::IOFLUENTCFF`，C++17。`SOURCES` 目前只有 `vtkFLUENTCFFInternal.cxx` |

`fluent_vtk_add_local_reader` 编入 `vtkFLUENTCFFInternal.cxx`、`vtkFLUENTCFFReader.cxx`、`vtkFLUENTCFFReaderTransient.cxx` 和模板实例化单元，并把两个开关以 `PUBLIC` 编译定义传出。头文件里的 `#if FLUENT_CFF_ENABLE_*` 会改变类的成员布局，链接该库的目标必须使用同一组开关。

`cmake/IOFLUENTCFF/CMakeLists.txt` 没有列入 `vtkFLUENTCFFReaderTransient.cxx`。时间步 API 要走 `fluent_vtk_add_local_reader`，或在模块 `SOURCES` 里自行加入该文件。

静态库的调用方需要能看到 VTK `CommonCore`、`CommonDataModel`、`CommonExecutionModel` 和 HDF5。语言标准为 C++17。

## 改代码时保持的行为

- case 用 `SetFileName`，dat 用 `SetDataFileName`。`DataFileName` 为空时，由 case 路径把末尾的 `cas.h5` 换成 `dat.h5`。
- `SetFileName` 换成另一个 case 时释放网格缓存和全部已加载时间步。
- 拓扑解释和 CFF 字段含义保持不变。构建兼容与性能优化分开写、分开核对。
- 大体量 VTK 数组用连续缓冲区和 `WritePointer` 写入。
- 改了公开行为时，同步 [`README.md`](../README.md) 和本文件。
- 改动保持最小，不添加与本次修改无关的文件。

## Commit

AI 提交信息末尾包含：

```
Co-Authored-By: Auto <noreply@cursor.com>
```
