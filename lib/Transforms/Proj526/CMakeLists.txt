# If we don't need RTTI or EH, there's no reason to export anything
# from the Proj526 plugin.
if( NOT LLVM_REQUIRES_RTTI )
  if( NOT LLVM_REQUIRES_EH )
    set(LLVM_EXPORTED_SYMBOL_FILE ${CMAKE_CURRENT_SOURCE_DIR}/Proj526.exports)
  endif()
endif()

if(WIN32 OR CYGWIN)
  set(LLVM_LINK_COMPONENTS Core Support TransformUtils)
endif()

add_llvm_loadable_module( LLVMProj526
  Proj526.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Transforms
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Transforms/Utils

  DEPENDS
  intrinsics_gen
  PLUGIN_TOOL
  opt
  )

#target_link_libraries(LLVMProj526 LLVMTransformUtils)
