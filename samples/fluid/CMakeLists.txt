cmake_minimum_required(VERSION 3.27)
set(CMAKE_CXX_STANDARD 23)

include(warnings)

project(ev2_fluid)

add_executable(ev2_fluid 
	main.cpp 
	poisson_solver.cpp
	grid_sim.cpp
)

target_link_libraries(ev2_fluid PRIVATE
	samples-shared
	ev2-engine
)

ev2_add_mount(ev2_fluid NAME fluid DIR resource)

add_executable(ev2_poisson_test 
	poisson_test.cpp 
	poisson_solver.cpp
)

target_link_libraries(ev2_poisson_test PRIVATE
	samples-shared
	ev2-engine
)

ev2_add_mount(ev2_poisson_test NAME fluid DIR resource)

