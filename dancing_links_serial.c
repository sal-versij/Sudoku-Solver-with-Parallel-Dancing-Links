#include <stdio.h>
#include <stdlib.h>

#define CL_TARGET_OPENCL_VERSION 120

#include "ocl_boiler.h"
#include "setup.h"

#define PUSH(v)                                                                \
  stack[top++] = v;                                                            \
  last_op = 0;

#define POP()                                                                  \
  --top;                                                                       \
  last_op = 1;

int exact_cover(int *dlx, const int *dlx_props, int *answer, int dlx_size, int N);

void solve(const int *board, int n);

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <sudoku>\n", argv[0]);
        return 1;
    }

    int n;
    int *board = read_board(argv[1], &n);

    const int N = n * n;
    const int lws = atoi(argv[2]);

    printf("Sudoku loaded: %d x %d\n", N, N);
    print_board(board, N);

    solve(board, n);

    free(board);
    return 0;
}

void solve(const int *board, int n) {
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

    //region Search
    int *answer = malloc(N * N * sizeof(int));
    int answer_length = exact_cover(dlx, dlx_props, answer, dlx_size, N);

    for (int i = 0; i < answer_length; ++i) answer[i] = dlx_props[answer[i] + dlx_size]; // convert to row numbers
    convert_answer_print_serial(answer, convert_table, N);

    free(answer);
    //endregion

    //region Free memory
    free(dlx);
    free(convert_table);
    free(col_ids);
    free(row_ids);
    for (int i = 0; i < N * N; ++i) free(valid_candidates[i]);
    free(valid_candidates);
    //endregion
}

int exact_cover(int *dlx, const int *dlx_props, int *answer, int dlx_size, int N) {
    int *col = dlx_props;

    int *up, *down, *left, *right;
    int *stack = malloc(N * N * sizeof(int));
    UNLOAD_NO_PROPS(dlx, dlx_size)

    int top = 0;
    int last_op = 0; // 0 - push stack, 1 - pop stack
    int c_col, c_row;
    while (1) {
        if (last_op == 0) {
            if (right[0] == 0) {
                memcpy(answer, stack, top * sizeof(int));
                return top;
            }

            c_col = right[0];
            c_row = down[c_col];
            if (c_row == c_col) {
                // this column has not been covered
                if (top == 0)
                    return 0;
                POP()
                continue;
            }
        } else {
            // read stack top and restore

            c_row = stack[top];
            for (int elem = right[c_row]; elem != c_row; elem = right[elem])
                restore_column(col[elem], dlx, dlx_size);
            restore_column(col[c_row], dlx, dlx_size);
            c_row = down[c_row]; // go to next row

            // this column has finished iteration
            if (c_row == right[0]) {
                // pop stack
                if (top == 0)
                    return 0;
                POP()
                continue;
            }
        }

        remove_column(col[c_row], dlx, dlx_size);
        for (int elem = right[c_row]; elem != c_row; elem = right[elem])
            remove_column(col[elem], dlx, dlx_size);

        PUSH(c_row)
    }
}