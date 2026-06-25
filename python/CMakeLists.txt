pybind11_add_module(obm obm_py.cpp)
target_link_libraries(obm PRIVATE obm_core)
target_include_directories(obm PRIVATE ${CMAKE_SOURCE_DIR}/include)

# Place the .pyd next to the source for easy `import obm`
set_target_properties(obm PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/python
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/python
)
