// SPDX-FileCopyrightText: Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
// SPDX-License-Identifier: BSD-3-Clause

#include "vtkFLUENTCFFReader.h"

#include "vtkDataArraySelection.h"
#include "vtkFLUENTCFFInternal.h"

#include "vtk_hdf5.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

VTK_ABI_NAMESPACE_BEGIN

struct vtkFLUENTCFFReader::vtkInternals
{
  hid_t FluentCaseFile;
  hid_t FluentDataFile;
};

namespace
{
bool PathsEqual(const std::string& a, const std::string& b)
{
  if (a == b)
  {
    return true;
  }
#ifdef _WIN32
  if (a.size() == b.size())
  {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
      [](char ca, char cb) { return std::tolower(static_cast<unsigned char>(ca)) ==
                                std::tolower(static_cast<unsigned char>(cb)); });
  }
#endif
  return false;
}
} // namespace

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::SetFileName(const std::string& fileName)
{
  if (this->FileName != fileName)
  {
    this->ReleaseAllDataTimeSteps();
    this->InvalidateMeshCache();
    this->FileName = fileName;
    this->Modified();
  }
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::CloseDataFile()
{
  if (this->HDFImpl->FluentDataFile >= 0)
  {
    H5Fclose(this->HDFImpl->FluentDataFile);
    this->HDFImpl->FluentDataFile = -1;
  }
}

//------------------------------------------------------------------------------
vtkFLUENTCFFReader::DataState vtkFLUENTCFFReader::OpenDataFileAtPath(const std::string& datPath)
{
  this->CloseDataFile();
  const htri_t fileType = H5Fis_hdf5(datPath.c_str());
  if (fileType == 0)
  {
    return DataState::ERROR;
  }
  if (fileType < 0)
  {
    return DataState::NOT_LOADED;
  }
  this->HDFImpl->FluentDataFile = H5Fopen(datPath.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
  return DataState::AVAILABLE;
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::ClearTransientReadBuffers()
{
  this->CellDataChunks.clear();
  this->FaceDataChunks.clear();
  this->PreReadCellData.clear();
  this->PreReadFaceData.clear();
  this->NumberOfArrays = 0;
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::ClearData()
{
  this->ClearTransientReadBuffers();
  this->CellDataArraySelection->RemoveAllArrays();
  this->FaceDataArraySelection->RemoveAllArrays();
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::InvalidateMeshCache()
{
  this->ResetMeshState();
  this->MeshLoaded = false;
  this->CaseFileNameLoaded.clear();
  if (this->HDFImpl->FluentCaseFile >= 0)
  {
    H5Fclose(this->HDFImpl->FluentCaseFile);
    this->HDFImpl->FluentCaseFile = -1;
  }
  this->GridDimension = 0;
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::ReleaseCaseMesh()
{
  this->ReleaseAllDataTimeSteps();
  this->InvalidateMeshCache();
}

//------------------------------------------------------------------------------
bool vtkFLUENTCFFReader::GetMeshLoaded() const
{
  return this->MeshLoaded;
}

//------------------------------------------------------------------------------
const vtkFLUENTCFFReader::LoadedTimeStep* vtkFLUENTCFFReader::GetLoadedTimeStep(
  int timeStepIndex) const
{
  if (timeStepIndex < 0 ||
    static_cast<std::size_t>(timeStepIndex) >= this->LoadedTimeSteps.size())
  {
    return nullptr;
  }
  return &this->LoadedTimeSteps[static_cast<std::size_t>(timeStepIndex)];
}

//------------------------------------------------------------------------------
vtkFLUENTCFFReader::LoadedTimeStep* vtkFLUENTCFFReader::GetLoadedTimeStep(int timeStepIndex)
{
  if (timeStepIndex < 0 ||
    static_cast<std::size_t>(timeStepIndex) >= this->LoadedTimeSteps.size())
  {
    return nullptr;
  }
  return &this->LoadedTimeSteps[static_cast<std::size_t>(timeStepIndex)];
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::FindLoadedTimeStepIndexByPath(const std::string& datPath) const
{
  for (std::size_t i = 0; i < this->LoadedTimeSteps.size(); ++i)
  {
    if (PathsEqual(this->LoadedTimeSteps[i].dataFileName, datPath))
    {
      return static_cast<int>(i);
    }
  }
  return -1;
}

//------------------------------------------------------------------------------
bool vtkFLUENTCFFReader::HasDeltaStepsDependingOnReference() const
{
#if FLUENT_CFF_ENABLE_DELTA_CACHE
  if (this->ReferenceTimeStepIndex < 0)
  {
    return false;
  }
  for (const auto& step : this->LoadedTimeSteps)
  {
    if (step.storageMode == TIME_STEP_DELTA_FLOAT32)
    {
      return true;
    }
  }
#endif
  return false;
}

#if FLUENT_CFF_ENABLE_DELTA_CACHE
//------------------------------------------------------------------------------
const vtkFLUENTCFFReader::DeltaDataChunk* vtkFLUENTCFFReader::FindDeltaDataChunk(
  const std::vector<DeltaDataChunk>& chunks, const char* name) const
{
  if (!name)
  {
    return nullptr;
  }
  for (const auto& chunk : chunks)
  {
    if (chunk.variableName == name)
    {
      return &chunk;
    }
  }
  return nullptr;
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::ConvertChunksToDelta(const std::vector<DataChunk>& referenceChunks,
  const std::vector<DataChunk>& currentChunks, std::vector<DeltaDataChunk>& outDelta) const
{
  outDelta.clear();
  outDelta.reserve(currentChunks.size());
  for (const auto& current : currentChunks)
  {
    const DataChunk* refChunk = this->FindDataChunk(referenceChunks, current.variableName.c_str());
    if (!refChunk || refChunk->dim != current.dim ||
      refChunk->dataVector.size() != current.dataVector.size())
    {
      vtkErrorMacro("Delta storage: missing or mismatched reference field \""
        << current.variableName << "\".");
      continue;
    }
    DeltaDataChunk delta;
    delta.variableName = current.variableName;
    delta.dim = current.dim;
    delta.FaceSectionFluentIdRanges1Based = current.FaceSectionFluentIdRanges1Based;
    delta.deltaVector.resize(current.dataVector.size());
    for (std::size_t i = 0; i < current.dataVector.size(); ++i)
    {
      delta.deltaVector[i] =
        static_cast<float>(current.dataVector[i] - refChunk->dataVector[i]);
    }
    outDelta.push_back(std::move(delta));
  }
}
#endif  // FLUENT_CFF_ENABLE_DELTA_CACHE

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::BuildMeshFromCase()
{
  this->ResetMeshState();
  const int parseFlag = this->ParseCaseFile();
  if (parseFlag == 0)
  {
    vtkErrorMacro("Unable to read the Case CFF file.");
    return 0;
  }
  try
  {
    this->CleanCells();
    this->PopulateCellNodes();
    this->GetNumberOfCellZones();
    this->BuildCellGraphData();
#if FLUENT_CFF_ENABLE_GRAPH_DATA
    this->BuildGraphData();
#endif
  }
  catch (const std::runtime_error& e)
  {
    vtkErrorMacro(<< e.what());
    return 0;
  }
  this->NumberOfCells = static_cast<vtkIdType>(this->Cells.size());
  return 1;
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::LoadCaseMesh()
{
  if (this->FileName.empty())
  {
    vtkErrorMacro("FileName has to be specified!");
    return 0;
  }
  if (this->MeshLoaded && this->CaseFileNameLoaded == this->FileName)
  {
    return 1;
  }
  this->ReleaseAllDataTimeSteps();
  if (this->CaseFileNameLoaded != this->FileName)
  {
    this->InvalidateMeshCache();
  }
  if (this->HDFImpl->FluentCaseFile < 0)
  {
    if (!this->OpenCaseFile(this->FileName))
    {
      vtkErrorMacro("Unable to open case file.");
      return 0;
    }
  }
  this->GridDimension = this->GetDimension();
  if (this->GridDimension == 0)
  {
    return 0;
  }
  if (!this->BuildMeshFromCase())
  {
    return 0;
  }
  this->MeshLoaded = true;
  this->CaseFileNameLoaded = this->FileName;
  return 1;
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::FinalizeLoadedTimeStepFromBuffers(
  const std::string& datPath, const vtkFLUENTCFFInternal::FluentDatSettings& settings)
{
  const int newIndex = static_cast<int>(this->LoadedTimeSteps.size());
  LoadedTimeStep step;
  step.dataFileName = datPath;
  step.flowTime = settings.flowTimeValid ? settings.flowTime
                                         : std::numeric_limits<double>::quiet_NaN();
  step.fluentTimeStep = settings.timeStep;

#if FLUENT_CFF_ENABLE_DELTA_CACHE
  const bool storeDelta = this->UseDeltaStorage && this->ReferenceTimeStepIndex >= 0 &&
    newIndex != this->ReferenceTimeStepIndex;
  if (storeDelta)
  {
    const LoadedTimeStep* refStep = this->GetLoadedTimeStep(this->ReferenceTimeStepIndex);
    if (!refStep || refStep->storageMode != TIME_STEP_FULL_DOUBLE)
    {
      vtkErrorMacro("Reference time step must be loaded as full double before delta steps.");
      return -1;
    }
    step.storageMode = TIME_STEP_DELTA_FLOAT32;
    this->ConvertChunksToDelta(
      refStep->cellDataChunks, this->CellDataChunks, step.cellDeltaChunks);
    this->ConvertChunksToDelta(
      refStep->faceDataChunks, this->FaceDataChunks, step.faceDeltaChunks);
  }
  else
#endif
  {
    step.storageMode = TIME_STEP_FULL_DOUBLE;
    step.cellDataChunks = std::move(this->CellDataChunks);
    step.faceDataChunks = std::move(this->FaceDataChunks);
  }

  this->LoadedTimeSteps.push_back(std::move(step));
  this->ClearTransientReadBuffers();
  this->ActiveTimeStepIndex = newIndex;
  this->DataFileName = datPath;
  this->FileState = DataState::LOADED;
  return newIndex;
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::LoadDataTimeStep(const std::string& datPath)
{
  if (!this->MeshLoaded)
  {
    vtkErrorMacro("LoadCaseMesh() must be called before LoadDataTimeStep().");
    return -1;
  }
  if (datPath.empty())
  {
    vtkErrorMacro("datPath is empty.");
    return -1;
  }

  const int existing = this->FindLoadedTimeStepIndexByPath(datPath);
  if (existing >= 0)
  {
    this->ActiveTimeStepIndex = existing;
    return existing;
  }

  this->FileState = this->OpenDataFileAtPath(datPath);
  if (this->FileState != DataState::AVAILABLE)
  {
    vtkErrorMacro("Unable to open dat file: " << datPath);
    return -1;
  }

  vtkFLUENTCFFInternal::FluentDatSettings settings;
  if (!vtkFLUENTCFFInternal::ParseDatSettingsFromH5(this->HDFImpl->FluentDataFile, settings))
  {
    vtkWarningMacro("Could not parse /settings/Data Variables from " << datPath);
  }

  const bool needMetaData = this->LoadedTimeSteps.empty() || this->PreReadCellData.empty();
  if (needMetaData)
  {
    this->ClearTransientReadBuffers();
    if (this->GetMetaData() == 0)
    {
      this->CloseDataFile();
      vtkErrorMacro("Unable to read dat metadata from " << datPath);
      return -1;
    }
    // Register names without enabling — callers whitelist via SetCellArrayStatus,
    // or exclusion-list mode re-enables inside ApplyExcludedFieldArrayNames().
    for (const auto& variableName : this->PreReadCellData)
    {
      this->CellDataArraySelection->AddArray(variableName.c_str(), false);
    }
    for (const auto& variableName : this->PreReadFaceData)
    {
      this->FaceDataArraySelection->AddArray(variableName.c_str(), false);
    }
  }
  else
  {
    this->ClearTransientReadBuffers();
  }

  this->ApplyExcludedFieldArrayNames();
  try
  {
    if (this->GetData() == 0)
    {
      this->CloseDataFile();
      vtkErrorMacro("Unable to read dat results from " << datPath);
      return -1;
    }
  }
  catch (const std::runtime_error& e)
  {
    this->CloseDataFile();
    vtkErrorMacro(<< e.what());
    return -1;
  }

  const int index = this->FinalizeLoadedTimeStepFromBuffers(datPath, settings);
  this->CloseDataFile();
  return index;
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::GetNumberOfLoadedTimeSteps() const
{
  return static_cast<int>(this->LoadedTimeSteps.size());
}

//------------------------------------------------------------------------------
double vtkFLUENTCFFReader::GetFlowTime(int timeStepIndex) const
{
  const LoadedTimeStep* step = this->GetLoadedTimeStep(timeStepIndex);
  return step ? step->flowTime : std::numeric_limits<double>::quiet_NaN();
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::GetFluentTimeStep(int timeStepIndex) const
{
  const LoadedTimeStep* step = this->GetLoadedTimeStep(timeStepIndex);
  return step ? step->fluentTimeStep : -1;
}

//------------------------------------------------------------------------------
const char* vtkFLUENTCFFReader::GetLoadedDataFileName(int timeStepIndex) const
{
  const LoadedTimeStep* step = this->GetLoadedTimeStep(timeStepIndex);
  return step ? step->dataFileName.c_str() : nullptr;
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::FindLoadedTimeStepIndexByFlowTime(
  double flowTime, double tolerance) const
{
  for (std::size_t i = 0; i < this->LoadedTimeSteps.size(); ++i)
  {
    if (std::isfinite(this->LoadedTimeSteps[i].flowTime) &&
      std::abs(this->LoadedTimeSteps[i].flowTime - flowTime) <= tolerance)
    {
      return static_cast<int>(i);
    }
  }
  return -1;
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::FindLoadedTimeStepIndexByFluentStep(int fluentTimeStep) const
{
  for (std::size_t i = 0; i < this->LoadedTimeSteps.size(); ++i)
  {
    if (this->LoadedTimeSteps[i].fluentTimeStep == fluentTimeStep)
    {
      return static_cast<int>(i);
    }
  }
  return -1;
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::GetTimeStepStorageMode(int timeStepIndex) const
{
  const LoadedTimeStep* step = this->GetLoadedTimeStep(timeStepIndex);
  if (!step)
  {
    return -1;
  }
#if FLUENT_CFF_ENABLE_DELTA_CACHE
  return step->storageMode == TIME_STEP_DELTA_FLOAT32 ? TIME_STEP_DELTA_FLOAT32
                                                      : TIME_STEP_FULL_DOUBLE;
#else
  return TIME_STEP_FULL_DOUBLE;
#endif
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::SetActiveTimeStepIndex(int index)
{
  if (index < -1 || index >= this->GetNumberOfLoadedTimeSteps())
  {
    vtkErrorMacro("Invalid active time step index: " << index);
    return;
  }
  this->ActiveTimeStepIndex = index;
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::GetActiveTimeStepIndex() const
{
  return this->ActiveTimeStepIndex;
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::SetReferenceTimeStepIndex(int index)
{
#if FLUENT_CFF_ENABLE_DELTA_CACHE
  if (index < 0 || index >= this->GetNumberOfLoadedTimeSteps())
  {
    vtkErrorMacro("Invalid reference time step index: " << index);
    return;
  }
  if (this->LoadedTimeSteps[static_cast<std::size_t>(index)].storageMode !=
    TIME_STEP_FULL_DOUBLE)
  {
    vtkErrorMacro("Reference time step must use full double storage.");
    return;
  }
  this->ReferenceTimeStepIndex = index;
#else
  (void)index;
  vtkWarningMacro(
    "FLUENT_CFF_ENABLE_DELTA_CACHE is disabled; SetReferenceTimeStepIndex() has no effect.");
#endif
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::GetReferenceTimeStepIndex() const
{
  return this->ReferenceTimeStepIndex;
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::ReleaseAllDataTimeSteps()
{
  this->LoadedTimeSteps.clear();
  this->LoadedTimeSteps.shrink_to_fit();
  this->ReconstructedCellChunkScratch.clear();
  this->ReconstructedFaceChunkScratch.clear();
  this->ClearTransientReadBuffers();
  this->ActiveTimeStepIndex = -1;
  this->ReferenceTimeStepIndex = -1;
  this->FileState = DataState::NOT_LOADED;
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::ReleaseDataTimeStep(int timeStepIndex)
{
  if (timeStepIndex < 0 ||
    static_cast<std::size_t>(timeStepIndex) >= this->LoadedTimeSteps.size())
  {
    vtkErrorMacro("Invalid time step index for release: " << timeStepIndex);
    return;
  }
  if (timeStepIndex == this->ReferenceTimeStepIndex && this->HasDeltaStepsDependingOnReference())
  {
    vtkErrorMacro(
      "Cannot release reference time step while delta-encoded steps are loaded. "
      "Release dependent steps or call ReleaseAllDataTimeSteps().");
    return;
  }

  this->LoadedTimeSteps.erase(this->LoadedTimeSteps.begin() + timeStepIndex);
  if (this->ReferenceTimeStepIndex == timeStepIndex)
  {
    this->ReferenceTimeStepIndex = -1;
  }
  else if (this->ReferenceTimeStepIndex > timeStepIndex)
  {
    --this->ReferenceTimeStepIndex;
  }
  if (this->ActiveTimeStepIndex == timeStepIndex)
  {
    this->ActiveTimeStepIndex = this->LoadedTimeSteps.empty()
      ? -1
      : static_cast<int>(this->LoadedTimeSteps.size()) - 1;
  }
  else if (this->ActiveTimeStepIndex > timeStepIndex)
  {
    --this->ActiveTimeStepIndex;
  }
}

//------------------------------------------------------------------------------
double vtkFLUENTCFFReader::LookupCellValueFromStep(
  const LoadedTimeStep& step, const char* name, vtkIdType cellId, int component) const
{
  if (!name || cellId < 0 || component < 0)
  {
    return std::numeric_limits<double>::quiet_NaN();
  }
  if (step.storageMode == TIME_STEP_FULL_DOUBLE)
  {
    const DataChunk* chunk = this->FindDataChunk(step.cellDataChunks, name);
    if (!chunk || static_cast<std::size_t>(component) >= chunk->dim)
    {
      return std::numeric_limits<double>::quiet_NaN();
    }
    const std::size_t offset =
      static_cast<std::size_t>(cellId) * chunk->dim + static_cast<std::size_t>(component);
    return offset < chunk->dataVector.size() ? chunk->dataVector[offset]
                                               : std::numeric_limits<double>::quiet_NaN();
  }

#if FLUENT_CFF_ENABLE_DELTA_CACHE
  const LoadedTimeStep* refStep = this->GetLoadedTimeStep(this->ReferenceTimeStepIndex);
  if (!refStep)
  {
    return std::numeric_limits<double>::quiet_NaN();
  }
  const DataChunk* refChunk = this->FindDataChunk(refStep->cellDataChunks, name);
  const DeltaDataChunk* deltaChunk = this->FindDeltaDataChunk(step.cellDeltaChunks, name);
  if (!refChunk || !deltaChunk || static_cast<std::size_t>(component) >= refChunk->dim ||
    refChunk->dim != deltaChunk->dim)
  {
    return std::numeric_limits<double>::quiet_NaN();
  }
  const std::size_t offset =
    static_cast<std::size_t>(cellId) * refChunk->dim + static_cast<std::size_t>(component);
  if (offset >= refChunk->dataVector.size() || offset >= deltaChunk->deltaVector.size())
  {
    return std::numeric_limits<double>::quiet_NaN();
  }
  return refChunk->dataVector[offset] + static_cast<double>(deltaChunk->deltaVector[offset]);
#else
  return std::numeric_limits<double>::quiet_NaN();
#endif
}

//------------------------------------------------------------------------------
double vtkFLUENTCFFReader::LookupFaceValueFromStep(
  const LoadedTimeStep& step, const char* name, vtkIdType faceId, int component) const
{
  if (!name || faceId < 0 || component < 0)
  {
    return std::numeric_limits<double>::quiet_NaN();
  }
  if (step.storageMode == TIME_STEP_FULL_DOUBLE)
  {
    const DataChunk* chunk = this->FindDataChunk(step.faceDataChunks, name);
    if (!chunk || static_cast<std::size_t>(component) >= chunk->dim)
    {
      return std::numeric_limits<double>::quiet_NaN();
    }
    const std::size_t offset =
      static_cast<std::size_t>(faceId) * chunk->dim + static_cast<std::size_t>(component);
    return offset < chunk->dataVector.size() ? chunk->dataVector[offset]
                                               : std::numeric_limits<double>::quiet_NaN();
  }

#if FLUENT_CFF_ENABLE_DELTA_CACHE
  const LoadedTimeStep* refStep = this->GetLoadedTimeStep(this->ReferenceTimeStepIndex);
  if (!refStep)
  {
    return std::numeric_limits<double>::quiet_NaN();
  }
  const DataChunk* refChunk = this->FindDataChunk(refStep->faceDataChunks, name);
  const DeltaDataChunk* deltaChunk = this->FindDeltaDataChunk(step.faceDeltaChunks, name);
  if (!refChunk || !deltaChunk || static_cast<std::size_t>(component) >= refChunk->dim ||
    refChunk->dim != deltaChunk->dim)
  {
    return std::numeric_limits<double>::quiet_NaN();
  }
  const std::size_t offset =
    static_cast<std::size_t>(faceId) * refChunk->dim + static_cast<std::size_t>(component);
  if (offset >= refChunk->dataVector.size() || offset >= deltaChunk->deltaVector.size())
  {
    return std::numeric_limits<double>::quiet_NaN();
  }
  return refChunk->dataVector[offset] + static_cast<double>(deltaChunk->deltaVector[offset]);
#else
  return std::numeric_limits<double>::quiet_NaN();
#endif
}

//------------------------------------------------------------------------------
double vtkFLUENTCFFReader::GetCellArrayValue(
  const char* name, vtkIdType cellId, int component, int timeStepIndex) const
{
  const LoadedTimeStep* step = this->GetLoadedTimeStep(timeStepIndex);
  if (!step)
  {
    return std::numeric_limits<double>::quiet_NaN();
  }
  return this->LookupCellValueFromStep(*step, name, cellId, component);
}

//------------------------------------------------------------------------------
double vtkFLUENTCFFReader::GetFaceArrayValue(
  const char* name, vtkIdType faceId, int component, int timeStepIndex) const
{
  const LoadedTimeStep* step = this->GetLoadedTimeStep(timeStepIndex);
  if (!step)
  {
    return std::numeric_limits<double>::quiet_NaN();
  }
  return this->LookupFaceValueFromStep(*step, name, faceId, component);
}

//------------------------------------------------------------------------------
bool vtkFLUENTCFFReader::MaterializeCellChunkForStep(int timeStepIndex, int chunkIndex) const
{
  const LoadedTimeStep* step = this->GetLoadedTimeStep(timeStepIndex);
  if (!step || chunkIndex < 0)
  {
    return false;
  }

  if (step->storageMode == TIME_STEP_FULL_DOUBLE)
  {
    if (static_cast<std::size_t>(chunkIndex) >= step->cellDataChunks.size())
    {
      return false;
    }
    if (static_cast<std::size_t>(chunkIndex) >= this->ReconstructedCellChunkScratch.size())
    {
      this->ReconstructedCellChunkScratch.resize(static_cast<std::size_t>(chunkIndex) + 1);
    }
    this->ReconstructedCellChunkScratch[static_cast<std::size_t>(chunkIndex)] =
      step->cellDataChunks[static_cast<std::size_t>(chunkIndex)];
    return true;
  }

#if FLUENT_CFF_ENABLE_DELTA_CACHE
  if (static_cast<std::size_t>(chunkIndex) >= step->cellDeltaChunks.size())
  {
    return false;
  }
  const LoadedTimeStep* refStep = this->GetLoadedTimeStep(this->ReferenceTimeStepIndex);
  if (!refStep)
  {
    return false;
  }
  const DeltaDataChunk& delta = step->cellDeltaChunks[static_cast<std::size_t>(chunkIndex)];
  const DataChunk* refChunk = this->FindDataChunk(refStep->cellDataChunks, delta.variableName.c_str());
  if (!refChunk || refChunk->dim != delta.dim ||
    refChunk->dataVector.size() != delta.deltaVector.size())
  {
    return false;
  }

  if (static_cast<std::size_t>(chunkIndex) >= this->ReconstructedCellChunkScratch.size())
  {
    this->ReconstructedCellChunkScratch.resize(static_cast<std::size_t>(chunkIndex) + 1);
  }
  DataChunk& out = this->ReconstructedCellChunkScratch[static_cast<std::size_t>(chunkIndex)];
  out.variableName = delta.variableName;
  out.dim = delta.dim;
  out.FaceSectionFluentIdRanges1Based = delta.FaceSectionFluentIdRanges1Based;
  out.dataVector.resize(delta.deltaVector.size());
  for (std::size_t i = 0; i < delta.deltaVector.size(); ++i)
  {
    out.dataVector[i] = refChunk->dataVector[i] + static_cast<double>(delta.deltaVector[i]);
  }
  return true;
#else
  return false;
#endif
}

//------------------------------------------------------------------------------
bool vtkFLUENTCFFReader::MaterializeFaceChunkForStep(int timeStepIndex, int chunkIndex) const
{
  const LoadedTimeStep* step = this->GetLoadedTimeStep(timeStepIndex);
  if (!step || chunkIndex < 0)
  {
    return false;
  }

  if (step->storageMode == TIME_STEP_FULL_DOUBLE)
  {
    if (static_cast<std::size_t>(chunkIndex) >= step->faceDataChunks.size())
    {
      return false;
    }
    if (static_cast<std::size_t>(chunkIndex) >= this->ReconstructedFaceChunkScratch.size())
    {
      this->ReconstructedFaceChunkScratch.resize(static_cast<std::size_t>(chunkIndex) + 1);
    }
    this->ReconstructedFaceChunkScratch[static_cast<std::size_t>(chunkIndex)] =
      step->faceDataChunks[static_cast<std::size_t>(chunkIndex)];
    return true;
  }

#if FLUENT_CFF_ENABLE_DELTA_CACHE
  if (static_cast<std::size_t>(chunkIndex) >= step->faceDeltaChunks.size())
  {
    return false;
  }
  const LoadedTimeStep* refStep = this->GetLoadedTimeStep(this->ReferenceTimeStepIndex);
  if (!refStep)
  {
    return false;
  }
  const DeltaDataChunk& delta = step->faceDeltaChunks[static_cast<std::size_t>(chunkIndex)];
  const DataChunk* refChunk = this->FindDataChunk(refStep->faceDataChunks, delta.variableName.c_str());
  if (!refChunk || refChunk->dim != delta.dim ||
    refChunk->dataVector.size() != delta.deltaVector.size())
  {
    return false;
  }

  if (static_cast<std::size_t>(chunkIndex) >= this->ReconstructedFaceChunkScratch.size())
  {
    this->ReconstructedFaceChunkScratch.resize(static_cast<std::size_t>(chunkIndex) + 1);
  }
  DataChunk& out = this->ReconstructedFaceChunkScratch[static_cast<std::size_t>(chunkIndex)];
  out.variableName = delta.variableName;
  out.dim = delta.dim;
  out.FaceSectionFluentIdRanges1Based = delta.FaceSectionFluentIdRanges1Based;
  out.dataVector.resize(delta.deltaVector.size());
  for (std::size_t i = 0; i < delta.deltaVector.size(); ++i)
  {
    out.dataVector[i] = refChunk->dataVector[i] + static_cast<double>(delta.deltaVector[i]);
  }
  return true;
#else
  return false;
#endif
}

//------------------------------------------------------------------------------
bool vtkFLUENTCFFReader::SyncCellDataChunksForActiveTimeStep()
{
  if (this->ActiveTimeStepIndex < 0)
  {
    return false;
  }
  const LoadedTimeStep* step = this->GetLoadedTimeStep(this->ActiveTimeStepIndex);
  if (!step)
  {
    return false;
  }
  if (step->storageMode == TIME_STEP_FULL_DOUBLE)
  {
    this->CellDataChunks = step->cellDataChunks;
    return true;
  }
#if FLUENT_CFF_ENABLE_DELTA_CACHE
  this->ReconstructedCellChunkScratch.clear();
  this->ReconstructedCellChunkScratch.resize(step->cellDeltaChunks.size());
  for (int i = 0; i < static_cast<int>(step->cellDeltaChunks.size()); ++i)
  {
    if (!this->MaterializeCellChunkForStep(this->ActiveTimeStepIndex, i))
    {
      return false;
    }
  }
  this->CellDataChunks = this->ReconstructedCellChunkScratch;
#endif
  return true;
}

//------------------------------------------------------------------------------
bool vtkFLUENTCFFReader::SyncFaceDataChunksForActiveTimeStep()
{
  if (this->ActiveTimeStepIndex < 0)
  {
    return false;
  }
  const LoadedTimeStep* step = this->GetLoadedTimeStep(this->ActiveTimeStepIndex);
  if (!step)
  {
    return false;
  }
  if (step->storageMode == TIME_STEP_FULL_DOUBLE)
  {
    this->FaceDataChunks = step->faceDataChunks;
    return true;
  }
#if FLUENT_CFF_ENABLE_DELTA_CACHE
  this->ReconstructedFaceChunkScratch.clear();
  this->ReconstructedFaceChunkScratch.resize(step->faceDeltaChunks.size());
  for (int i = 0; i < static_cast<int>(step->faceDeltaChunks.size()); ++i)
  {
    if (!this->MaterializeFaceChunkForStep(this->ActiveTimeStepIndex, i))
    {
      return false;
    }
  }
  this->FaceDataChunks = this->ReconstructedFaceChunkScratch;
#endif
  return true;
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::GetLoadedCellChunkCount(int timeStepIndex) const
{
  const LoadedTimeStep* step = this->GetLoadedTimeStep(timeStepIndex);
  if (!step)
  {
    return 0;
  }
#if FLUENT_CFF_ENABLE_DELTA_CACHE
  return step->storageMode == TIME_STEP_FULL_DOUBLE
    ? static_cast<int>(step->cellDataChunks.size())
    : static_cast<int>(step->cellDeltaChunks.size());
#else
  return static_cast<int>(step->cellDataChunks.size());
#endif
}

//------------------------------------------------------------------------------
const char* vtkFLUENTCFFReader::GetLoadedCellChunkName(int timeStepIndex, int index) const
{
  const LoadedTimeStep* step = this->GetLoadedTimeStep(timeStepIndex);
  if (!step || index < 0)
  {
    return nullptr;
  }
  if (step->storageMode == TIME_STEP_FULL_DOUBLE)
  {
    if (static_cast<std::size_t>(index) >= step->cellDataChunks.size())
    {
      return nullptr;
    }
    return step->cellDataChunks[static_cast<std::size_t>(index)].variableName.c_str();
  }
#if FLUENT_CFF_ENABLE_DELTA_CACHE
  if (static_cast<std::size_t>(index) >= step->cellDeltaChunks.size())
  {
    return nullptr;
  }
  return step->cellDeltaChunks[static_cast<std::size_t>(index)].variableName.c_str();
#else
  return nullptr;
#endif
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::GetLoadedCellChunkDim(int timeStepIndex, int index) const
{
  const LoadedTimeStep* step = this->GetLoadedTimeStep(timeStepIndex);
  if (!step || index < 0)
  {
    return 0;
  }
  if (step->storageMode == TIME_STEP_FULL_DOUBLE)
  {
    if (static_cast<std::size_t>(index) >= step->cellDataChunks.size())
    {
      return 0;
    }
    return static_cast<int>(step->cellDataChunks[static_cast<std::size_t>(index)].dim);
  }
#if FLUENT_CFF_ENABLE_DELTA_CACHE
  if (static_cast<std::size_t>(index) >= step->cellDeltaChunks.size())
  {
    return 0;
  }
  return static_cast<int>(step->cellDeltaChunks[static_cast<std::size_t>(index)].dim);
#else
  return 0;
#endif
}

//------------------------------------------------------------------------------
const double* vtkFLUENTCFFReader::GetLoadedCellChunkData(int timeStepIndex, int index) const
{
  if (!this->MaterializeCellChunkForStep(timeStepIndex, index))
  {
    return nullptr;
  }
  const auto& chunk = this->ReconstructedCellChunkScratch[static_cast<std::size_t>(index)];
  return chunk.dataVector.empty() ? nullptr : chunk.dataVector.data();
}

//------------------------------------------------------------------------------
vtkIdType vtkFLUENTCFFReader::GetLoadedCellChunkTupleCount(int timeStepIndex, int index) const
{
  const int dim = this->GetLoadedCellChunkDim(timeStepIndex, index);
  if (dim <= 0)
  {
    return 0;
  }
  const double* data = this->GetLoadedCellChunkData(timeStepIndex, index);
  if (!data)
  {
    return 0;
  }
  const LoadedTimeStep* step = this->GetLoadedTimeStep(timeStepIndex);
  if (!step)
  {
    return 0;
  }
  const std::size_t count = step->storageMode == TIME_STEP_FULL_DOUBLE
    ? step->cellDataChunks[static_cast<std::size_t>(index)].dataVector.size()
#if FLUENT_CFF_ENABLE_DELTA_CACHE
    : this->ReconstructedCellChunkScratch[static_cast<std::size_t>(index)].dataVector.size();
#else
    : 0;
#endif
  return static_cast<vtkIdType>(count / static_cast<std::size_t>(dim));
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::GetLoadedFaceChunkCount(int timeStepIndex) const
{
  const LoadedTimeStep* step = this->GetLoadedTimeStep(timeStepIndex);
  if (!step)
  {
    return 0;
  }
#if FLUENT_CFF_ENABLE_DELTA_CACHE
  return step->storageMode == TIME_STEP_FULL_DOUBLE
    ? static_cast<int>(step->faceDataChunks.size())
    : static_cast<int>(step->faceDeltaChunks.size());
#else
  return static_cast<int>(step->faceDataChunks.size());
#endif
}

//------------------------------------------------------------------------------
const char* vtkFLUENTCFFReader::GetLoadedFaceChunkName(int timeStepIndex, int index) const
{
  const LoadedTimeStep* step = this->GetLoadedTimeStep(timeStepIndex);
  if (!step || index < 0)
  {
    return nullptr;
  }
  if (step->storageMode == TIME_STEP_FULL_DOUBLE)
  {
    if (static_cast<std::size_t>(index) >= step->faceDataChunks.size())
    {
      return nullptr;
    }
    return step->faceDataChunks[static_cast<std::size_t>(index)].variableName.c_str();
  }
#if FLUENT_CFF_ENABLE_DELTA_CACHE
  if (static_cast<std::size_t>(index) >= step->faceDeltaChunks.size())
  {
    return nullptr;
  }
  return step->faceDeltaChunks[static_cast<std::size_t>(index)].variableName.c_str();
#else
  return nullptr;
#endif
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::GetLoadedFaceChunkDim(int timeStepIndex, int index) const
{
  const LoadedTimeStep* step = this->GetLoadedTimeStep(timeStepIndex);
  if (!step || index < 0)
  {
    return 0;
  }
  if (step->storageMode == TIME_STEP_FULL_DOUBLE)
  {
    if (static_cast<std::size_t>(index) >= step->faceDataChunks.size())
    {
      return 0;
    }
    return static_cast<int>(step->faceDataChunks[static_cast<std::size_t>(index)].dim);
  }
#if FLUENT_CFF_ENABLE_DELTA_CACHE
  if (static_cast<std::size_t>(index) >= step->faceDeltaChunks.size())
  {
    return 0;
  }
  return static_cast<int>(step->faceDeltaChunks[static_cast<std::size_t>(index)].dim);
#else
  return 0;
#endif
}

//------------------------------------------------------------------------------
const double* vtkFLUENTCFFReader::GetLoadedFaceChunkData(int timeStepIndex, int index) const
{
  if (!this->MaterializeFaceChunkForStep(timeStepIndex, index))
  {
    return nullptr;
  }
  const auto& chunk = this->ReconstructedFaceChunkScratch[static_cast<std::size_t>(index)];
  return chunk.dataVector.empty() ? nullptr : chunk.dataVector.data();
}

//------------------------------------------------------------------------------
vtkIdType vtkFLUENTCFFReader::GetLoadedFaceChunkTupleCount(int timeStepIndex, int index) const
{
  const int dim = this->GetLoadedFaceChunkDim(timeStepIndex, index);
  if (dim <= 0)
  {
    return 0;
  }
  const double* data = this->GetLoadedFaceChunkData(timeStepIndex, index);
  if (!data)
  {
    return 0;
  }
  const LoadedTimeStep* step = this->GetLoadedTimeStep(timeStepIndex);
  if (!step)
  {
    return 0;
  }
  const std::size_t count = step->storageMode == TIME_STEP_FULL_DOUBLE
    ? step->faceDataChunks[static_cast<std::size_t>(index)].dataVector.size()
#if FLUENT_CFF_ENABLE_DELTA_CACHE
    : this->ReconstructedFaceChunkScratch[static_cast<std::size_t>(index)].dataVector.size();
#else
    : 0;
#endif
  return static_cast<vtkIdType>(count / static_cast<std::size_t>(dim));
}

VTK_ABI_NAMESPACE_END
