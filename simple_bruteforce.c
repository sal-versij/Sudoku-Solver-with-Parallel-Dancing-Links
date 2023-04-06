#include <stdio.h>
#include <stdlib.h>

#define CL_TARGET_OPENCL_VERSION 120

#include "ocl_boiler.h"
#include "setup.h"

cl_event
execute_backtrack_kernel(cl_command_queue q, cl_kernel k, size_t lws, size_t slots, int n, cl_mem d_boards,
                         cl_mem d_ans_found) {
    cl_int err;
    int i = 0;
    int N = n * n;

    printf("LWS: %zu\n", lws);

    AddKernelArg(k, i++, sizeof(d_boards), &d_boards);
    AddKernelArg(k, i++, sizeof(d_ans_found), &d_ans_found);
    AddKernelArg(k, i++, sizeof(n), &n);

    size_t supportSize = lws * N * N;

    struct MemoryString memory_support = memory_string(supportSize * sizeof(int));
    struct MemoryString memory_total = memory_string(supportSize * sizeof(int));

    printf("Shared boards size: %zu (%zu %s)\n", supportSize, memory_support.value, memory_support.unit);
    printf("Stack size: %zu (%zu %s)\n", supportSize, memory_support.value, memory_support.unit);
    printf("Total local Memory: %zu %s\n", memory_total.value, memory_total.unit);

    AddKernelArg(k, i++, supportSize * sizeof(int), NULL);
    AddKernelArg(k, i++, supportSize * sizeof(int), NULL);

    size_t gws = slots;

    cl_event kernel_evt;
    err = clEnqueueNDRangeKernel(q, k, 1, NULL, &gws, &lws, 0, NULL, &kernel_evt);
    ocl_check(err, "launch kernel");
    return kernel_evt;
}

int solve(const int *board, int n, int slots, int tile_size) {
    int N = n * n;

    int *boards = (int *) calloc(N * N * slots, sizeof(int));
    memcpy(boards, board, N * N * sizeof(int));

    int answer_found = cpu_initial_search(n, boards, slots);

    if (answer_found) {
        print_board(boards, N);
    } else {
        printf("Initial search finished.\n");
        printf("Starting GPU search...\n");
        cl_int err;

        struct Info info = initialize("sbt_kernels.cl", "backtrack_kernel");
        printf("d_boards size: %d\n", N * N * slots);
        cl_mem d_boards = clCreateBuffer(info.context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR | CL_MEM_ALLOC_HOST_PTR,
                                         N * N * slots * sizeof(int), boards, &err);
        ocl_check(err, "create buffer for boards");

        answer_found = -1;
        cl_mem d_ans_found = clCreateBuffer(info.context,
                                            CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR | CL_MEM_ALLOC_HOST_PTR,
                                            sizeof(int), &answer_found, &err);
        ocl_check(err, "create buffer for answer_found");

        cl_event kernel_evt = execute_backtrack_kernel(
                info.queue, info.kernel,
                tile_size, slots,
                n, d_boards, d_ans_found);

        cl_event read_ans_evt;
        err = clEnqueueReadBuffer(info.queue, d_ans_found,
                                  CL_TRUE, 0, sizeof(int), &answer_found,
                                  1, &kernel_evt, &read_ans_evt);
        ocl_check(err, "read answer_found");

        printf("GPU search finished.\n");

        if (answer_found >= 0) {
            err = clEnqueueReadBuffer(info.queue, d_boards,
                                      CL_TRUE, answer_found * N * N * sizeof(int), N * N * sizeof(int), boards,
                                      1, &read_ans_evt, NULL);

            answer_found = 1;
            print_board(boards, N);
        }

        freeInfo(info);
        clReleaseMemObject(d_boards);
        clReleaseMemObject(d_ans_found);
    }

    free(boards);
    return answer_found;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <sudoku> <slots> <tile_size>\n", argv[0]);
        return 1;
    }

    int n;
    int *board = read_board(argv[1], &n);

    const int N = n * n;
    const int slots = atoi(argv[2]);
    const int tile_size = atoi(argv[3]);

    printf("Sudoku loaded: %d x %d\n", N, N);
    print_board(board, N);
    printf("Slots: %d\nTile size: %d\n", slots, tile_size);

    int ans = solve(board, n, slots, tile_size);

    printf(ans == 1 ? "Answer found.\n" : "No answer found.\n");

    free(board);
    return 0;
}
