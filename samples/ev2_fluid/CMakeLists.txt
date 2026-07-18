cmake_minimum_required(VERSION 3.27)
set(CMAKE_CXX_STANDARD 23)

include(clang-warnings)

project(ev2_fluid)

add_executable(ev2_fluid 
	main.cpp 
	poisson_solver.cpp
)

target_link_libraries(ev2_fluid PRIVATE
	samples-shared
	engine
)

add_executable(ev2_poisson_test 
	poisson_test.cpp 
	poisson_solver.cpp
)

target_link_libraries(ev2_poisson_test PRIVATE
	samples-shared
	engine
)

