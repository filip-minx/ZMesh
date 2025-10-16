add_executable(zmesh_example_calculator calculator.cpp)

set_target_properties(zmesh_example_calculator PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

target_link_libraries(zmesh_example_calculator
    PRIVATE
        zmesh
)
