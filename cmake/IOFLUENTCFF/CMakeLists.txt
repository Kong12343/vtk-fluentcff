set(classes
  vtkFLUENTCFFReader)

set(private_sources
  vtkFLUENTCFFInternal.cxx)

vtk_module_add_module(VTK::IOFLUENTCFF
  CLASSES ${classes}
  SOURCES ${private_sources}
  )
vtk_module_set_properties(VTK::IOFLUENTCFF
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON)
vtk_add_test_mangling(VTK::IOFLUENTCFF)
