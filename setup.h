#include <math.h>

#define SERIAL_COORD(i, j, N) ((i) * N + (j))
#define ROW(p, N) ((p) / N)
#define COL(p, N) ((p) % N)
#define BOX(p, n) ((p) / (n * n * n) * n + ((p) % (n * n)) / n)

void print_board(int *board, int N) {
    int i, j;
    for (i = 0; i < N; ++i) {
        for (j = 0; j < N; ++j) {
            printf("%d ", board[SERIAL_COORD(i, j, N)]);
        }
        printf("\n");
    }
    printf("\n");
}

struct Info {
    cl_platform_id platform;
    cl_device_id device;
    cl_context context;
    cl_command_queue queue;
    cl_program program;
    cl_kernel kernel;
    size_t preferred_multiple_init;
};

void freeInfo(struct Info info) {
    clReleaseKernel(info.kernel);
    clReleaseProgram(info.program);
    clReleaseCommandQueue(info.queue);
    clReleaseContext(info.context);
}

struct Info initialize(const char *kernel_name) {
    cl_int err;
    //region Initialize OpenCL
    cl_platform_id p = select_platform();
    cl_device_id d = select_device(p);
    cl_context ctx = create_context(p, d);
    cl_command_queue que = create_queue(ctx, d);
    //endregion

    //region Initialize Kernel
    cl_program prog = create_program("kernels.cl", ctx, d);

    cl_kernel calculate_cost_k = clCreateKernel(prog, kernel_name, &err);
    ocl_check(err, "create kernel");

    size_t preferred_multiple_init;
    clGetKernelWorkGroupInfo(calculate_cost_k, d, CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE,
                             sizeof(preferred_multiple_init), &preferred_multiple_init, NULL);
    //endregion

    return (struct Info) {p, d, ctx, que, prog, calculate_cost_k, preferred_multiple_init};
}

void AddKernelArg(cl_kernel kernel, cl_uint arg_index, size_t arg_size, void *arg_value) {
    cl_int err = clSetKernelArg(kernel, arg_index, arg_size, arg_value);
    ocl_check(err, "setting kernel arg %u", arg_index);
}

struct Task {
    double runtime;
};

void printResult(FILE *stream, struct Task task) {
    fprintf(stream, "%f\n", task.runtime);

}

