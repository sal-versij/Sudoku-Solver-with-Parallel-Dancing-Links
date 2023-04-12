#define UNLOAD(dlx, dlx_size)                                                  \
  up = (dlx);                                                                  \
  down = (dlx) + dlx_size;                                                     \
  left = (dlx) + dlx_size * 2;                                                 \
  right = (dlx) + dlx_size * 3;

#define PUSH(v)                                                                \
  stack[top++] = v;                                                            \
  last_op = 0;

#define POP()                                                                  \
  --top;                                                                       \
  last_op = 1;

void remove_column_d(int id, __global int *dlx, int dlx_size) {
  __global int *up, *down, *left, *right;
  UNLOAD(dlx, dlx_size);

  // first detach the column indicator
  right[left[id]] = right[id];
  left[right[id]] = left[id];

  // find every row of this column
  for (int c_row = down[id]; c_row != id; c_row = down[c_row]) {
    // find every element in that row
    for (int elem = right[c_row]; elem != c_row; elem = right[elem]) {
      // detach that element
      down[up[elem]] = down[elem];
      up[down[elem]] = up[elem];
    }
  }
}

void restore_column_d(int id, __global int *dlx, int dlx_size) {
  __global int *up, *down, *left, *right;
  UNLOAD(dlx, dlx_size);

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

kernel void exact_cover_kernel(global int *tasks, global int *_dlx,
                               global int *dlxs, global const int *dlx_props,
                               global int *answer, global int *answer_found,
                               int dlx_size, int N, int task_count,
                               local int *stacks) {
  int l_id = get_local_id(0);
  int g_id = get_global_id(0);
  int l_size = get_local_size(0);

  // printf("[%d:%d] starting! answer found: %d (%d)\n", g_id, l_id,
  //        answer_found[0], answer_found[1]);
  if (g_id >= task_count || answer_found[0] != -1)
    return;

  const __global int *col = dlx_props;
  const __global int *row = dlx_props + dlx_size;

  __global int *up, *down, *left, *right;
  __global int *dlx = dlxs + g_id * dlx_size * 4;
  __local int *stack = stacks + l_id * N * N;

  for (int i = 0; i < dlx_size * 4; ++i) {
    dlx[i] = _dlx[i];
  }
  barrier(CLK_LOCAL_MEM_FENCE);

  UNLOAD(dlx, dlx_size);

  int first_row = tasks[g_id];

  remove_column_d(col[first_row], dlx, dlx_size);
  for (int elem = right[first_row]; elem != first_row; elem = right[elem])
    remove_column_d(col[elem], dlx, dlx_size);

  int top = 0;
  int last_op = 0; // 0 - push stack, 1 - pop stack
  int c_col, c_row;
  // printf("[%d:%d] Starting to search", g_id, l_id);
  while (*answer_found == -1) {
    // printf("[%d:%d] top: %d, last_op: %d, right[0]:%d\n", g_id, l_id, top,
    //        last_op, right[0]);
    if (last_op == 0) {
      if (right[0] == 0) {
        // every element has been covered, answer found
        int old = atomic_cmpxchg(answer_found, -1, g_id);

        // printf("[%d:%d] another answer! Previous: %d (%d)\n", g_id, l_id,
        //        answer_found[0], answer_found[1]);

        if (old == -1) {
          // copy answer to global memory

          answer_found[1] = top;
          // printf("answer: %d (%d)\n", answer_found[0], answer_found[1]);

          for (int i = 0; i < top; ++i) {
            answer[i] = stack[i];
          }
        }
        break;
      }

      c_col = right[0];
      c_row = down[c_col];
      if (c_row == c_col) {
        // this column has not been covered
        if (top == 0)
          break;
        POP()
        continue;
      }
    } else {
      // read stack top and restore

      c_row = stack[top];
      for (int elem = right[c_row]; elem != c_row; elem = right[elem])
        restore_column_d(col[elem], dlx, dlx_size);
      restore_column_d(col[c_row], dlx, dlx_size);
      c_row = down[c_row]; // go to next row

      // this column has finished iteration
      if (c_row == right[0]) {
        // pop stack
        if (top == 0)
          break;
        POP()
        continue;
      }
    }

    remove_column_d(col[c_row], dlx, dlx_size);
    for (int elem = right[c_row]; elem != c_row; elem = right[elem])
      remove_column_d(col[elem], dlx, dlx_size);

    PUSH(c_row)
  }
  // printf("[%d:%d] no answer!\n", g_id, l_id);
}