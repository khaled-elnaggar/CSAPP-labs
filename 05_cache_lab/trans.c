/*
 * trans.c - Matrix transpose B = A^T
 *
 * Each transpose function must have a prototype of the form:
 * void trans(int M, int N, int A[N][M], int B[M][N]);
 *
 * A transpose function is evaluated by counting the number of misses
 * on a 1KB direct mapped cache with a block size of 32 bytes.
 */
#include <stdio.h>
#include "cachelab.h"

int is_transpose(int M, int N, int A[N][M], int B[M][N]);

/*
 * transpose_submit - This is the solution transpose function that you
 *     will be graded on for Part B of the assignment. Do not change
 *     the description string "Transpose submission", as the driver
 *     searches for that string to identify the transpose function to
 *     be graded.
 */
char transpose_submit_desc[] = "Transpose submission";
void transpose_submit(int M, int N, int A[N][M], int B[M][N])
{
    // get appropriate block size
    int bsize = 8;
    if (M == 64 && N == 64)
    {
        bsize = 4;
    }

    // calculate how many blocks fit
    int N_blocks = bsize * (N / bsize);
    int M_blocks = bsize * (M / bsize);

    int i, j, i1, j1;
    for (i = 0; i < N_blocks; i += bsize)
    {
        for (j = 0; j < M_blocks; j += bsize)
        {
            for (i1 = i; i1 < i + bsize; i1++)
            {
                for (j1 = j; j1 < j + bsize; j1++)
                {
                    B[j1][i1] = A[i1][j1];
                }
            }
        }
    }

    // do the leftover from blocks
    for (i = 0; i < N; i++)
    {
        for (j = M_blocks; j < M; j++)
        {
            B[j][i] = A[i][j];
        }
    }

    for (j = 0; j < M; j++)
    {
        for (i = N_blocks; i < N; i++)
        {

            B[j][i] = A[i][j];
        }
    }
}

/*
 * registerFunctions - This function registers your transpose
 *     functions with the driver.  At runtime, the driver will
 *     evaluate each of the registered functions and summarize their
 *     performance. This is a handy way to experiment with different
 *     transpose strategies.
 */
void registerFunctions()
{
    /* Register your solution function */
    registerTransFunction(transpose_submit, transpose_submit_desc);
}

/*
 * is_transpose - This helper function checks if B is the transpose of
 *     A. You can check the correctness of your transpose by calling
 *     it before returning from the transpose function.
 */
int is_transpose(int M, int N, int A[N][M], int B[M][N])
{
    int i, j;

    for (i = 0; i < N; i++)
    {
        for (j = 0; j < M; ++j)
        {
            if (A[i][j] != B[j][i])
            {
                return 0;
            }
        }
    }
    return 1;
}
