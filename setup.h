#include <math.h>

#define SERIAL_COORD(i, j, N) ((i) * N + (j))
#define ROW(p, N) ((p) / N)
#define COL(p, N) ((p) % N)
#define BOX(p, n) ((p) / (n * n * n) * n + ((p) % (n * n)) / n)
#define UNLOAD_NO_PROPS(dlx, dlx_size) \
    up    = (dlx); \
    down  = (dlx) + dlx_size; \
    left  = (dlx) + dlx_size * 2; \
    right = (dlx) + dlx_size * 3;
#define UNLOAD(dlx, dlx_props, dlx_size) \
    UNLOAD_NO_PROPS(dlx, dlx_size) \
    col   = (dlx_props); \
    row   = (dlx_props) + dlx_size;


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

// populate the valid_candidates array with the possible candidates for each cell
void initial_check(const int *board, int n, int **valid_candidates, int *placed) {
    int N = n * n, i, cand;
    *placed = 0;
    for (i = 0; i < N * N; ++i) {
        if (board[i] != 0) {
            ++(*placed);
            continue;
        }
        for (cand = 0; cand < N; ++cand) {
            if (check_partial_board(board, n, i, cand + 1))
                valid_candidates[i][cand] = 1;
        }
    }
}

int build_dancing_links(const int *col_ids, const int *row_ids, int n, int **dlx_ptr) {

    // calculate number of nodes
    int num_cols = 0, num_rows = 0, i;
    for (i = 0; i < n; ++i) {
        if (col_ids[i] > num_cols)
            num_cols = col_ids[i];
        if (row_ids[i] > num_rows)
            num_rows = row_ids[i];
    }
    ++num_cols;
    ++num_rows;
    printf("DLX Grid size: %d x %d\n", num_rows, num_cols);

    int count = num_cols + n + 1;// 1 for the head, n for the singular elements, num_cols for the column indicators

    // allocate memory for dancing links
    *dlx_ptr = calloc(count * 6, sizeof(int));
    int *up, *down, *left, *right, *col, *row;
    UNLOAD(*dlx_ptr, *dlx_ptr + 4 * count, count)

    // build column indicators first
    int now_id = 1;
    for (i = 0; i < num_cols; ++i) {
        left[now_id] = now_id - 1;
        right[now_id - 1] = now_id;
        up[now_id] = down[now_id] = col[now_id] = now_id;
        now_id++;
    }
    right[now_id - 1] = 0;
    left[0] = now_id - 1;

    // save pointers to one element in that row
    // for faster allocation of rows
    int *row_ptrs = calloc(num_rows, sizeof(int));

    // insert elements
    for (i = 0; i < n; ++i, ++now_id) {
        // add vertical edges
        int col_ptr_id = col_ids[i] + 1;
        col[now_id] = col_ptr_id;
        down[now_id] = down[col_ptr_id];
        down[col_ptr_id] = now_id;
        up[now_id] = col_ptr_id;
        up[down[now_id]] = now_id;

        // add horizontal edges
        int row_num = row[now_id] = row_ids[i];
        if (row_ptrs[row_num] == 0) {
            // first element in this row
            left[now_id] = right[now_id] = now_id;
            row_ptrs[row_num] = now_id;
        } else {
            int row_ptr_id = row_ptrs[row_num];
            right[now_id] = right[row_ptr_id];
            right[row_ptr_id] = now_id;
            left[now_id] = row_ptr_id;
            left[right[now_id]] = now_id;
        }
    }

    free(row_ptrs);
    return count;
}

// convert a Sudoku matrix to exact cover matrix
int convert_matrix(const int *board, int **valid_candidates, int placed, int n, int **cols_ptr, int **rows_ptr,
                   int **convert_table_ptr) {
    //https://www.jianshu.com/p/93b52c37cc65
    int N = n * n, i, j;
    int total = N * N;
    // first compute the number of 1
    int elements_count = placed;

    for (i = 0; i < N * N; ++i) {
        for (j = 0; j < N; ++j) {
            if (valid_candidates[i][j]) {
                ++elements_count;
            }
        }
    }

    *cols_ptr = (int *) malloc(sizeof(int) * elements_count * 4);
    *rows_ptr = (int *) malloc(sizeof(int) * elements_count * 4);
    *convert_table_ptr = (int *) malloc(sizeof(int) * elements_count);
    int *cols = *cols_ptr;
    int *rows = *rows_ptr;
    int *convert_table = *convert_table_ptr;

#define insert_number_into_matrix(num) { \
    /* a number is put into grid i */ \
    rows[elem] = row_num; \
    cols[elem ++] = i; \
    /* number num is put into row ROW(i) */ \
    rows[elem] = row_num; \
    cols[elem ++] = total + ROW(i, N) * N + num - 1;\
    /* number num is put into column COL(i) */ \
    rows[elem] = row_num; \
    cols[elem ++] = total * 2 + COL(i, N) * N + num - 1;\
    /* number num is put into box BOX(i) */ \
    rows[elem] = row_num; \
    cols[elem ++] = total * 3 + BOX(i, n) * N + num - 1;\
}

    int row_num = 0, elem = 0;
    for (i = 0; i < N * N; ++i) {
        if (board[i] == 0) {
            for (j = 0; j < N; ++j) {
                if (valid_candidates[i][j]) {
                    insert_number_into_matrix(j + 1);
                    convert_table[row_num] = (i * N) + j;
                    row_num++;
                }
            }
        } else {
            insert_number_into_matrix(board[i]);
            convert_table[row_num] = (i * N) + board[i] - 1;
            row_num++;
        }
    }

//    printf("Max row number: %d after %d steps with %d elements\n", row_num, i, elem);

    return elem;
}

// convert an exact cover answer to a Sudoku answer and print
void convert_answer_print(int task_row, const int *ans, const int *convert_table, int N) {
    int i;
    int *answer_board = calloc(N * N, sizeof(int));
    int pos_and_num = convert_table[task_row];
    answer_board[pos_and_num / N] = pos_and_num % N + 1;
//    printf("task row: %d, convert table: %d, pos: %d, num: %d\n",
//           task_row, convert_table[task_row], pos_and_num / N, pos_and_num % N + 1
//    );
    for (i = 0; i < N * N - 1; ++i) {
        pos_and_num = convert_table[ans[i]];
        answer_board[pos_and_num / N] = pos_and_num % N + 1;
    }
    print_board(answer_board, N);
    free(answer_board);
}

void convert_answer_print_serial(const int *ans, const int *convert_table, int N) {
    int i;
    int *answer_board = calloc(N * N, sizeof(int));
    int pos_and_num;
    for (i = 0; i < N * N; ++i) {
        pos_and_num = convert_table[ans[i]];
        answer_board[pos_and_num / N] = pos_and_num % N + 1;
    }
    print_board(answer_board, N);
    free(answer_board);
}

void remove_column(int id, int *dlx, int dlx_size) {
    int *up, *down, *left, *right;
    UNLOAD_NO_PROPS(dlx, dlx_size);

    // first detach the column indicator
    right[left[id]] = right[id];
    left[right[id]] = left[id];

    // find every row of this column
    int row_id, elem;
    for (row_id = down[id]; row_id != id; row_id = down[row_id]) {
        // find every element in that row
        for (elem = right[row_id]; elem != row_id; elem = right[elem]) {
            // detach that element
            down[up[elem]] = down[elem];
            up[down[elem]] = up[elem];
        }
    }
}

void restore_column(int id, int *dlx, int dlx_size) {
    int *up, *down, *left, *right;
    UNLOAD_NO_PROPS(dlx, dlx_size);

    // first detach the column indicator
    right[left[id]] = id;
    left[right[id]] = id;

    // find every row of this column
    for (int c_row = down[id]; c_row != id; c_row = down[c_row]) {
        // find every element in that row
        for (int elem = right[c_row]; elem != c_row; elem = right[elem]) {
            // attach that element
            down[up[elem]] = elem;
            up[down[elem]] = elem;
        }
    }
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

