# vtk-fluentcff

VTK `IO/FLUENTCFF`：读取 ANSYS Fluent Common Fluid Format（`.cas.h5` 网格、`.dat.h5` 结果），重建单元/面拓扑，并按体区域输出 `vtkMultiBlockDataSet`。读入后的网格和字段留在 Reader 内，可按全局 id 直接查询，不必再从 VTK 数据集反查。

支持笛卡尔与非结构网格（三角形、四面体、四边形、六面体、金字塔、楔形、多面体）、二维与三维、单精度与双精度。`.cas.h5` 与 `.dat.h5` 可以分别指定，同一网格可换不同结果文件。面结果与体结果分开枚举、分开选择。

类声明：[`vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.h`](vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.h)。

## 读取结果

`Update()` 在 `EmitVtkOutput` 为真时写出 `vtkMultiBlockDataSet`：每个体区域一块 `vtkUnstructuredGrid`。块下标与 `GetCellZoneIdAtIndex` / `GetCellZoneName` 的 `index` 一致。体字段写在对应块的 CellData 上。

面字段不单独成块。它们保存在面数据 chunk 里，可用 `GetFaceArrayValue` 按全局面 id 读取，或用 `CreateFaceZonePolyData` 做成某个面区域的 `vtkPolyData`。

网格只解析一次。之后可以：

- 再走一次管线 `Update()`，换 `DataFileName` 或字段选择；
- 用 `LoadDataTimeStep` 把多个 `.dat.h5` 追加为时间步，按步索引读取。

`SetFileName` 换成另一个 case 时，已缓存的网格和全部时间步会被释放。

## 两种调用

### 一次读完 case 和 dat

`DataFileName` 为空时，由 case 路径推导数据文件：去掉末尾 6 个字符 `cas.h5`，接上 `dat.h5`（`flow.cas.h5` → `flow.dat.h5`）。找不到 `.dat.h5` 时只读网格。

```cpp
#include "vtkFLUENTCFFReader.h"
#include <vtkNew.h>

vtkNew<vtkFLUENTCFFReader> reader;
reader->SetFileName("flow.cas.h5");          // 可选：reader->SetDataFileName("other.dat.h5");
reader->SetRenameArrays(false);              // 默认 false，保留 Fluent 变量名
reader->UpdateInformation();                 // 登记可选的体/面字段名
reader->SetCellArrayStatus("SV_P", 1);
reader->DisableAllFaceArrays();
reader->Update();                            // 网格 + 选中字段 + multiblock

vtkIdType nCells = reader->GetNumberOfCells();
double p = reader->GetCellArrayValue("SV_P", /*cellId=*/0);
```

### 网格缓存，按时间步追加 dat

`LoadDataTimeStep` 只追加，不释放已有步。同一路径再次调用时返回已有下标，并把该步设为活动步。

```cpp
reader->SetFileName("flow.cas.h5");
reader->SetCellArrayStatus("SV_P", 1);       // 白名单：在首次 LoadDataTimeStep 之前打开
if (reader->LoadCaseMesh() != 1)
{
  return;
}
const int step0 = reader->LoadDataTimeStep("flow-000.dat.h5"); // 新步下标，失败为 -1
reader->SetReferenceTimeStepIndex(step0);    // 仅 DELTA_CACHE 打开时生效
reader->SetUseDeltaStorage(true);
const int step1 = reader->LoadDataTimeStep("flow-001.dat.h5");

reader->SetActiveTimeStepIndex(step1);
double p = reader->GetCellArrayValue("SV_P", /*cellId=*/0);          // 活动步
double p0 = reader->GetCellArrayValue("SV_P", 0, 0, step0);          // 指定步

reader->SetEmitVtkOutput(false);             // Update() 只同步 chunk，不建 multiblock
reader->Update();
```

## 下标、名字和返回值

| 概念 | 约定 |
|------|------|
| 节点 / 面 / 单元 id | Reader 内部 **0-based**。文件中的 Fluent id 在读入时已转换 |
| 区域 id（`zoneId`） | Fluent zone id，与区域列表下标不是同一个数 |
| 区域下标（`index`） | 体区域、面区域列表中的 **0-based** 位置；multiblock 块号与体区域下标一致 |
| 时间步下标 | `LoadDataTimeStep` 的追加顺序，从 0 起。尚无活动步时 `GetActiveTimeStepIndex()` 为 `-1` |
| 字段名 | 与 `GetCellArrayName` / `GetFaceArrayName` 一致。`RenameArrays` 为真时使用 Common Fluids 可读名。相索引大于 1 时带后缀 `-phase_N` |
| 找不到的名称指针 | `nullptr` |
| 找不到的整型 id | `-1`（面区域尺寸等少数接口返回 `0`，见各表） |
| 找不到的字段值 | 安静 NaN |
| chunk 指针 | 空数据或越界为 `nullptr`。布局为 `data[(id * dim) + component]`。无下标接口指向当前 chunk，保持到下一次读取、`Update()` 或 `Release*`。带时间步下标的 `GetLoaded*ChunkData` 写入内部暂存（delta 步先还原成 double），同一侧（体或面）再次按步取 chunk 可能使先前的指针失效 |

单元类型（`GetCellType`）是 Fluent 单元类型：`1` 三角形，`2` 四面体，`3` 四边形，`4` 六面体，`5` 金字塔，`6` 楔形，`7` 多面体。面类型（`GetFaceType`）是该面的节点数。

体区域 `zoneType`：`0` / `1` 为流体，`2` 为固体。面区域 `zoneType` 来自 CFF zone 记录，例如 `3` 为 wall、`7` 为 symmetry；`2` 视为内部面（边界面邻接会跳过它）。未知 zone id 返回 `-1`。

## API

以下方法都属于 `vtkFLUENTCFFReader`（`vtkMultiBlockDataSetAlgorithm`）。`New()`、`Update()`、`UpdateInformation()` 沿用 VTK 管线。

### 文件

| 方法 | 作用 |
|------|------|
| `SetFileName(fileName)` / `GetFileName()` | case 路径，通常是 `.cas.h5`。路径变化会释放网格缓存和全部时间步 |
| `SetDataFileName(path)` / `GetDataFileName()` | 显式 `.dat.h5`。空字符串表示按 case 文件名推导。`LoadDataTimeStep` 成功后会被写成该步路径 |
| `SetRenameArrays(bool)` / `GetRenameArrays()` | 是否把 Fluent 变量名换成 Common Fluids 可读名。默认 `false` |
| `GetNumberOfCells()` | 成功读入网格后的单元数，初始为 `0` |
| `GetNumberOfNodesRead()` / `GetNumberOfFacesRead()` | 已读入的节点数、面数 |
| `GetMeshLoaded()` | 网格是否已缓存 |

### 字段选择

体字段与面字段各有一套选择器。`UpdateInformation()` 登记 dat 里出现的名字，并默认启用。`LoadDataTimeStep` 第一次见到的名字默认关闭。

排除名单为空时，保留 `Set*ArrayStatus` 的白名单。排除名单非空时，读取前先启用全部已知字段，再关闭名单里的名字。

| 方法 | 作用 |
|------|------|
| `GetNumberOfCellArrays()` / `GetCellArrayName(index)` | 已登记的体字段 |
| `GetCellArrayStatus(name)` / `SetCellArrayStatus(name, status)` | 该体字段是否读取。`status` 非 0 为启用 |
| `EnableAllCellArrays()` / `DisableAllCellArrays()` | 全部体字段 |
| `GetNumberOfFaceArrays()` / `GetFaceArrayName(index)` | 已登记的面字段 |
| `GetFaceArrayStatus(name)` / `SetFaceArrayStatus(name, status)` | 该面字段是否读取 |
| `EnableAllFaceArrays()` / `DisableAllFaceArrays()` | 全部面字段 |
| `SetExcludedFieldArrayNames(names)` | 设置排除名单 |
| `GetExcludedFieldArrayNames()` | 当前排除名单 |
| `ClearExcludedFieldArrayNames()` | 清空排除名单，回到白名单模式 |

### 非定常时间步

`LoadCaseMesh()` 成功返回 `1`，失败返回 `0`。同一 `FileName` 且网格已在内存中时直接返回 `1`。

`LoadDataTimeStep(datPath)` 返回该步下标；打不开、元数据或结果读取失败时返回 `-1`。调用前必须已经 `LoadCaseMesh()`。

| 方法 | 作用 |
|------|------|
| `LoadCaseMesh()` | 解析 case 并缓存拓扑、单元质心和单元邻接 |
| `LoadDataTimeStep(datPath)` | 追加一个 `.dat.h5`，并将其设为活动步 |
| `GetNumberOfLoadedTimeSteps()` | 已追加的步数 |
| `GetLoadedDataFileName(index)` | 该步的 dat 路径 |
| `GetFlowTime(index)` | `/settings` 中的 flow time；无记录或下标无效时为 NaN |
| `GetFluentTimeStep(index)` | Fluent time-step 整数；无效为 `-1` |
| `FindLoadedTimeStepIndexByFlowTime(flowTime, tolerance = 1e-9)` | 按流动时间查找，未找到为 `-1` |
| `FindLoadedTimeStepIndexByFluentStep(fluentTimeStep)` | 按 Fluent 步号查找，未找到为 `-1` |
| `GetTimeStepStorageMode(index)` | `TIME_STEP_FULL_DOUBLE`（`0`）或 `TIME_STEP_DELTA_FLOAT32`（`1`）；下标无效为 `-1` |
| `SetActiveTimeStepIndex(index)` / `GetActiveTimeStepIndex()` | 无下标的字段读取使用这一步。允许 `-1` |
| `SetReferenceTimeStepIndex(index)` / `GetReferenceTimeStepIndex()` | delta 的参考步。参考步必须是全量 double。`DELTA_CACHE` 关闭时设置被忽略 |
| `SetUseDeltaStorage(bool)` / `GetUseDeltaStorage()` | 为真时，参考步之后载入的步保存相对参考步的 `float` 增量。默认 `true`，且只在 `DELTA_CACHE` 打开时生效 |
| `SetEmitVtkOutput(bool)` / `GetEmitVtkOutput()` | 为假时 `Update()` 在同步活动步 chunk 后返回，不填充 multiblock。默认 `true` |
| `ReleaseDataTimeStep(index)` | 释放一步。若该步是仍有 delta 步依赖的参考步，调用失败并保留数据。其后的下标前移 |
| `ReleaseAllDataTimeSteps()` | 释放全部时间步，活动步和参考步回到 `-1` |
| `ReleaseCaseMesh()` | 释放网格缓存 |

`DELTA_CACHE` 关闭时，每一步都是 `TIME_STEP_FULL_DOUBLE`。打开时，参考步本身保持全量 double；其余在 `UseDeltaStorage` 为真时存 float 增量。按步取值时增量会加回参考步，接口仍返回 `double`。

### 体区域

| 方法 | 作用 |
|------|------|
| `GetCellZoneCount()` | 体区域个数，等于 multiblock 块数 |
| `GetCellZoneIdAtIndex(index)` | 该块的 Fluent zone id |
| `GetCellZoneName(index)` | 显示名（从 `zoneTopology/name` 解析） |
| `GetCellZoneNameById(zoneId)` | 按 zone id 取显示名 |
| `GetCellZoneCffNameById(zoneId)` | 原始 CFF 标签，例如 `fluid:fluid-main:1` |
| `GetCellZoneIndexByName(name)` | 显示名 → 块下标 |
| `GetCellZoneType(zoneId)` | `0`/`1` 流体，`2` 固体 |
| `GetCellZoneFirstCellIdAtIndex(index)` / `GetCellZoneLastCellIdAtIndex(index)` | 该区域在全局单元 id 中的闭区间 |

### 面区域

`GetNumberOfFaceZones()` 与 `GetFaceZoneCount()` 相同。

| 方法 | 作用 |
|------|------|
| `GetFaceZoneName(index)` | 面区域名 |
| `GetFaceZoneIndexByName(name)` | 名称 → 列表下标 |
| `GetFaceZoneIdByName(name)` / `GetFaceZoneNameById(zoneId)` | 名称与 Fluent zone id |
| `GetFaceZoneType(zoneId)` | CFF zoneType |
| `GetFaceZoneFirstFaceIdByName(name)` / `GetFaceZoneLastFaceIdByName(name)` | 全局面 id 闭区间 |
| `GetFaceZoneSizeByName(name)` | 区间内的面数；未知名称返回 `0` |
| `GetFaceIdByZoneName(name, localFaceIndex)` | 区域内局部下标 → 全局面 id |

### 单元、面、节点

越界时，计数类返回 `0`，id 类返回 `-1`。`GetFaceC0` / `GetFaceC1` 是面两侧的单元 id；边界面只有一侧有效，另一侧为负。

| 方法 | 作用 |
|------|------|
| `GetCellType(cellId)` | Fluent 单元类型，见上文 |
| `GetCellZoneId(cellId)` | 所属体区域 id |
| `GetCellNumberOfFaces(cellId)` / `GetCellFaceId(cellId, localFaceId)` | 单元的面 |
| `GetCellNumberOfNodes(cellId)` / `GetCellNodeId(cellId, localNodeId)` | 单元的节点 |
| `GetFaceType(faceId)` | 面的节点数 |
| `GetFaceZoneId(faceId)` | 所属面区域 id |
| `GetFaceNumberOfNodes(faceId)` / `GetFaceNodeId(faceId, localNodeId)` | 面的节点 |
| `GetFaceC0(faceId)` / `GetFaceC1(faceId)` | 相邻单元 |
| `GetNodeCoordinates(nodeId, double coords[3])` | 节点坐标。成功为 `true`；二维点的 z 为 `0` |

### 字段值与 chunk

无时间步参数的读取：若已有活动步，读活动步；否则读最近一次 `Update()` / 数据读取留下的 chunk。

| 方法 | 作用 |
|------|------|
| `GetCellArrayComponents(name)` / `GetFaceArrayComponents(name)` | 分量数；未知字段为 `0` |
| `GetCellArrayValue(name, cellId, component = 0)` | 活动步或当前 chunk 的体字段 |
| `GetFaceArrayValue(name, faceId, component = 0)` | 活动步或当前 chunk 的面字段 |
| `GetCellArrayValue(name, cellId, component, timeStepIndex)` | 指定步的体字段 |
| `GetFaceArrayValue(name, faceId, component, timeStepIndex)` | 指定步的面字段 |

整段数组（体、面各一套；下面以体字段为例，面字段把 `Cell` 换成 `Face`）：

| 无下标 | 指定时间步 |
|--------|------------|
| `GetLoadedCellChunkCount()` | `GetLoadedCellChunkCount(timeStepIndex)` |
| `GetLoadedCellChunkName(index)` | `GetLoadedCellChunkName(timeStepIndex, index)` |
| `GetLoadedCellChunkDim(index)` | `GetLoadedCellChunkDim(timeStepIndex, index)` |
| `GetLoadedCellChunkTupleCount(index)` | `GetLoadedCellChunkTupleCount(timeStepIndex, index)` |
| `GetLoadedCellChunkData(index)` | `GetLoadedCellChunkData(timeStepIndex, index)` |

`GetLoaded*ChunkData` 返回 `const double*`。delta 步在取出指针前会还原成 double。

### 面区域几何

| 方法 | 作用 |
|------|------|
| `CreateFaceZonePolyData(zoneName, faceArrayName = nullptr, component = 0)` | 该面区域的 `vtkPolyData`。给出字段名时，把该分量写成标量。拓扑按区域缓存 |
| `GetFaceZoneIndicesOverlappingFaceArray(faceArrayName, vtkIntArray* out)` | 哪些面区域的全局面 id 区间与该字段的 HDF5 section 相交。返回区域个数；参数为空或字段未知时为 `-1`，并清空 `out` |

### 网格派生量

网格载入后即可用（与 `FLUENT_CFF_ENABLE_GRAPH_DATA` 无关）：

| 方法 | 作用 |
|------|------|
| `GetCellCentroidCount()` | 单元质心个数 |
| `GetCellCentroids()` | `float[N * 3]`，按全局单元 id 排列。无数据时为 `nullptr` |
| `GetCellCentroidsByZone(zoneId, centroids)` | 该体区域的质心，`float[count * 3]`，写入并覆盖 `centroids` |
| `GetEdgeCount()` | 单元–单元有向邻接条数 |
| `GetEdgeIndex(src, dst)` | 邻接的源、目标单元 id。一条内部面贡献两个方向 |
| `GetBoundaryFaceEdgeCount()` | 边界面之间、共享一条边的有向邻接条数 |
| `GetBoundaryFaceEdgeIndex(src, dst)` | 上述邻接。下标是边界面在边界列表中的局部序号 |

`FLUENT_CFF_ENABLE_GRAPH_DATA` 打开后额外提供边界面几何。关闭时指针接口返回 `nullptr`，计数返回 `0`，`GetFaceBoundaryType` 返回 `-1`，按区域填充的 vector 会被清空。

| 方法 | 作用 |
|------|------|
| `GetFaceBoundaryType(faceId)` | `0` 内部面（两侧单元都有效），`1` 边界面 |
| `GetBoundaryFaceCount()` | 边界面个数 |
| `GetFaceCentroids()` / `GetFaceNormals()` | 仅边界面，`float[M * 3]`，顺序一致 |
| `GetFaceCentroidsByZone(zoneId, centroids)` / `GetFaceNormalsByZone(zoneId, normals)` | 该面区域在边界数组中的连续一段 |
| `GetFaceAreas()` / `GetFaceAreaCount()` | 边界面面积，与质心、法向同一顺序 |
| `GetCellFaceAreas()` / `GetCellFaceAreaCount()` | 每条单元邻接边对应的面面积，与 `GetEdgeIndex` 对齐。同一内部面的正反向边面积相同 |

面积为多边形面积（`float`）。三角形用叉积；四边及以上用 Newell 方法。

## 编译开关

开关定义在 [`vtk/IO/FLUENTCFF/vtkFLUENTCFFConfig.h`](vtk/IO/FLUENTCFF/vtkFLUENTCFFConfig.h)，默认都是关闭。`fluent_vtk_add_local_reader` 用 `PUBLIC` 编译定义传给库和链接它的目标，因此同一进程里的调用方必须使用同一组开关（头文件里的成员布局随开关变化）。

| CMake option | 宏 | 关闭时的接口 |
|--------------|----|--------------|
| `FLUENT_CFF_ENABLE_GRAPH_DATA` | 同名 | 仍有单元质心、`GetEdgeIndex`、边界面邻接。边界面质心、法向、面积为上面的空结果 |
| `FLUENT_CFF_ENABLE_DELTA_CACHE` | 同名 | `LoadDataTimeStep` 仍可用，每步全量 double。`SetReferenceTimeStepIndex` 不起作用 |

在调用 `fluent_vtk_add_local_reader` 之前设置 option：

```cmake
set(FLUENT_CFF_ENABLE_GRAPH_DATA ON CACHE BOOL "")
set(FLUENT_CFF_ENABLE_DELTA_CACHE ON CACHE BOOL "")
```

## 接到工程里

本仓库提供静态库入口 [`cmake/FluentVtk.cmake`](cmake/FluentVtk.cmake)，自身不单独生成可执行文件。

```cmake
set(FLUENT_VTK_ROOT "/path/to/vtk-fluentcff" CACHE PATH "vtk-fluentcff 根目录")
include("${FLUENT_VTK_ROOT}/cmake/FluentVtk.cmake")

fluent_vtk_add_local_reader(LocalFLUENTCFFReader)

find_package(VTK REQUIRED COMPONENTS
  CommonCore CommonDataModel CommonExecutionModel FiltersCore)
find_package(hdf5 CONFIG REQUIRED)

target_link_libraries(LocalFLUENTCFFReader PUBLIC
  VTK::CommonCore
  VTK::CommonDataModel
  VTK::CommonExecutionModel
  VTK::FiltersCore
  hdf5::hdf5
)

target_link_libraries(my_app PRIVATE LocalFLUENTCFFReader)
```

未设置 `FLUENT_VTK_ROOT` 时，默认为 `FluentVtk.cmake` 所在目录的上一级。

`fluent_vtk_add_local_reader(target)` 在目标尚不存在时创建静态库，并 `PUBLIC` 暴露头文件目录 `vtk/IO/FLUENTCFF`。编入的源文件：

| 源文件 | 内容 |
|--------|------|
| `vtkFLUENTCFFInternal.cxx` | HDF5 辅助与字段名映射 |
| `vtkFLUENTCFFReader.cxx` | 网格、字段、区域与派生量 |
| `vtkFLUENTCFFReaderTransient.cxx` | `LoadCaseMesh` / `LoadDataTimeStep` 及释放 |
| `cmake/vtkAOSDataArrayTemplateInstantiate.cxx` | 补齐部分 VTK 构建未导出的 `vtkAOSDataArrayTemplate` 符号 |

CMake 变量：

| 变量 | 含义 |
|------|------|
| `FLUENT_VTK_ROOT` | 本仓库根目录 |
| `FLUENT_VTK_IO` | `vtk/IO/FLUENTCFF` |
| `FLUENT_VTK_CMAKE` | `cmake/` |
| `FLUENT_CFF_ENABLE_GRAPH_DATA` | 边界面几何与面面积，默认 `OFF` |
| `FLUENT_CFF_ENABLE_DELTA_CACHE` | float delta 时间步，默认 `OFF` |

语言标准为 C++17。依赖 VTK `CommonCore`、`CommonDataModel`、`CommonExecutionModel`、`FiltersCore`，以及 HDF5。

若把源码放进整棵 VTK 树，可用 [`cmake/IOFLUENTCFF/CMakeLists.txt`](cmake/IOFLUENTCFF/CMakeLists.txt) 注册模块 `VTK::IOFLUENTCFF`。该模块清单目前只列出 `vtkFLUENTCFFReader` 与 `vtkFLUENTCFFInternal.cxx`；时间步 API 在 `vtkFLUENTCFFReaderTransient.cxx`，走这条路径时需要把它加入 `SOURCES`。直接使用 `fluent_vtk_add_local_reader` 时该文件已经包含在内。

## 目录

```
vtk/IO/FLUENTCFF/     vtkFLUENTCFFReader、Transient、Internal、编译开关
cmake/                FluentVtk.cmake、VTK 模块清单、模板实例化单元
docs/                 CFF 读取流程与 HDF5 字段树
scripts/              HDF5 检查脚本
```

格式细节：

| 主题 | 文件 |
|------|------|
| Agent 说明 | [docs/AGENTS.md](docs/AGENTS.md) |
| 读取流程与内存中的拓扑 | [docs/fluent-cff-dat-cas-structure.md](docs/fluent-cff-dat-cas-structure.md) |
| HDF5 字段树 | [docs/cff-v21-cas-dat-hdf5-field-tree.md](docs/cff-v21-cas-dat-hdf5-field-tree.md) |
| 已知 HDF5 源文件问题 | [docs/hdf5-errors.md](docs/hdf5-errors.md) |
