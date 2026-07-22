# vtk-fluentcff

VTK `IO/FLUENTCFF` 模块：Fluent Common Fluid Format（`.cas.h5` / `.dat.h5`）Reader 的权威源码与文档。

本仓库**不单独构建**；由消费方 CMake 通过 `cmake/FluentVtk.cmake` 把 Reader 编进目标工程。

## 目录

```
vtk-fluentcff/
├── vtk/IO/FLUENTCFF/          # vtkFLUENTCFFReader / Internal / Transient
├── cmake/
│   ├── FluentVtk.cmake        # FLUENT_VTK_ROOT / fluent_vtk_add_local_reader()
│   ├── vtkAOSDataArrayTemplateInstantiate.cxx
│   └── IOFLUENTCFF/CMakeLists.txt   # 嵌入整棵 VTK 树时用
├── docs/                        # Reader / CFF 格式文档
└── scripts/                     # 例如 inspect_transient_cff_h5.py
```

## 在 CMake 中引用（推荐）

### 1. include 模块脚本

```cmake
set(FLUENT_VTK_ROOT "E:/klj/vtk-fluentcff" CACHE PATH "vtk-fluentcff 根目录")
include("${FLUENT_VTK_ROOT}/cmake/FluentVtk.cmake")
```

未设置 `FLUENT_VTK_ROOT` 时，默认为 `FluentVtk.cmake` 所在目录的上一级。

### 2. 创建静态库目标

```cmake
fluent_vtk_add_local_reader(LocalFLUENTCFFReader)
```

该函数会编译：

| 源文件 | 说明 |
|--------|------|
| `vtkFLUENTCFFInternal.cxx` | HDF5 工具、字段映射 |
| `vtkFLUENTCFFReader.cxx` | 主 Reader |
| `vtkFLUENTCFFReaderTransient.cxx` | 非定常多 dat API |
| `vtkAOSDataArrayTemplateInstantiate.cxx` | MSYS2/部分 VTK 构建的模板符号补丁 |

并 `PUBLIC` 暴露头文件目录 `${FLUENT_VTK_IO}`（即 `vtk/IO/FLUENTCFF`），以及编译宏 `FLUENT_CFF_ENABLE_GRAPH_DATA` / `FLUENT_CFF_ENABLE_DELTA_CACHE`（见下节）。

### 编译期特性开关（默认 OFF）

`FluentVtk.cmake` 提供两个 `option`，经 `target_compile_definitions(... PUBLIC)` 传入 Reader 与所有链接方：

| CMake option | 宏 | 默认 | OFF 时 |
|--------------|-----|------|--------|
| `FLUENT_CFF_ENABLE_GRAPH_DATA` | `FLUENT_CFF_ENABLE_GRAPH_DATA` | **OFF** | 不编译扩展 GNN 几何（边界面质心/法向、多边形面积、`CellFaceAreas`）；**`GetCellCentroids` / `GetEdgeIndex` / `GetBoundaryFaceEdge*` 仍可用** |
| `FLUENT_CFF_ENABLE_DELTA_CACHE` | `FLUENT_CFF_ENABLE_DELTA_CACHE` | **OFF** | 不编译 float delta 路径；`LoadDataTimeStep` 仍可用，每步 **全量 double** |

配置示例（GNN / 非定常 delta 需显式开启）：

```cmake
set(FLUENT_CFF_ENABLE_GRAPH_DATA ON)
set(FLUENT_CFF_ENABLE_DELTA_CACHE ON)
fluent_vtk_add_local_reader(LocalFLUENTCFFReader)
```

或命令行：

```powershell
cmake ... -DFLUENT_CFF_ENABLE_GRAPH_DATA=ON -DFLUENT_CFF_ENABLE_DELTA_CACHE=ON
```

`fluent-ml` 的 `FluentCFFGNNPy` / `vtk-addon` 已在 `include(FluentVtk.cmake)` 之后将两 flag 设为 **ON**。仅读 mesh + chunk、不做 GNN 的消费方可保持默认 OFF 以减小二进制体积。

头文件默认值见 `vtk/IO/FLUENTCFF/vtkFLUENTCFFConfig.h`。

### 3. 链接 VTK / HDF5 并挂到你的目标

**默认工具链：vcpkg + MSVC（`x64-windows`）**，与 `fluent-ml` / pip PyTorch 扩展 ABI 一致。

```cmake
list(PREPEND CMAKE_PREFIX_PATH "E:/klj/vcpkg-work/fluentcff-gnn/installed/x64-windows")

find_package(VTK REQUIRED COMPONENTS
  CommonCore CommonDataModel CommonExecutionModel FiltersCore)
find_package(hdf5 CONFIG REQUIRED)

fluent_vtk_add_local_reader(LocalFLUENTCFFReader)

target_compile_definitions(LocalFLUENTCFFReader PRIVATE
  VTK_USE_EXTERN_TEMPLATE=0
  VTKCOMMONCORE_STATIC_DEFINE
)

find_library(VTKHDF5_LIB vtkhdf5-9.3 REQUIRED)
find_library(VTKHDF5_HL_LIB vtkhdf5_hl-9.3 REQUIRED)

target_link_libraries(LocalFLUENTCFFReader PUBLIC
  hdf5::hdf5-shared   # 或 hdf5-static，视 vcpkg 安装而定
  VTK::CommonCore
  VTK::CommonDataModel
  VTK::CommonExecutionModel
  VTK::FiltersCore
  ${VTKHDF5_LIB}
  ${VTKHDF5_HL_LIB}
)

add_executable(my_app main.cxx)
target_link_libraries(my_app PRIVATE LocalFLUENTCFFReader)
```

C++ 使用：

```cpp
#include "vtkFLUENTCFFReader.h"
#include <vtkNew.h>

vtkNew<vtkFLUENTCFFReader> reader;
reader->SetFileName("case.cas.h5");
reader->Update();
```

### CMake 变量

| 变量 | 含义 |
|------|------|
| `FLUENT_VTK_ROOT` | 本仓根路径 |
| `FLUENT_VTK_IO` | `vtk/IO/FLUENTCFF`（include） |
| `FLUENT_VTK_CMAKE` | `cmake/` |
| `FLUENT_CFF_ENABLE_GRAPH_DATA` | GNN graph 数据（默认 OFF） |
| `FLUENT_CFF_ENABLE_DELTA_CACHE` | float delta 多步缓存（默认 OFF） |
| `FLUENTCFF_MSVC_VCPKG_ROOT` | vcpkg 安装前缀（MSVC VTK/HDF5） |

## 验证构建（vcpkg 路径）

在 **VS Developer PowerShell** 中（PowerShell 不支持 `&&`，请分行执行）：

```powershell
$env:FLUENTCFF_MSVC_VCPKG_ROOT = "E:/klj/vcpkg-work/fluentcff-gnn/installed/x64-windows"
cmake -S ../fluent-ml/cmake/FluentCFFGNNPy -B ../fluent-ml/build/fluentcff_gnn_py --preset fluentcff-gnn-py-msvc
cmake --build ../fluent-ml/build/fluentcff_gnn_py -j 8 --target LocalFLUENTCFFReader
```

默认 OFF（无 GNN/delta）单独验证：

```powershell
cmake -S ../fluent-ml/cmake/FluentCFFGNNPy -B ../fluent-ml/build/fluentcff_gnn_py_off `
  -DFLUENTCFF_MSVC_VCPKG_ROOT=E:/klj/vcpkg-work/fluentcff-gnn/installed/x64-windows `
  -DFLUENT_VTK_ROOT=E:/klj/vtk-fluentcff `
  -DFLUENT_CFF_ENABLE_GRAPH_DATA=OFF -DFLUENT_CFF_ENABLE_DELTA_CACHE=OFF
cmake --build ../fluent-ml/build/fluentcff_gnn_py_off -j 8 --target LocalFLUENTCFFReader
```

完整 Python 扩展：

```powershell
cmake --build ../fluent-ml/build/fluentcff_gnn_py -j 8
```

仓库内可参考的完整 `CMakeLists.txt`：

- [`fluent-ml/cmake/FluentCFFGNNPy/CMakeLists.txt`](../fluent-ml/cmake/FluentCFFGNNPy/CMakeLists.txt)
- [`fluent-ml/python/vtk-addon/cpp/CMakeLists.txt`](../fluent-ml/python/vtk-addon/cpp/CMakeLists.txt)

> MSYS2 / `FluentCFFZoneViewer` 为历史路径；**新改动与日常验证一律走 vcpkg + MSVC**。

## 嵌入 VTK 源码树（可选）

若通过 [`vtk-build`](../vtk-build/) 整棵编译 VTK，可将 `IO/FLUENTCFF` 同步到 VTK 树，使用 `cmake/IOFLUENTCFF/CMakeLists.txt` 注册 `VTK::IOFLUENTCFF`：

```cmake
find_package(VTK REQUIRED COMPONENTS IOFLUENTCFF ...)
target_link_libraries(my_app PRIVATE VTK::IOFLUENTCFF)
```

注意：fork 中的 transient 实现位于 `vtkFLUENTCFFReaderTransient.cxx`；若走 VTK 模块路径，需在 `IOFLUENTCFF/CMakeLists.txt` 的 `SOURCES` 中自行补全。日常开发以 **`FluentVtk.cmake` 本地静态库** 为准。

## 消费方

| 项目 | 引用方式 |
|------|----------|
| [fluent-ml](../fluent-ml/) | `-DFLUENT_VTK_ROOT=../vtk-fluentcff` + `include(.../FluentVtk.cmake)` |
| [vtk-build](../vtk-build/) | `IO/FLUENTCFF/` 源码同步自本仓库 |

## 文档

| 主题 | 文件 |
|------|------|
| 模块改动与 API（含 transient） | [docs/fluent-cff-modified-modules.md](docs/fluent-cff-modified-modules.md) |
| Reader 读取流程 | [docs/fluent-cff-dat-cas-structure.md](docs/fluent-cff-dat-cas-structure.md) |
| HDF5 字段树 | [docs/cff-v21-cas-dat-hdf5-field-tree.md](docs/cff-v21-cas-dat-hdf5-field-tree.md) |
| Python 绑定 | [docs/vtk-addon/](docs/vtk-addon/) |
| 非定常 testdata 摘要 | [docs/transient-testdata-LCSanalysis.md](docs/transient-testdata-LCSanalysis.md) |
