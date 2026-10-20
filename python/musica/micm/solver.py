from enum import IntEnum
class SolverType(IntEnum):
    undefined = 0
    rosenbrock = 1
    rosenbrock_standard_order = 2
    backward_euler = 3
    backward_euler_standard_order = 4
    cuda_rosenbrock = 5
    rosenbrock_dae4 = 6
    rosenbrock_dae4_standard_order = 7
    rosenbrock_dae6 = 8
    rosenbrock_dae6_standard_order = 9
