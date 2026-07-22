// SPDX-FileCopyrightText: Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
// SPDX-License-Identifier: BSD-3-Clause
/**
 * Compile-time feature flags for vtkFLUENTCFFReader.
 * Override via CMake (fluent_vtk_add_local_reader) or -D on the command line.
 */
#ifndef vtkFLUENTCFFConfig_h
#define vtkFLUENTCFFConfig_h

#ifndef FLUENT_CFF_ENABLE_GRAPH_DATA
#define FLUENT_CFF_ENABLE_GRAPH_DATA 0
#endif

#ifndef FLUENT_CFF_ENABLE_DELTA_CACHE
#define FLUENT_CFF_ENABLE_DELTA_CACHE 0
#endif

#endif
