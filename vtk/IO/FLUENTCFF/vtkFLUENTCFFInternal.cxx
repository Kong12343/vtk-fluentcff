// SPDX-FileCopyrightText: Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
// SPDX-License-Identifier: BSD-3-Clause

#include "vtkFLUENTCFFInternal.h"

#include <regex>
#include <vector>

namespace
{
bool ReadSettingsDataVariables(hid_t datFile, std::string& outText)
{
  if (H5Gget_objinfo(datFile, "/settings/Data Variables", false, nullptr) != 0)
  {
    return false;
  }
  hid_t dset = H5Dopen(datFile, "/settings/Data Variables", H5P_DEFAULT);
  if (dset < 0)
  {
    return false;
  }
  hid_t space = H5Dget_space(dset);
  hid_t type = H5Dget_type(dset);
  const std::size_t stringLength = H5Tget_size(type);
  std::vector<char> buffer(stringLength + 1, '\0');
  const herr_t status = H5Dread(dset, type, H5S_ALL, H5S_ALL, H5P_DEFAULT, buffer.data());
  H5Tclose(type);
  H5Sclose(space);
  H5Dclose(dset);
  if (status < 0)
  {
    return false;
  }
  outText.assign(buffer.data());
  return true;
}
} // namespace

//------------------------------------------------------------------------------
bool vtkFLUENTCFFInternal::ParseDatSettingsFromH5(hid_t datFile, FluentDatSettings& out)
{
  out = FluentDatSettings{};
  std::string text;
  if (!ReadSettingsDataVariables(datFile, text))
  {
    return false;
  }
  static const std::regex flowTimeRe(R"(\(flow-time\s+([0-9.eE+-]+)\))");
  static const std::regex timeStepRe(R"(\(time-step\s+(\d+)\))");
  static const std::regex caseFileRe(R"(\(Case File\s+([^)]+)\))");
  std::smatch match;
  if (std::regex_search(text, match, flowTimeRe) && match.size() > 1)
  {
    out.flowTime = std::stod(match[1].str());
    out.flowTimeValid = true;
  }
  if (std::regex_search(text, match, timeStepRe) && match.size() > 1)
  {
    out.timeStep = std::stoi(match[1].str());
  }
  if (std::regex_search(text, match, caseFileRe) && match.size() > 1)
  {
    out.caseFileRef = match[1].str();
  }
  return out.flowTimeValid || out.timeStep >= 0 || !out.caseFileRef.empty();
}

//------------------------------------------------------------------------------
bool vtkFLUENTCFFInternal::RemoveTrailingIndex(std::string& fieldName)
{
  if (fieldName.size() > 2 && fieldName[fieldName.size() - 2] == '_' &&
    std::isdigit(fieldName[fieldName.size() - 1]))
  {
    fieldName.erase(fieldName.size() - 2);
    return true;
  }
  return false;
}

//------------------------------------------------------------------------------
bool vtkFLUENTCFFInternal::RemoveSuffixIfPresent(std::string& fieldName, const std::string& suffix)
{
  if (fieldName.size() > suffix.size() &&
    fieldName.substr(fieldName.size() - suffix.size(), suffix.size()) == suffix)
  {
    fieldName.erase(fieldName.size() - suffix.size());
    return true;
  }
  return false;
}

//------------------------------------------------------------------------------
std::string vtkFLUENTCFFInternal::GetMatchingFieldName(const std::string& strSectionName)
{
  const std::string prefix = strSectionName.substr(0, 3);
  if (prefix != "SV_")
  {
    return strSectionName;
  }
  std::string fieldName = strSectionName.substr(3);

  std::string vector_string;
  if (vtkFLUENTCFFInternal::RemoveSuffixIfPresent(fieldName, "_G"))
  {
    vector_string = " gradient vector";
  }
  else if (vtkFLUENTCFFInternal::RemoveSuffixIfPresent(fieldName, "_RG"))
  {
    vector_string = " RG vector";
  }

  vtkFLUENTCFFInternal::RemoveTrailingIndex(fieldName);
  if (!vtkFLUENTCFFInternal::RemoveSuffixIfPresent(fieldName, "_MEAN"))
  {
    vtkFLUENTCFFInternal::RemoveSuffixIfPresent(fieldName, "_RMS");
  }

  std::string previous_step_string;
  if (vtkFLUENTCFFInternal::RemoveSuffixIfPresent(fieldName, "_M1"))
  {
    previous_step_string = " at previous step";
  }
  else if (vtkFLUENTCFFInternal::RemoveSuffixIfPresent(fieldName, "_M2"))
  {
    previous_step_string = " at second_previous_step";
  }

  if (!vtkFLUENTCFFInternal::FieldsNamesMap.count(fieldName))
  {
    return strSectionName;
  }

  return vtkFLUENTCFFInternal::FieldsNamesMap.at(fieldName) + vector_string + previous_step_string +
    " (" + strSectionName + ")";
}
