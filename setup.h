#include <math.h>

#define SERIAL_COORD(i, j, N) ((i) * N + (j))
#define ROW(p, N) ((p) / N)
#define COL(p, N) ((p) % N)
#define BOX(p, n) ((p) / (n * n * n) * n + ((p) % (n * n)) / n)

int *read_board(const char *file_name, int *n) {
    FILE *fp = fopen(file_name, "r");
    int *board = NULL;

    fscanf(fp, "%d", n);
    int N = *n * *n;
    int total = N * N, i;
    board = (int *) calloc(total, sizeof(int));
    for (i = 0; i < total; ++i)
        fscanf(fp, "%d", board + i);
    return board;
}

void _print_board_le9(const int *board, int N) {
    int n = sqrt(N);

    int i, j, cell;
    for (i = 0; i < N; ++i) {
        for (j = 0; j < N; ++j) {
            cell = board[SERIAL_COORD(i, j, N)];
            if (cell > 0) {
                printf("%d", cell);
            } else {
                printf(" ");
            }

            if (j % n == (n - 1) && j < (N - 1)) printf("|");
            else printf(" ");
        }
        if (i % n == (n - 1) && i < (N - 1)) {
            printf("\n");
            for (j = 0; j < N; ++j) {
                printf("-");
                if (j % n == (n - 1) && j < (N - 1)) {
                    printf("+");
                } else if (j % N == (N - 1)) {
                    printf(" ");
                } else {
                    printf("-");
                }
            }
            printf("\n");
        } else printf("\n");

    }

    printf("\n");
}

void _print_board_gt9(const int *board, int N) {
    int n = sqrt(N);

    int i, j, cell;
    for (i = 0; i < N; ++i) {
        for (j = 0; j < N; ++j) {
            cell = board[SERIAL_COORD(i, j, N)];
            if (cell > 0) {
                if (cell > 9) {
                    printf("%d", cell);
                } else {
                    printf(" %d", cell);

                }
            } else {
                printf("  ");
            }

            if (j % n == (n - 1) && j < (N - 1)) printf("|");
            else printf(" ");
        }
        if (i % n == (n - 1) && i < (N - 1)) {
            printf("\n");
            for (j = 0; j < N; ++j) {
                printf("--");
                if (j % n == (n - 1) && j < (N - 1)) {
                    printf("+");
                } else if (j % N == (N - 1)) {
                    printf(" ");
                } else {
                    printf("-");
                }
            }
            printf("\n");
        } else printf("\n");

    }

    printf("\n");
}

void print_board(const int *board, int N) {
    if (N <= 9) {
        _print_board_le9(board, N);
    } else {
        _print_board_gt9(board, N);
    }
}

int check_partial_board(const int *board, int n, int p, int num) {
    int i;
    int N = n * n;
    int row = p / (n * N);
    int col = (p % N) / n;
    int box_top_left = row * n * N + col * n;
    int now_row = ROW(p, N);

    // check row
    for (i = now_row * N; i < (now_row + 1) * N; ++i)
        if (board[i] == num)
            return 0;

    // check col
    for (i = COL(p, N); i < N * N; i += N)
        if (board[i] == num)
            return 0;

    // check box
    for (i = 0; i < N; ++i)
        if (board[box_top_left + SERIAL_COORD((i / n), (i % n), N)] == num)
            return 0;

    return 1;
}

// initial search on CPU with (modified) breadth first search
// slots is the size of expected nodes
// array boards should be multiple of (N * N * slots)
int cpu_initial_search(int n, int *boards, int slots) {
    int N = n * n;
    int slot_size = N * N;

    // slot head
    int head = -1;
    // slot tail
    int tail = 1;

    int empty_slots = slots - 1;

    // mark the first element of every slot with -1 to indicate it is empty
    int i, cell;
    for (i = 1; i < slots; ++i) { boards[i * N * N] = -1; }

    // slots search tree
    while (empty_slots > 0 && empty_slots < slots) {
        // advance head pointer
        head++;
        if (head == slots) head = 0;
        int *current_board = boards + head * slot_size;
        if (current_board[0] == -1) continue;

        // find the first empty cell
        for (cell = 0; cell < N * N && current_board[cell] != 0; ++cell);
        if (cell == N * N) {
            // answer found
            memcpy(boards, current_board, slot_size * sizeof(int));
            return 1;
        }

        // reserve a value for in-place modification
        // which modifies the board at its original place to save one copy operation
        // this can also be used as flag of answer found or dead node
        int reserve = 0;
        for (i = 1; i <= N; ++i) {
            if (check_partial_board(current_board, n, cell, i)) {
                // fill in the reserved value
                if (reserve == 0) { reserve = i * N * N + cell; }
                else {
                    // find an empty slot
                    if (empty_slots == 0) return 0;
                    int *new_board;
                    do {
                        new_board = boards + tail * slot_size;
                        tail++;
                        if (tail == slots) tail = 0;
                    } while (new_board[0] != -1);
                    empty_slots--;
                    // copy and modify the board
                    memcpy(new_board, current_board, slot_size * sizeof(int));
                    new_board[cell] = i;
                }
            }
        }
        if (reserve == 0) {
            // dead node
            current_board[0] = -1; // mark current_board == -1 to indicate this slot is empty
            empty_slots++;
        } else {
            // in-place modification
            current_board[reserve % (N * N)] = reserve / (N * N);
        }
    }
    return 0;
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

struct Info initialize(const char *kernels_file, const char *kernel_name) {
    cl_int err;
    //region Initialize OpenCL
    cl_platform_id p = select_platform();
    cl_device_id d = select_device(p);
    cl_context ctx = create_context(p, d);
    cl_command_queue que = create_queue(ctx, d);
    //endregion

    //region Initialize Kernel
    cl_program prog = create_program(kernels_file, ctx, d);

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

struct MemoryString {
    size_t value;
    char *unit;
};

struct MemoryString memory_string(size_t bytes) {
    if (bytes < 1024) {
        return (struct MemoryString) {bytes, "B"};
    } else if (bytes < 1048576) {
        return (struct MemoryString) {bytes / 1024, "KB"};
    } else if (bytes < 1073741824) {
        return (struct MemoryString) {bytes / 1048576, "MB"};
    } else {
        return (struct MemoryString) {bytes / 1073741824, "GB"};
    }
}

struct Task {
    double runtime;
};

void printResult(FILE *stream, struct Task task) {
    fprintf(stream, "%f\n", task.runtime);

}

