// SPDX-FileCopyrightText: Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
// SPDX-License-Identifier: BSD-3-Clause
/**
 * @class   vtkFLUENTCFFReader
 * @brief   reads a dataset in Fluent CFF file format
 *
 * vtkFLUENTCFFReader creates a multiblock dataset containing for each group cell
 * an unstructured grid dataset. It reads .cas.h5 and .dat.h5 files stored in FLUENT
 * CFF format (hdf5).
 *
 * The name of the arrays can be renamed with a more meaningful name that correspond to what Fluent
 * displays. The details for these renamings can be found here:
 * https://ansyshelp.ansys.com/public/account/secured?returnurl=/Views/Secured/corp/v242/en/flu_udf/flu_udf_DataAccessMacros.html
 * https://developer.ansys.com/docs/common-fluids-format-/_data_models_overview.html
 *
 * @par Thanks:
 * Original author : Arthur Piquet
 *
 * This class is based on the vtkFLUENTReader class from Brian W. Dotson &
 * Terry E. Jordan (Department of Energy, National Energy Technology
 * Laboratory) & Douglas McCorkle (Iowa State University)
 *
 * This class reads the HDF5 data in Fluent Format (face type structure)
 * and converts it to VTK Format (cell type structure).
 * This class could be improved for memory performance but the developer
 * will need to rewrite entirely the structure of the class.
 * Some piece of functionality lack in the HDF reading (overset, AMR tree,
 * interface), no file available in order to code/test the structure.
 *
 *
 * @sa
 * vtkFLUENTReader
 */

#ifndef vtkFLUENTCFFReader_h
#define vtkFLUENTCFFReader_h

#include <cstdint> // std::uint64_t
#include <limits>
#include <memory>  // std::unique_ptr
#include <string>
#include <utility> // std::pair
#include <vector>

#include "vtkIOFLUENTCFFModule.h" // For export macro

#include "vtkFLUENTCFFConfig.h"
#include "vtkFLUENTCFFInternal.h"

#include "vtkMultiBlockDataSetAlgorithm.h"
#include "vtkNew.h" // For vtkNew
#include "vtkSmartPointer.h"

VTK_ABI_NAMESPACE_BEGIN
class vtkDataArraySelection;
class vtkIntArray;
class vtkCellArray;
class vtkPoints;
class vtkTriangle;
class vtkTetra;
class vtkQuad;
class vtkHexahedron;
class vtkPyramid;
class vtkPolyData;
class vtkUnstructuredGrid;
class vtkWedge;

class VTKIOFLUENTCFF_EXPORT vtkFLUENTCFFReader : public vtkMultiBlockDataSetAlgorithm
{
public:
  static vtkFLUENTCFFReader* New();
  vtkTypeMacro(vtkFLUENTCFFReader, vtkMultiBlockDataSetAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  //@{
  /**
   * Specify the file name of the Fluent case file to read.
   * Changing the case file invalidates cached mesh and loaded time steps.
   */
  virtual void SetFileName(const std::string& fileName);
  vtkGetMacro(FileName, std::string);
  //@}

  //@{
  /**
   * Transient / multi-dat API: parse case mesh once, append dat time steps, optional delta storage.
   */
  enum TimeStepStorageMode
  {
    TIME_STEP_FULL_DOUBLE = 0,
    TIME_STEP_DELTA_FLOAT32 = 1
  };

  virtual int LoadCaseMesh();
  virtual int LoadDataTimeStep(const std::string& datPath);
  virtual int GetNumberOfLoadedTimeSteps() const;
  virtual double GetFlowTime(int timeStepIndex) const;
  virtual int GetFluentTimeStep(int timeStepIndex) const;
  virtual const char* GetLoadedDataFileName(int timeStepIndex) const;
  virtual int FindLoadedTimeStepIndexByFlowTime(double flowTime, double tolerance = 1e-9) const;
  virtual int FindLoadedTimeStepIndexByFluentStep(int fluentTimeStep) const;
  virtual int GetTimeStepStorageMode(int timeStepIndex) const;
  virtual void ReleaseDataTimeStep(int timeStepIndex);
  virtual void ReleaseAllDataTimeSteps();
  virtual void ReleaseCaseMesh();
  virtual bool GetMeshLoaded() const;
  virtual void SetActiveTimeStepIndex(int index);
  virtual int GetActiveTimeStepIndex() const;
  virtual void SetReferenceTimeStepIndex(int index);
  virtual int GetReferenceTimeStepIndex() const;
  vtkSetMacro(UseDeltaStorage, bool);
  vtkGetMacro(UseDeltaStorage, bool);
  vtkSetMacro(EmitVtkOutput, bool);
  vtkGetMacro(EmitVtkOutput, bool);
  //@}

  //@{
  /**
   * Optionally specify an explicit Fluent data file (.dat.h5).
   * When empty, the reader derives the data file name from the case file.
   */
  vtkSetMacro(DataFileName, std::string);
  vtkGetMacro(DataFileName, std::string);
  //@}

  //@{
  /**
   * Set/Get if we want to rename the arrays of the data with a more meaningful
   * name from common fluids data model or not.
   * False by default.
   */
  vtkSetMacro(RenameArrays, bool);
  vtkGetMacro(RenameArrays, bool);
  //@}

  //@{
  /**
   * Get the total number of cells. The number of cells is only valid after a
   * successful read of the data file is performed. Initial value is 0.
   */
  vtkGetMacro(NumberOfCells, vtkIdType);
  //@}

  /**
   * Get the number of cell arrays available in the input.
   */
  int GetNumberOfCellArrays();

  /**
   * Get the name of the cell array with the given index in
   * the input.
   */
  const char* GetCellArrayName(int index);

  //@{
  /**
   * Get/Set whether the cell array with the given name is to
   * be read.
   */
  int GetCellArrayStatus(const char* name);
  void SetCellArrayStatus(const char* name, int status);
  //@}

  //@{
  /**
   * Turn on/off all cell arrays.
   */
  void DisableAllCellArrays();
  void EnableAllCellArrays();
  //@}

  /**
   * Get the number of face arrays available in the input.
   */
  int GetNumberOfFaceArrays();

  /**
   * Get the name of the face array with the given index in
   * the input.
   */
  const char* GetFaceArrayName(int index);

  //@{
  /**
   * Get/Set whether the face array with the given name is to
   * be read.
   */
  int GetFaceArrayStatus(const char* name);
  void SetFaceArrayStatus(const char* name, int status);
  //@}

  //@{
  /**
   * Turn on/off all face arrays.
   */
  void DisableAllFaceArrays();
  void EnableAllFaceArrays();
  //@}

  //@{
  /**
   * Exclude these field array names from reading HDF5 cell/face chunks in GetData().
   * Names must match the registry used by vtkDataArraySelection (same rules as ReadDataForType:
   * RenameArrays mapping and optional "-phase_N" suffix for phase index > 1).
   */
  virtual void SetExcludedFieldArrayNames(const std::vector<std::string>& names);
  virtual void ClearExcludedFieldArrayNames();
  virtual const std::vector<std::string>& GetExcludedFieldArrayNames() const;
  //@}

  vtkIdType GetNumberOfNodesRead() const;
  vtkIdType GetNumberOfFacesRead() const;
  int GetNumberOfFaceZones() const;
  int GetFaceZoneCount() const;
  const char* GetFaceZoneName(int index) const;
  int GetFaceZoneIndexByName(const char* name) const;
  int GetFaceZoneIdByName(const char* name) const;
  vtkIdType GetFaceZoneFirstFaceIdByName(const char* name) const;
  vtkIdType GetFaceZoneLastFaceIdByName(const char* name) const;
  vtkIdType GetFaceZoneSizeByName(const char* name) const;
  int GetFaceIdByZoneName(const char* name, vtkIdType localFaceIndex) const;

  //@{
  /**
   * Query FaceZone by zoneId.
   * @param zoneId Fluent zone id (not zone index).
   * @return Pointer to zone name string, or nullptr if not found.
   */
  const char* GetFaceZoneNameById(int zoneId) const;

  /**
   * Get FaceZone zoneType integer.
   * @param zoneId Fluent zone id (not zone index).
   * @return zoneType value (e.g., 3=wall, 7=symmetry), or -1 if not found.
   */
  int GetFaceZoneType(int zoneId) const;

  /**
   * Get CellZone zoneType integer.
   * @param zoneId Fluent zone id (not zone index).
   * @return zoneType value (0/1=fluid, 2=solid), or -1 if not found.
   */
  int GetCellZoneType(int zoneId) const;
  //@}

  /**
   * Number of cell-zone blocks in the multiblock output (same as CellZones.size() after Update).
   */
  int GetCellZoneCount() const;

  /**
   * Fluent cell zone id for output block index \p index (0 <= index < GetCellZoneCount()).
   */
  int GetCellZoneIdAtIndex(int index) const;

  /**
   * Display name for cell zone at output block index \p index (parsed from CFF zoneTopology/name).
   */
  const char* GetCellZoneName(int index) const;

  /**
   * Query CellZone by zoneId.
   * @param zoneId Fluent zone id (not zone index).
   * @return Pointer to display name string, or nullptr if not found.
   */
  const char* GetCellZoneNameById(int zoneId) const;

  /**
   * Raw CFF label from zoneTopology/name (e.g. "fluid:fluid-main:1").
   */
  const char* GetCellZoneCffNameById(int zoneId) const;

  int GetCellZoneIndexByName(const char* name) const;
  vtkIdType GetCellZoneFirstCellIdAtIndex(int index) const;
  vtkIdType GetCellZoneLastCellIdAtIndex(int index) const;

  int GetCellType(vtkIdType cellId) const;
  int GetCellZoneId(vtkIdType cellId) const;
  void GetCellCentroidsByZone(int zoneId, std::vector<float>& centroids) const;
  vtkIdType GetCellNumberOfFaces(vtkIdType cellId) const;
  int GetCellFaceId(vtkIdType cellId, vtkIdType localFaceId) const;
  vtkIdType GetCellNumberOfNodes(vtkIdType cellId) const;
  int GetCellNodeId(vtkIdType cellId, vtkIdType localNodeId) const;

  int GetFaceType(vtkIdType faceId) const;
  int GetFaceZoneId(vtkIdType faceId) const;
  vtkIdType GetFaceNumberOfNodes(vtkIdType faceId) const;
  int GetFaceNodeId(vtkIdType faceId, vtkIdType localNodeId) const;
  int GetFaceC0(vtkIdType faceId) const;
  int GetFaceC1(vtkIdType faceId) const;

  //@{
  /**
   * Graph data for GNN.
   * Cell centroids and edge index (adjacency) constructed from face connectivity.
   */
  int GetCellCentroidCount() const;
  const float* GetCellCentroids() const;
  int GetEdgeCount() const;
  void GetEdgeIndex(std::vector<int>& src, std::vector<int>& dst) const;
  int GetFaceBoundaryType(vtkIdType faceId) const;  // 0=内部, 1=边界

  // Boundary face data (non-interior faces only)
  int GetBoundaryFaceCount() const;
  const float* GetFaceCentroids() const;
  const float* GetFaceNormals() const;
  void GetFaceCentroidsByZone(int zoneId, std::vector<float>& centroids) const;
  void GetFaceNormalsByZone(int zoneId, std::vector<float>& normals) const;

  // Face area data (boundary-only, same count/order as GetFaceCentroids/GetFaceNormals)
  const float* GetFaceAreas() const;
  int GetFaceAreaCount() const;
  // CellFaceAreas: per-edge face area, aligned with edge_index
  const float* GetCellFaceAreas() const;
  int GetCellFaceAreaCount() const;

  // Boundary face adjacency via shared node-pair (edge) detection.
  // Returns COO edge_index of boundary face local indices that share an edge.
  int GetBoundaryFaceEdgeCount() const;
  void GetBoundaryFaceEdgeIndex(std::vector<int>& src, std::vector<int>& dst) const;
  //@}

  bool GetNodeCoordinates(vtkIdType nodeId, double coords[3]) const;

  int GetCellArrayComponents(const char* name) const;
  int GetFaceArrayComponents(const char* name) const;
  double GetCellArrayValue(const char* name, vtkIdType cellId, int component = 0) const;
  double GetFaceArrayValue(const char* name, vtkIdType faceId, int component = 0) const;
  double GetCellArrayValue(
    const char* name, vtkIdType cellId, int component, int timeStepIndex) const;
  double GetFaceArrayValue(
    const char* name, vtkIdType faceId, int component, int timeStepIndex) const;

  //@{
  /**
   * Access loaded data chunks (after Update() with data file present).
   * These are the underlying containers used by GetCellArrayValue/GetFaceArrayValue.
   *
   * Notes:
   * - Returned pointers remain valid until the next ClearData()/Update() that reloads data.
   * - `dim` is number of components per tuple.
   * - `dataVector` is laid out as tuple-major: data[(tupleId * dim) + component].
   */
  int GetLoadedCellChunkCount() const;
  int GetLoadedCellChunkCount(int timeStepIndex) const;
  const char* GetLoadedCellChunkName(int index) const;
  const char* GetLoadedCellChunkName(int timeStepIndex, int index) const;
  int GetLoadedCellChunkDim(int index) const;
  int GetLoadedCellChunkDim(int timeStepIndex, int index) const;
  const double* GetLoadedCellChunkData(int index) const;
  const double* GetLoadedCellChunkData(int timeStepIndex, int index) const;
  vtkIdType GetLoadedCellChunkTupleCount(int index) const;
  vtkIdType GetLoadedCellChunkTupleCount(int timeStepIndex, int index) const;

  int GetLoadedFaceChunkCount() const;
  int GetLoadedFaceChunkCount(int timeStepIndex) const;
  const char* GetLoadedFaceChunkName(int index) const;
  const char* GetLoadedFaceChunkName(int timeStepIndex, int index) const;
  int GetLoadedFaceChunkDim(int index) const;
  int GetLoadedFaceChunkDim(int timeStepIndex, int index) const;
  const double* GetLoadedFaceChunkData(int index) const;
  const double* GetLoadedFaceChunkData(int timeStepIndex, int index) const;
  vtkIdType GetLoadedFaceChunkTupleCount(int index) const;
  vtkIdType GetLoadedFaceChunkTupleCount(int timeStepIndex, int index) const;
  //@}

  vtkSmartPointer<vtkPolyData> CreateFaceZonePolyData(const char* zoneName,
    const char* faceArrayName = nullptr, int component = 0) const;

  /**
   * After Update() with a face array loaded, return face zone indices (0..GetNumberOfFaceZones()-1)
   * whose Fluent global face id range intersects any HDF5 section stored for \p faceArrayName.
   *
   * \param faceArrayName  Must match the loaded array name (GetFaceArrayName), including
   * RenameArrays renames and multi-phase suffix (e.g. SV_P-phase_1 for the second phase).
   * \param outZoneIndices  Cleared and filled with zone indices (sorted by increasing index).
   * \return Number of overlapping zones, or -1 if \p faceArrayName / \p outZoneIndices is null or
   * the array is unknown.
   */
  int GetFaceZoneIndicesOverlappingFaceArray(const char* faceArrayName, vtkIntArray* outZoneIndices) const;

  //@{
  //
  //  Structures
  //
  struct Cell
  {
    int type;
    std::vector<int> faces;
    int parent;
    int child;
    vtkIdType nodePoolOffset = 0;
    int nodePoolCount = 0;
    vtkIdType nodeOffsetPoolOffset = 0;
    int nodeOffsetPoolCount = 0;
    vtkIdType uniqueNodePoolOffset = 0;
    int uniqueNodePoolCount = 0;
    std::vector<int> nodes;
    std::vector<int> nodesOffset;
    std::vector<int> childId;
  };
  struct Face
  {
    int type;                  // 面类型 (VTK cell type: 3=三角形, 4=四边形)
    vtkIdType nodeOffset = 0;  // 节点偏移量 (全局节点数组中的起始位置)
    int nodeCount = 0;         // 构成面的节点数量
    int c0;                    // 相邻单元0 (该面一侧的单元ID)
    int c1;                    // 相邻单元1 (该面另一侧的单元ID，内部面有两个，边界只有一个)
    int periodicShadow;        // 周期性对端面ID (周期性边界上对应的另一个面)
    int parent;                // 父面 (AMR自适应网格粗化时使用)
    int child;                 // 子面 (AMR自适应网格细化时使用)
    int interfaceFaceParent;   // 界面面父 (多级非结构网格中父级别的界面面ID)
    int interfaceFaceChild;    // 界面面子 (多级非结构网格中子级别的界面面ID)
    int ncgParent;             // NCG父单元ID (Non-Conforming Grid父单元)
    int ncgChild;              // NCG子单元ID (Non-Conforming Grid子单元)
  };
//*Faces 在大Faces数组中连续储存，应删除Face.zone 只需要判断数组索引在哪个 FaceZoneInfo .firstFaceId .lastFaceId 范围内既能得到对应 zone name ，cell同理
  struct FaceZoneInfo
  {
    std::string name;
    int id = -1;
    vtkIdType firstFaceId = -1;
    vtkIdType lastFaceId = -1;
    int zoneType = -1;
  };
  //@}

protected:
  vtkFLUENTCFFReader();
  ~vtkFLUENTCFFReader() override;
  int RequestInformation(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

  /**
   * Enumerate
   */
  enum DataState
  {
    NOT_LOADED = 0,
    AVAILABLE = 1,
    LOADED = 2,
    ERROR = 3
  };

  //@{
  /**
   * Open the HDF5 file structure
   */
  virtual bool OpenCaseFile(const std::string& filename);
  virtual DataState OpenDataFile(const std::string& filename);
  virtual DataState OpenDataFileAtPath(const std::string& datPath);
  virtual void CloseDataFile();
  //@}

  /**
   * Retrieve the number of cell zones
   */
  virtual void GetNumberOfCellZones();

  /**
   * Reads necessary information from the .cas file
   */
  virtual int ParseCaseFile();

  /**
   * Get the dimension of the file (2D/3D)
   */
  virtual int GetDimension();

  //@{
  /**
   * Get the total number of nodes/cells/faces
   */
  virtual void GetNodesGlobal();
  virtual void GetCellsGlobal();
  virtual void GetFacesGlobal();
  //@}

  /**
   * Get the size and index of node per zone
   */
  virtual void GetNodes();

  /**
   * Get the topology of cell per zone
   */
  virtual void GetCells();

  /**
   * Build cell centroids and cell-cell edge_index (always available).
   */
  virtual void BuildCellGraphData();

  /**
   * Build extended GNN graph data (face areas, boundary centroids/normals); requires GRAPH_DATA.
   */
  virtual void BuildGraphData();

  /**
   * Get the topology of face per zone
   */
  virtual void GetFaces();

  /**
   * Get the periodic shadown faces information
   * !!! NOT IMPLEMENTED YET !!!
   */
  virtual void GetPeriodicShadowFaces();

  /**
   * Get the overset cells information
   * !!! NOT IMPLEMENTED YET !!!
   */
  virtual void GetCellOverset();

  /**
   * Get the tree (AMR) face topology
   */
  virtual void GetFaceTree();

  /**
   * Get the interface id of parent faces
   */
  virtual void GetInterfaceFaceParents();

  /**
   * Get the non conformal grid interface information
   * !!! NOT IMPLEMENTED YET !!!
   */
  virtual void GetNonconformalGridInterfaceFaceInformation();

  /**
   * Removes unnecessary faces from the cells
   */
  virtual void CleanCells();

  //@{
  /**
   * Reconstruct and convert the Fluent data format
   * to the VTK format
   */
  virtual void PopulateCellNodes();
  //@}

  //@{
  /**
   * Reconstruct VTK cell topology from Fluent format
   */
  virtual void PopulateTriangleCell(int i);
  virtual void PopulateTetraCell(int i);
  virtual void PopulateQuadCell(int i);
  virtual void PopulateHexahedronCell(int i);
  virtual void PopulatePyramidCell(int i);
  virtual void PopulateWedgeCell(int i);
  virtual void PopulatePolyhedronCell(int i);
  //@}

  /**
   * Read and reconstruct data from .dat.h5 file
   */
  virtual int GetData();

  /**
   * Pre-read variable name data available for selection
   */
  virtual int GetMetaData();

  virtual void ResetMeshState();
  virtual void ClearData();
  virtual void ClearTransientReadBuffers();
  virtual void InvalidateMeshCache();
  virtual int BuildMeshFromCase();
  virtual int FinalizeLoadedTimeStepFromBuffers(
    const std::string& datPath, const vtkFLUENTCFFInternal::FluentDatSettings& settings);
  virtual bool SyncCellDataChunksForActiveTimeStep();
  virtual bool SyncFaceDataChunksForActiveTimeStep();

  //
  //  Variables
  //
  vtkNew<vtkDataArraySelection> CellDataArraySelection;
  vtkNew<vtkDataArraySelection> FaceDataArraySelection;
  /** Applied in RequestData immediately before GetData(). */
  std::vector<std::string> ExcludedFieldArrayNames;
  std::string FileName;
  std::string DataFileName;
  bool RenameArrays = false;
  bool MeshLoaded = false;
  bool UseDeltaStorage = true;
  bool EmitVtkOutput = true;
  int ActiveTimeStepIndex = -1;
  int ReferenceTimeStepIndex = -1;
  std::string CaseFileNameLoaded;
  vtkIdType NumberOfCells = 0;
  int NumberOfCellArrays = 0;

  struct vtkInternals;
  std::unique_ptr<vtkInternals> HDFImpl;

  vtkNew<vtkPoints> Points;
  vtkNew<vtkTriangle> Triangle;
  vtkNew<vtkTetra> Tetra;
  vtkNew<vtkQuad> Quad;
  vtkNew<vtkHexahedron> Hexahedron;
  vtkNew<vtkPyramid> Pyramid;
  vtkNew<vtkWedge> Wedge;

  std::vector<Cell> Cells;
  std::vector<Face> Faces;
  std::vector<int> FaceNodePool;
  std::vector<int> CellNodePool;
  std::vector<int> CellNodeOffsetPool;
  std::vector<vtkIdType> CellUniqueNodePool;
  std::vector<int> CellZones;
  std::vector<int> CellZonesType;
  std::map<int, int> CellZoneIdToType;
  struct CellZoneInfo
  {
    int id = -1;
    vtkIdType firstCellId = -1;
    vtkIdType lastCellId = -1;
    std::string name;
    std::string cffName;
  };
  std::vector<CellZoneInfo> CellZonesInfo;

  struct CellSpan
  {
    vtkIdType firstCellId = -1;
    vtkIdType count = 0;
  };
  std::map<int, CellSpan> CellZoneIdToCellSpan;  // zoneId -> contiguous cellId span

  // CellId -> Fluent zoneId mapping (size == Cells.size(), -1 if unknown)
  std::vector<int> CellIdToZoneId;
  std::vector<FaceZoneInfo> FaceZones;

  // Cell graph (centroids + cell-cell edge_index); always built after mesh load.
  std::vector<float> CellCentroids;  // [N_cells * 3] flatten
  std::vector<int> EdgeIndexSrc;
  std::vector<int> EdgeIndexDst;

#if FLUENT_CFF_ENABLE_GRAPH_DATA
  // Extended GNN geometry (boundary faces, polygon areas, edge weights)
  std::vector<float> FaceCentroids;     // [N_boundary_faces * 3]
  std::vector<float> FaceNormals;       // [N_boundary_faces * 3]
  std::vector<float> FaceAreas;         // [N_boundary_faces] boundary face areas, same order as FaceCentroids/FaceNormals
  struct BoundaryFaceSpan
  {
    vtkIdType firstBoundaryFaceIndex = -1;  // index into boundary arrays (FaceCentroids/FaceNormals/FaceAreas)
    vtkIdType count = 0;                   // number of faces in this span
  };
  std::map<int, BoundaryFaceSpan> FaceZoneIdToBoundarySpan;  // zoneId -> contiguous span in boundary arrays

  // Face area data (polygon-accurate)
  std::vector<float> FaceAreasAll;    // [F] area of every face (internal + boundary), internal use
  std::vector<float> CellFaceAreas;   // [E] face area per directed edge, aligned with EdgeIndexSrc/Dst
#endif

  // FaceId -> Fluent zoneId mapping (size == Faces.size(), -1 if unknown)
  std::vector<int> FaceIdToZoneId;

  vtkTypeBool SwapBytes = 0;
  int GridDimension = 0;
  DataState FileState = DataState::NOT_LOADED;

private:
  vtkFLUENTCFFReader(const vtkFLUENTCFFReader&) = delete;
  void operator=(const vtkFLUENTCFFReader&) = delete;

  const CellZoneInfo* LookupCellZoneInfo(int zoneId) const;

  void ApplyExcludedFieldArrayNames();

  struct DataChunk
  {
    std::string variableName;
    size_t dim;
    std::vector<double> dataVector;
    /** Populated for face arrays: Fluent 1-based inclusive id range per HDF5 section. */
    std::vector<std::pair<std::uint64_t, std::uint64_t>> FaceSectionFluentIdRanges1Based;
  };

#if FLUENT_CFF_ENABLE_DELTA_CACHE
  struct DeltaDataChunk
  {
    std::string variableName;
    size_t dim = 0;
    std::vector<float> deltaVector;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> FaceSectionFluentIdRanges1Based;
  };
#endif

  struct LoadedTimeStep
  {
    std::string dataFileName;
    double flowTime = std::numeric_limits<double>::quiet_NaN();
    int fluentTimeStep = -1;
    TimeStepStorageMode storageMode = TIME_STEP_FULL_DOUBLE;
    std::vector<DataChunk> cellDataChunks;
    std::vector<DataChunk> faceDataChunks;
#if FLUENT_CFF_ENABLE_DELTA_CACHE
    std::vector<DeltaDataChunk> cellDeltaChunks;
    std::vector<DeltaDataChunk> faceDeltaChunks;
#endif
  };

  int ReadMetadataForType(const std::string& groupName, vtkDataArraySelection* selection,
    std::vector<std::string>& preReadData, int phaseIndex);
  int ReadDataForType(const std::string& groupName, vtkDataArraySelection* selection,
    std::vector<DataChunk>& chunks, vtkIdType tupleCount, int phaseIndex);
  const DataChunk* FindDataChunk(const std::vector<DataChunk>& chunks, const char* name) const;
#if FLUENT_CFF_ENABLE_DELTA_CACHE
  const DeltaDataChunk* FindDeltaDataChunk(
    const std::vector<DeltaDataChunk>& chunks, const char* name) const;
  void ConvertChunksToDelta(const std::vector<DataChunk>& referenceChunks,
    const std::vector<DataChunk>& currentChunks, std::vector<DeltaDataChunk>& outDelta) const;
#endif
  const LoadedTimeStep* GetLoadedTimeStep(int timeStepIndex) const;
  LoadedTimeStep* GetLoadedTimeStep(int timeStepIndex);
  int FindLoadedTimeStepIndexByPath(const std::string& datPath) const;
  bool HasDeltaStepsDependingOnReference() const;
  double LookupCellValueFromStep(const LoadedTimeStep& step, const char* name, vtkIdType cellId,
    int component) const;
  double LookupFaceValueFromStep(const LoadedTimeStep& step, const char* name, vtkIdType faceId,
    int component) const;
  bool MaterializeCellChunkForStep(int timeStepIndex, int chunkIndex) const;
  bool MaterializeFaceChunkForStep(int timeStepIndex, int chunkIndex) const;
  void EnsureFaceZoneTopologyCache(int zoneIndex) const;

  std::vector<DataChunk> CellDataChunks;
  std::vector<DataChunk> FaceDataChunks;
  std::vector<LoadedTimeStep> LoadedTimeSteps;
  mutable std::vector<DataChunk> ReconstructedCellChunkScratch;
  mutable std::vector<DataChunk> ReconstructedFaceChunkScratch;
  mutable std::vector<double> SectionReadBufferDouble;
  mutable std::vector<float> SectionReadBufferFloat;
  std::vector<std::string> PreReadCellData;
  std::vector<std::string> PreReadFaceData;
  int NumberOfArrays = 0;
  std::vector<std::vector<vtkIdType>> CellIndicesByZone;

  struct FaceZoneTopologyCache
  {
    bool Built = false;
    vtkSmartPointer<vtkCellArray> Polys;
    std::vector<vtkIdType> FaceIds;
  };
  mutable std::vector<FaceZoneTopologyCache> FaceZoneTopologyCaches;

  /**
   * UDM arrays of N components must be split in N scalar arrays
   */
  void ParseUDMData(
    std::vector<vtkSmartPointer<vtkUnstructuredGrid>>& grid, const DataChunk& vectorDataChunk);
};
VTK_ABI_NAMESPACE_END
#endif
