# Micro-benchmarks. Off by default (-DAEGIS_BUILD_BENCHMARKS=ON).
# No numbers are published until the measured components are implemented.
add_executable(bench_datastructures bench_datastructures.cpp)
target_link_libraries(bench_datastructures PRIVATE aegis_options aegis_datastructures aegis_common)
