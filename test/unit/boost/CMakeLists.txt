cmake_minimum_required(VERSION 3.10)
project(hermes)

set(CMAKE_CXX_STANDARD 17)

#------------------------------------------------------------------------------
# Build Tests
#------------------------------------------------------------------------------

add_executable(test_boost_exec
        ${TEST_MAIN}/main.cc
        test_boost.cc
)
target_link_libraries(test_boost_exec
        ${_Chimaera_RUNTIME_LIBRARIES} Catch2::Catch2 MPI::MPI_CXX)

#------------------------------------------------------------------------------
# Test Cases
#------------------------------------------------------------------------------

#add_test(NAME test_boost COMMAND
#        ${CMAKE_BINARY_DIR}/bin/test_messages "TestBoost")

#------------------------------------------------------------------------------
# Install Targets
#------------------------------------------------------------------------------
install(TARGETS
        test_boost_exec
        EXPORT
        ${HERMES_EXPORTED_TARGETS}
        LIBRARY DESTINATION ${HERMES_INSTALL_LIB_DIR}
        ARCHIVE DESTINATION ${HERMES_INSTALL_LIB_DIR}
        RUNTIME DESTINATION ${HERMES_INSTALL_BIN_DIR})

#-----------------------------------------------------------------------------
# Coverage
#-----------------------------------------------------------------------------
if(HERMES_ENABLE_COVERAGE)
    set_coverage_flags(test_boost_exec)
endif()
