int check_partial_board_d(int *board, int n, int p, int num) {
  int j;
  int N = n * n;
  int row = p / (n * N);
  int col = (p % N) / n;
  int box_top_left = row * n * N + col * n;
  int now_row = ((p) / N);
  // check row
  for (j = now_row * N; j < (now_row + 1) * N; ++j)
    if (board[j] == num)
      return 0;
  // check col
  for (j = ((p) % N); j < N * N; j += N)
    if (board[j] == num)
      return 0;
  // check box
  for (j = 0; j < N; ++j)
    if (board[box_top_left + (j / n) * N + (j % n)] == num)
      return 0;
  return 1;
}

kernel void backtrack_kernel(global int *boards, global int *ans_found, int n,
                             local int *shared_boards, local int *stacks) {
  int l_id = get_local_id(0);
  int l_size = get_local_size(0);
  int g_id = get_global_id(0);
  int slots = get_global_size(0);
  int group = get_group_id(0);

  int N = n * n;
  int slot_size = N * N;

  int i;
  // if (g_id == 0)
  //   printf("slot_size * slots: %d\n", slot_size * l_size);
  for (i = 0; i < slot_size * l_size; i += l_size) {
    // printf("[%2d:%2d:%2d]: (%d) %d -> %d\n", g_id, l_id, group, i, g_id + i,
    //        l_id + i);
    shared_boards[l_id + i] = boards[g_id + i];
  }
  barrier(CLK_LOCAL_MEM_FENCE);

  int *board = shared_boards + l_id * slot_size;
  int *stack = stacks + l_id * slot_size;

  int last_op = 0; // 0 - push stack, 1 - pop stack
  int top = 0, nowp = 0;

  while (*ans_found == -1) {
    int num_to_try;
    if (last_op == 0) { // push stack

      // search first empty cell
      for (; nowp < slot_size && board[nowp] != 0; ++nowp)
        ;
      // check if the board is filled
      if (nowp == slot_size) {
        // answer found
        int old = atomic_cmpxchg(ans_found, -1, g_id);
        if (old == -1) {
          // copy back to global memory
          for (i = 0; i < slot_size; ++i)
            boards[g_id * slot_size + i] = board[i];
        }
        break;
      }
      // initialize the number to try
      num_to_try = 1;
    } else {
      // read stack top and restore
      int stack_num = stack[top];
      nowp = stack_num % (N * N);
      num_to_try = board[nowp] + 1;
    }

    // find next valid number
    for (; num_to_try <= N; ++num_to_try) {
      if (check_partial_board_d(board, n, nowp, num_to_try)) {
        // push stack
        stack[top++] = nowp;
        // move to next location
        board[nowp] = num_to_try;
        last_op = 0;
        break;
      }
    }
    if (num_to_try > N) {
      // pop stack
      if (top == 0)
        break;
      board[nowp] = 0;
      top--;
      last_op = 1;
    }
  }
}