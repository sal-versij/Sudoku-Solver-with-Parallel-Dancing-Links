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

    AddKernelArg(k, i++, sizeof(d_boards), &d_boards);
    AddKernelArg(k, i++, sizeof(d_ans_found), &d_ans_found);
    AddKernelArg(k, i++, sizeof(n), &n);

    size_t supportSize = lws * N * N;
    printf("Support size: %zu\n", supportSize);
    printf("LWS: %zu\n", lws);

    AddKernelArg(k, i++, supportSize, NULL);
    AddKernelArg(k, i++, supportSize, NULL);

    size_t gws = slots;

    cl_event kernel_evt;
    err = clEnqueueNDRangeKernel(q, k, 1, NULL, &gws, &lws, 0, NULL, &kernel_evt);
    ocl_check(err, "launch kernel");
    return kernel_evt;
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

        struct Info info = initialize("backtrack_kernel");
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
