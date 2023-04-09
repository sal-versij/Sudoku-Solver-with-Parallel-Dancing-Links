#include <stdio.h>
#include <stdlib.h>

#define CL_TARGET_OPENCL_VERSION 120

#include "ocl_boiler.h"
#include "setup.h"

#define UNLOAD_NO_PROPS(dlx, dlx_size) \
    up    = (dlx); \
    down  = (dlx) + dlx_size; \
    left  = (dlx) + dlx_size * 2; \
    right = (dlx) + dlx_size * 3;
#define UNLOAD(dlx, dlx_props, dlx_size) \
    UNLOAD_NO_PROPS(dlx, dlx_size) \
    col   = (dlx_props); \
    row   = (dlx_props) + dlx_size;

cl_event
execute_exact_cover_kernel(cl_command_queue q, cl_kernel k, size_t task_count, size_t lws, cl_int n, cl_mem d_dlxs,
                           cl_mem d_dlx_props, cl_int dlx_size, cl_mem d_ans, cl_mem d_ans_found, cl_event *waitingList,
                           int waitingListSize);

void remove_column(int id, int *dlx, int dlx_size);

int permutate_tasks(const int *dlx, int dlx_size, int n, int *dlxs, int *answers, int slots);

void initial_check(const int *board, int n, int **valid_candidates, int *placed);

int build_dancing_links(const int *col_ids, const int *row_ids, int n, int **dlx_ptr);

int convert_matrix(const int *board, int **valid_candidates, int placed, int n, int **cols_ptr, int **rows_ptr,
                   int **convert_table_ptr);

void convert_answer_print(int task_row, const int *ans, const int *convert_table, int N);

void solve(const int *board, int n, int lws);

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <sudoku> <tile_size>\n", argv[0]);
        return 1;
    }

    int n;
    int *board = read_board(argv[1], &n);

    const int N = n * n;
    const int lws = atoi(argv[2]);

    printf("Sudoku loaded: %d x %d\n", N, N);
    print_board(board, N);

    solve(board, n, lws);

    free(board);
    return 0;
}

void solve(const int *board, int n, int lws) {
    int N = n * n;
    struct MemoryString memory;

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

    memory = memory_string(dlx_size * 4 * sizeof(int));

    printf("Number of nodes in dancing links: %d (~%llu %s)\n", dlx_size,
           memory.value, memory.unit);
    //endregion

    //region Generate tasks
    int estimated_tasks_count = dlx_size - N * N - 1;

    memory = memory_string(dlx_size * 4 * estimated_tasks_count * sizeof(int));

    printf("Generating %d tasks (taking ~%llu %s of memory)...\n", estimated_tasks_count, memory.value, memory.unit);

    int *dlxs = (int *) malloc(dlx_size * 4 * estimated_tasks_count * sizeof(int));
    int *answers = (int *) malloc(sizeof(int) * estimated_tasks_count);

    int c_tasks_count = permutate_tasks(dlx, dlx_size, n, dlxs, answers, estimated_tasks_count);
    if (c_tasks_count > estimated_tasks_count) {
        fprintf(stderr, "Too many tasks generated: %d > %d\n", c_tasks_count, estimated_tasks_count);
        return;
    }
    if (c_tasks_count < 0) {
        fprintf(stderr, "Unknown error: Alredy solved?.\n");
        return;
    }

    for (int i = 0; i < c_tasks_count; ++i) {
        if (convert_table[answers[i]] / N > N * N) {
            fprintf(stderr, "Invalid task %d: %d -> %d (%d: %d)\n",
                    i, answers[i], convert_table[answers[i]], convert_table[answers[i]] / N,
                    convert_table[answers[i]] % N + 1);

            return;
        }
    }

    printf("Tasks generated.\n");
    //endregion

    //region GPU Search

    printf("Starting GPU search...\n");

    //region Initialization
    cl_int err;
    int *answer_data = (int *) malloc(sizeof(int) * 2);
    answer_data[0] = -1;
    answer_data[1] = 0;

    struct Info info = initialize("dlx_kernels.cl", "exact_cover_kernel");

    cl_mem d_dlxs = clCreateBuffer(info.context, CL_MEM_READ_WRITE | CL_MEM_HOST_WRITE_ONLY,
                                   dlx_size * 4 * c_tasks_count * sizeof(int), NULL, &err);
    ocl_check(err, "create buffer for dlxs");

    cl_mem d_dlx_props = clCreateBuffer(info.context,
                                        CL_MEM_READ_ONLY | CL_MEM_HOST_WRITE_ONLY,
                                        dlx_size * 2 * sizeof(int), NULL, &err);
    ocl_check(err, "create buffer for dlx_props");

    cl_mem d_answer = clCreateBuffer(info.context, CL_MEM_WRITE_ONLY | CL_MEM_HOST_READ_ONLY,
                                     N * N * sizeof(int),
                                     NULL, &err);
    ocl_check(err, "create buffer for ans");

    cl_mem d_answer_data = clCreateBuffer(info.context,
                                          CL_MEM_READ_WRITE,
                                          sizeof(int) * 2, NULL, &err);
    ocl_check(err, "create buffer for ans_found");

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
    cl_event evt_writes[3];
    err = clEnqueueWriteBuffer(info.queue, d_answer_data,
                               CL_FALSE, 0, sizeof(int) * 2, answer_data,
                               0, NULL, &evt_writes[0]);
    ocl_check(err, "write answer_data");

    err = clEnqueueWriteBuffer(info.queue, d_dlxs,
                               CL_FALSE, 0, dlx_size * 4 * c_tasks_count * sizeof(int), dlxs,
                               0, NULL, &evt_writes[1]);
    ocl_check(err, "write dlxs");

    err = clEnqueueWriteBuffer(info.queue, d_dlx_props,
                               CL_FALSE, 0, dlx_size * 2 * sizeof(int), dlx_props,
                               0, NULL, &evt_writes[2]);
    ocl_check(err, "write dlx_props");
    //endregion

    cl_event kernel_evt = execute_exact_cover_kernel(
            info.queue, info.kernel,
            c_tasks_count, lws, n,
            d_dlxs, d_dlx_props, dlx_size, d_answer, d_answer_data,
            evt_writes, 3);

    //region Read answer

    cl_event read_answer_found_evt;
    err = clEnqueueReadBuffer(info.queue, d_answer_data,
                              CL_TRUE, 0, 2 * sizeof(int), answer_data,
                              1, &kernel_evt, &read_answer_found_evt);
    ocl_check(err, "read answer_data");

    printf("GPU search finished.\n");

    int answer_found = answer_data[0];
    int answer_length = answer_data[1];

    if (answer_found && answer_length > 0) {
        int *answer = (int *) malloc(N * N * sizeof(int));
        cl_event read_answer_evt;
        err = clEnqueueReadBuffer(info.queue, d_answer,
                                  CL_TRUE, 0, N * N * sizeof(int), answer,
                                  1, &read_answer_found_evt, &read_answer_evt);
        ocl_check(err, "read answer");

        for (int i = 0; i < answer_length; ++i) answer[i] = dlx_props[answer[i] + dlx_size]; // convert to row numbers
        convert_answer_print(answers[answer_found], answer, convert_table, N);

        free(answer);
    }
    //endregion
    //endregion

    //region Free memory
    freeInfo(info);
    clReleaseMemObject(d_answer);
    clReleaseMemObject(d_dlxs);
    clReleaseMemObject(d_dlx_props);
    clReleaseMemObject(d_answer_data);

    free(dlxs);
    free(dlx);
    free(convert_table);
    free(col_ids);
    free(row_ids);
    free(answer_data);
    for (int i = 0; i < N * N; ++i) free(valid_candidates[i]);
    free(valid_candidates);
    //endregion
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

    for (i = 0; i < N * N; ++ i) {
        for(j = 0; j < N; ++ j){
            if(valid_candidates[i][j]) {
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

int permutate_tasks(const int *dlx, int dlx_size, int n, int *dlxs, int *answers, int slots) {
    const int *b_down = dlx + 1 * dlx_size;
    const int *b_right = dlx + 3 * dlx_size;
    const int *b_col = dlx + 4 * dlx_size;
    const int *b_row = dlx + 5 * dlx_size;

    int count = 0;

    // iterate each column
    int c_col;
    for (c_col = b_right[0]; c_col != 0; c_col = b_right[c_col]) {
        // iterate each row
        int c_row;
        for (c_row = b_down[c_col]; c_row != c_col; c_row = b_down[c_row]) {
            if (count > slots) {
                fprintf(stderr, "slots not enough: tried to generate %d-th task but only %d slots available.\n",
                        ++count, slots);
                continue;
            }

            memcpy(dlxs + count * dlx_size * 4, dlx, sizeof(int) * dlx_size * 4);
            int *c_dlx = dlxs + count * dlx_size * 4;

            const int *c_right = dlx + 3 * dlx_size;

//            printf("[#%d] task: (%d, %d)\n", count, c_col, c_row);
//            printf("[#%d] right[0]: %d\n", count, c_right[0]);

            remove_column(c_col, c_dlx, dlx_size);
            for (int elem = c_right[c_row]; elem != c_row; elem = c_right[elem]) {
                remove_column(b_col[elem], c_dlx, dlx_size);
            }

            if (c_right[0] == 0) {
                printf("Impossible solution: (%d, %d)", c_col, c_row);
                // no more column, we have found a solution
                return -1;
            }

            answers[count++] = b_row[c_row];
        }
    }
//    printf("Total count: %d\n", count);
    return count;
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

cl_event
execute_exact_cover_kernel(cl_command_queue q, cl_kernel k, size_t task_count, size_t lws, cl_int n, cl_mem d_dlxs,
                           cl_mem d_dlx_props, cl_int dlx_size, cl_mem d_ans, cl_mem d_ans_found, cl_event *waitingList,
                           int waitingListSize) {
    cl_int err;
    int i = 0;
    int N = n * n;

//    global int *dlxs, global int *dlx_props,
//    global int *answer, global int *answer_found,
//    int dlx_size, int N, int task_count,
//    local int *stacks

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
