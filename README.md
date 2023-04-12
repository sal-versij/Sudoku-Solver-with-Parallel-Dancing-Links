# Parallel Sudoku Solver with Dancing Links

## Prerequisites

- `C` compilers
- OpenCL SDK for at least one of your platforms

## Build

1. Check the `CMakeLists.txt` to suits your environment variables and compiler
2. Setup the toolchain for your compiler
3. Run `cmake` to generate the build files
4. Run `cmake --build .\build --target dlx_parallel` to build the parallel project
4. Run `cmake --build .\build --target dlx_serial` to build the serial project

> An example of building with `ninja` on Windows
>
> ```shell
> cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM=ninja -G Ninja -S .\ -B .\build
> cmake --build .\build --target dlx_parallel -j 3
> ```

## Example

```shell
.\build\dancing_links_parallel.exe .\inputs\4.txt 1
```

```
Sudoku loaded: 9 x 9
9    |  3 7|6       
     |6    |  9     
    8|     |    4   
-----+-----+-----   
  9  |     |    1   
6    |     |    9
3    |     |  4
-----+-----+-----
7    |     |8
  1  |    9|
    2|5 4  |      

Initializing dlx...
DLX Grid size: 276 x 324
Number of nodes in dancing links: 1429 (~22 KB)
Generating 1347 tasks (taking ~29 MB of memory)...
1104 tasks generated (taking ~24 MB of memory).
Starting GPU search...
Device buffer tasks size: 1104 (4 KB)
Device buffer dlx size: 5716 (22 KB)
Device buffer dlxs size: 6310464 (24 MB)
Device buffer dlx_props size: 2858 (11 KB)
Device buffer answer size: 81 (324 B)
Device buffer answer_data size: 2 (8 B)
Local Memory: 324 B
GPU search finished.
9 5 4|1 3 7|6 8 2
2 7 3|6 8 4|1 9 5
1 6 8|2 9 5|7 3 4
-----+-----+-----
4 9 5|7 2 8|3 6 1
6 8 1|4 5 3|2 7 9
3 2 7|9 6 1|5 4 8
-----+-----+-----
7 4 9|3 1 2|8 5 6
5 1 6|8 7 9|4 2 3
8 3 2|5 4 6|9 1 7 
```