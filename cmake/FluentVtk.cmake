# FluentVtk.cmake — shared paths for vtk-fluentcff consumers (fluent-ml, vtk-addon, etc.)

#

# Override at configure time:

#   cmake ... -DFLUENT_VTK_ROOT=E:/klj/vtk-fluentcff

#   cmake ... -DFLUENT_CFF_ENABLE_GRAPH_DATA=ON -DFLUENT_CFF_ENABLE_DELTA_CACHE=ON



if(NOT FLUENT_VTK_ROOT)

  get_filename_component(FLUENT_VTK_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

endif()



if(NOT EXISTS "${FLUENT_VTK_ROOT}/vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.h")

  message(FATAL_ERROR

    "FLUENT_VTK_ROOT does not look like vtk-fluentcff: ${FLUENT_VTK_ROOT}\n"

    "  Expected: ${FLUENT_VTK_ROOT}/vtk/IO/FLUENTCFF/vtkFLUENTCFFReader.h")

endif()



set(FLUENT_VTK_IO "${FLUENT_VTK_ROOT}/vtk/IO/FLUENTCFF")

set(FLUENT_VTK_CMAKE "${FLUENT_VTK_ROOT}/cmake")



option(FLUENT_CFF_ENABLE_GRAPH_DATA

  "Compile GNN graph data (centroids, areas, edge_index, boundary adjacency)" OFF)

option(FLUENT_CFF_ENABLE_DELTA_CACHE

  "Compile float delta storage relative to a reference time step" OFF)



function(fluent_vtk_add_local_reader target_name)

  if(NOT TARGET ${target_name})

    add_library(${target_name} STATIC

      "${FLUENT_VTK_IO}/vtkFLUENTCFFInternal.cxx"

      "${FLUENT_VTK_IO}/vtkFLUENTCFFReader.cxx"

      "${FLUENT_VTK_IO}/vtkFLUENTCFFReaderTransient.cxx"

      "${FLUENT_VTK_CMAKE}/vtkAOSDataArrayTemplateInstantiate.cxx"

    )

    target_include_directories(${target_name} PUBLIC "${FLUENT_VTK_IO}")

    target_compile_definitions(${target_name} PUBLIC

      FLUENT_CFF_ENABLE_GRAPH_DATA=$<BOOL:${FLUENT_CFF_ENABLE_GRAPH_DATA}>

      FLUENT_CFF_ENABLE_DELTA_CACHE=$<BOOL:${FLUENT_CFF_ENABLE_DELTA_CACHE}>

    )

    message(STATUS "LocalFLUENTCFFReader: GRAPH_DATA=${FLUENT_CFF_ENABLE_GRAPH_DATA} DELTA_CACHE=${FLUENT_CFF_ENABLE_DELTA_CACHE}")

  endif()

endfunction()

