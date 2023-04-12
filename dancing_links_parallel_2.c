#include <stdio.h>
#include <stdlib.h>

#define CL_TARGET_OPENCL_VERSION 120

#include "ocl_boiler.h"
#include "setup.h"

cl_event
execute_exact_cover_kernel(cl_command_queue q, cl_kernel k, size_t task_count, size_t lws, cl_int n, cl_mem d_tasks,
                           cl_mem d_dlx, cl_mem d_dlxs, cl_mem d_dlx_props, cl_int dlx_size, cl_mem d_ans,
                           cl_mem d_ans_found, cl_event *waitingList, int waitingListSize);

int permutate_tasks(const int *dlx, int dlx_size, int *tasks, int i);

struct Task solve(const int *board, int n, int lws);

int main(int argc, char *argv[]) {
    if (argc < 3 || argc > 4) {
        fprintf(stderr, "Usage: %s <sudoku> <tile_size> [csv]\n", argv[0]);
        return 1;
    }

    int n;
    int *board = read_board(argv[1], &n);

    const int N = n * n;
    const int lws = atoi(argv[2]);
    const char *csv = argc == 4 ? argv[3] : "";

    printf("Sudoku loaded: %d x %d\n", N, N);
    print_board(board, N);

    struct Task task = solve(board, n, lws);

    if (*csv != 0) {
        FILE *csv_file = fopen(csv, "a");
        write_task_to_csv(csv_file, task);
        fclose(csv_file);
    }

    free(board);
    return 0;
}

struct Task solve(const int *board, int n, int lws) {
    struct Task task = {0};
    int N = n * n;
    struct MemoryString memory;

    task.size = N;
    task.lws = lws;

    //region Initialize dlx
    printf("Initializing dlx...\n");
    int *col_ids, *row_ids, *convert_table;
    int *dlx;
    int placed;
    int **valid_candidates = (int **) malloc(N * N * sizeof(int *));

    for (int i = 0; i < N * N; ++i) {
        valid_candidates[i] = (int *) calloc(N, sizeof(int));
    }
    initial_check(board, n, valid_candidates, &placed);

    int num_elems = convert_matrix(board, valid_candidates, placed, n, &col_ids, &row_ids, &convert_table);
    int dlx_size = build_dancing_links(col_ids, row_ids, num_elems, &dlx);
    int *dlx_props = dlx + 4 * dlx_size;
    int *row = dlx_props + dlx_size;

    memory = memory_string(dlx_size * 4 * sizeof(int));

    printf("Number of nodes in dancing links: %d (~%llu %s)\n", dlx_size,
           memory.value, memory.unit);
    //endregion

    //region Generate tasks
    int estimated_tasks_count = dlx_size - N * N - 1;

    memory = memory_string(dlx_size * 4 * estimated_tasks_count * sizeof(int));
    printf("Generating %d tasks (taking ~%llu %s of memory)...\n", estimated_tasks_count, memory.value, memory.unit);

    int *tasks = (int *) malloc(estimated_tasks_count * sizeof(int));

    int c_tasks_count = permutate_tasks(dlx, dlx_size, tasks, estimated_tasks_count);
    task.tasks = c_tasks_count;
    if (c_tasks_count > estimated_tasks_count) {
        fprintf(stderr, "Too many tasks generated: %d > %d\n", c_tasks_count, estimated_tasks_count);
        return task;
    }
    if (c_tasks_count < 0) {
        fprintf(stderr, "Unknown error: Alredy solved?.\n");
        return task;
    }

    for (int i = 0; i < c_tasks_count; ++i) {
        int r = row[tasks[i]];

        if (convert_table[r] / N > N * N) {
            fprintf(stderr, "Invalid task %d: %d -> %d (%d: %d)\n",
                    i, r, convert_table[r], convert_table[r] / N,
                    convert_table[r] % N + 1);

            return task;
        }
    }

    memory = memory_string(dlx_size * 4 * c_tasks_count * sizeof(int));
    printf("%d tasks generated (taking ~%llu %s of memory).\n", c_tasks_count, memory.value, memory.unit);
    //endregion

    //region GPU Search

    printf("Starting GPU search...\n");

    //region Initialization
    cl_int err;
    int *answer_data = (int *) malloc(sizeof(int) * 2);
    answer_data[0] = -1;
    answer_data[1] = 0;

    struct Info info = initialize("dlx_kernels_2.cl", "exact_cover_kernel");

    cl_mem d_tasks = clCreateBuffer(info.context, CL_MEM_READ_WRITE | CL_MEM_HOST_WRITE_ONLY | CL_MEM_USE_HOST_PTR,
                                    c_tasks_count * sizeof(int), tasks, &err);
    ocl_check(err, "create buffer for tasks");

    cl_mem d_dlx = clCreateBuffer(info.context, CL_MEM_READ_WRITE | CL_MEM_HOST_WRITE_ONLY | CL_MEM_USE_HOST_PTR,
                                  dlx_size * 4 * sizeof(int), dlx, &err);
    ocl_check(err, "create buffer for dlx");

    cl_mem d_dlx_props = clCreateBuffer(info.context,
                                        CL_MEM_READ_ONLY | CL_MEM_HOST_WRITE_ONLY | CL_MEM_USE_HOST_PTR,
                                        dlx_size * 2 * sizeof(int), dlx_props, &err);
    ocl_check(err, "create buffer for dlx_props");

    cl_mem d_answer_data = clCreateBuffer(info.context,
                                          CL_MEM_READ_WRITE | CL_MEM_USE_HOST_PTR,
                                          sizeof(int) * 2, answer_data, &err);
    ocl_check(err, "create buffer for answer_data");

    cl_mem d_answer = clCreateBuffer(info.context, CL_MEM_WRITE_ONLY | CL_MEM_HOST_READ_ONLY,
                                     N * N * sizeof(int),
                                     NULL, &err);
    ocl_check(err, "create buffer for answer");

    cl_mem d_dlxs = clCreateBuffer(info.context, CL_MEM_READ_WRITE | CL_MEM_HOST_NO_ACCESS,
                                   dlx_size * 4 * c_tasks_count * sizeof(int), NULL, &err);
    ocl_check(err, "create buffer for dlxs");

    task.write_answer_data_byte = sizeof(int) * 2;
    task.write_tasks_byte = c_tasks_count * sizeof(int);
    task.write_dlx_byte = dlx_size * 4 * sizeof(int);
    task.write_dlx_props_byte = dlx_size * 2 * sizeof(int);
    task.write_dlxs_byte = dlx_size * 4 * c_tasks_count * sizeof(int);

    memory = memory_string(c_tasks_count * sizeof(int));
    printf("Device buffer tasks size: %d (%llu %s)\n", c_tasks_count, memory.value, memory.unit);

    memory = memory_string(dlx_size * 4 * sizeof(int));
    printf("Device buffer dlx size: %d (%llu %s)\n", dlx_size * 4, memory.value, memory.unit);

    memory = memory_string(dlx_size * 4 * c_tasks_count * sizeof(int));
    printf("Device buffer dlxs size: %d (%llu %s)\n", dlx_size * 4 * c_tasks_count, memory.value, memory.unit);

    memory = memory_string(dlx_size * 2 * sizeof(int));
    printf("Device buffer dlx_props size: %d (%llu %s)\n", dlx_size * 2, memory.value, memory.unit);

    memory = memory_string(N * N * sizeof(int));
    printf("Device buffer answer size: %d (%llu %s)\n", N * N, memory.value, memory.unit);

    memory = memory_string(sizeof(int) * 2);
    printf("Device buffer answer_data size: %d (%llu %s)\n", 2, memory.value, memory.unit);

    //endregion

    //region Write data to device
    cl_event evt_maps[4];
    cl_event evt_unmaps[4];

    clEnqueueMapBuffer(info.queue, d_answer_data,
                       CL_FALSE, CL_MAP_WRITE, 0, sizeof(int) * 2,
                       0, NULL, &evt_maps[0], &err);
    ocl_check(err, "map answer_data");

    clEnqueueMapBuffer(info.queue, d_tasks,
                       CL_FALSE, CL_MAP_WRITE, 0, c_tasks_count * sizeof(int),
                       0, NULL, &evt_maps[1], &err);
    ocl_check(err, "map tasks");

    clEnqueueMapBuffer(info.queue, d_dlx,
                       CL_FALSE, CL_MAP_WRITE, 0, dlx_size * 4 * sizeof(int),
                       0, NULL, &evt_maps[2], &err);
    ocl_check(err, "map dlx");

    clEnqueueMapBuffer(info.queue, d_dlx_props,
                       CL_FALSE, CL_MAP_WRITE, 0, dlx_size * 2 * sizeof(int),
                       0, NULL, &evt_maps[3], &err);
    ocl_check(err, "map dlx_props");

    // Unmapping data

    err = clEnqueueUnmapMemObject(info.queue, d_answer_data, answer_data,
                                  1, &evt_maps[0], &evt_unmaps[0]);
    ocl_check(err, "unmap answer_data");

    err = clEnqueueUnmapMemObject(info.queue, d_tasks, tasks,
                                  1, &evt_maps[1], &evt_unmaps[1]);
    ocl_check(err, "unmap tasks");

    err = clEnqueueUnmapMemObject(info.queue, d_dlx, dlx,
                                  1, &evt_maps[2], &evt_unmaps[2]);
    ocl_check(err, "unmap dlx");

    err = clEnqueueUnmapMemObject(info.queue, d_dlx_props, dlx_props,
                                  1, &evt_maps[3], &evt_unmaps[3]);
    ocl_check(err, "unmap dlx_props");
    //endregion

    // print dlx
    // printf("Host DLX (%d):\n", dlx_size);
    // for (int i = 0; i < dlx_size; ++i) {
    //     printf("%d: u%d d%d l%d r%d\n", i, dlx[i], dlx[i + dlx_size], dlx[i + dlx_size * 2], dlx[i + dlx_size * 3]);
    // }

    cl_event kernel_evt = execute_exact_cover_kernel(
            info.queue, info.kernel,
            c_tasks_count, lws, n,
            d_tasks, d_dlx, d_dlxs, d_dlx_props,
            dlx_size, d_answer, d_answer_data,
            evt_unmaps, 4);

    //region Read answer

    cl_event read_answer_found_evt;
    clEnqueueMapBuffer(info.queue, d_answer_data,
                       CL_TRUE, CL_MAP_READ, 0, sizeof(int) * 2,
                       1, &kernel_evt, &read_answer_found_evt, &err);
    ocl_check(err, "read answer_data");

    task.read_answer_found_byte = sizeof(int) * 2;

    printf("GPU search finished.\n");

    int answer_found = answer_data[0];
    int answer_length = answer_data[1];

    clEnqueueUnmapMemObject(info.queue, d_answer_data, answer_data,
                            1, &read_answer_found_evt, NULL);

    if (answer_found && answer_length > 0) {
        cl_event read_answer_evt;
        int *answer = clEnqueueMapBuffer(info.queue, d_answer,
                                         CL_TRUE, CL_MAP_READ, 0, N * N * sizeof(int),
                                         1, &kernel_evt, &read_answer_evt, &err);
        ocl_check(err, "read answer");

        task.read_answer_byte = N * N * sizeof(int);
        task.read_answer_nanoseconds = runtime_ns(read_answer_evt);

        for (int i = 0; i < answer_length; ++i)
            answer[i] = row[answer[i]]; // convert to row numbers
        convert_answer_print(row[tasks[answer_found]], answer, convert_table, N
        );

        clEnqueueUnmapMemObject(info.queue, d_answer, answer,
                                1, &read_answer_evt, NULL);
    } else {
        printf("No answer found.\n");
    }
    //endregion

    task.write_answer_data_nanoseconds = runtime_ns(evt_maps[0]);
    task.write_tasks_nanoseconds = runtime_ns(evt_maps[1]);
    task.write_dlx_nanoseconds = runtime_ns(evt_maps[2]);
    task.write_dlx_props_nanoseconds = runtime_ns(evt_maps[3]);
    task.kernel_nanoseconds = runtime_ns(kernel_evt);
    task.read_answer_found_nanoseconds = runtime_ns(read_answer_found_evt);

    //region Free memory
    freeInfo(info);
    clReleaseMemObject(d_answer);
    clReleaseMemObject(d_dlxs);
    clReleaseMemObject(d_dlx_props);
    clReleaseMemObject(d_answer_data);

    free(tasks);
    free(dlx);
    free(convert_table);
    free(col_ids);
    free(row_ids);
    free(answer_data);
    for (int i = 0; i < N * N; ++i)
        free(valid_candidates[i]);
    free(valid_candidates);
    //endregion

    task.completed = 1;

    return task;
}

int permutate_tasks(const int *dlx, int dlx_size, int *tasks, int tasks_size) {
    const int *b_down = dlx + 1 * dlx_size;
    const int *b_right = dlx + 3 * dlx_size;

    int count = 0;

    // iterate each column
    int c_col;
    for (c_col = b_right[0]; c_col != 0; c_col = b_right[c_col]) {
        // iterate each row
        int c_row;
        for (c_row = b_down[c_col]; c_row != c_col; c_row = b_down[c_row]) {
            if (count > tasks_size) {
                fprintf(stderr, "slots not enough: tried to generate %d-th task but only %d slots available.\n",
                        ++count, tasks_size);
                continue;
            }

            tasks[count++] = c_row;
        }
    }
    return count;
}

cl_event
execute_exact_cover_kernel(cl_command_queue q, cl_kernel k, size_t task_count, size_t lws, cl_int n, cl_mem d_tasks,
                           cl_mem d_dlx, cl_mem d_dlxs, cl_mem d_dlx_props, cl_int dlx_size, cl_mem d_ans,
                           cl_mem d_ans_found, cl_event *waitingList, int waitingListSize) {
    cl_int err;
    int i = 0;
    int N = n * n;

//    global int *tasks, global int *_dlx
//    global int *dlxs, global int *dlx_props,
//    global int *answer, global int *answer_found,
//    int dlx_size, int N, int task_count,
//    local int *stacks

    AddKernelArg(k, i++, sizeof(d_tasks), &d_tasks);
    AddKernelArg(k, i++, sizeof(d_dlx), &d_dlx);
    AddKernelArg(k, i++, sizeof(d_dlxs), &d_dlxs);
    AddKernelArg(k, i++, sizeof(d_dlx_props), &d_dlx_props);
    AddKernelArg(k, i++, sizeof(d_ans), &d_ans);
    AddKernelArg(k, i++, sizeof(d_ans_found), &d_ans_found);
    AddKernelArg(k, i++, sizeof(int), &dlx_size);
    AddKernelArg(k, i++, sizeof(int), &N);
    AddKernelArg(k, i++, sizeof(int), &task_count);

    AddKernelArg(k, i++, sizeof(int) * N * N * lws, NULL);

    struct MemoryString memory = memory_string(sizeof(int) * N * N * lws);
    printf("Local Memory: %zu %s\n", memory.value, memory.unit);

    size_t wgn = (task_count + lws - 1) / lws;
    size_t gws = wgn * lws;

    cl_event kernel_evt;
    err = clEnqueueNDRangeKernel(q, k, 1, NULL, &gws, &lws, waitingListSize, waitingList, &kernel_evt);
    ocl_check(err, "launch kernel");
    return kernel_evt;
}
