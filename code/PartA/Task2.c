#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <math.h>
//#define MAX_LIMIT 20
//#define N 10

int malloc2dint(int ***array, int n, int m)
{
    //allocate the n*m contiguous items
    int *p = (int *)malloc(n * m * sizeof(int));
    if (!p)
        return -1;
    //allocate the row pointers into the memory */
    (*array) = (int **)malloc(n * sizeof(int *));
    if (!(*array))
    {
        free(p);
        return -1;
    }
    //set up the pointers into the contiguous memory
    for (int i = 0; i < n; i++)
        (*array)[i] = &(p[i * m]);
    return 0;
}

int free2dint(int ***array)
{
    free(&((*array)[0][0]));
    free(*array);
    return 0;
}

int main(int argc, char **argv)
{
    MPI_Init(&argc, &argv);
    int comm_size;
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
    if ((comm_size & (comm_size - 1)) != 0)
    {
        printf("This application must be run with square amount of MPI processes.\n");
        MPI_Abort(MPI_COMM_WORLD, -1);
    }
    int my_rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
    MPI_Status status;
    int column1, row1, column2, row2;
    int **A, **B, **C;
    int squaredP = sqrt(comm_size);
    FILE *iptr;
    FILE *optr;

    if (my_rank == 0)
    {
        if ((iptr = fopen(argv[1], "r")) == NULL)
        {
            printf("Error! opening file");
            // Program exits if the file pointer returns NULL.
            exit(1);
        }
        fscanf(iptr, "%d", &row1);
        fscanf(iptr, "%d", &column1);
        malloc2dint(&A, row1, column1);
        for (int i = 0; i < row1; i++)
        {
            for (int j = 0; j < column1; j++)
            {
                fscanf(iptr, "%d\t", &A[i][j]);
            }
        }
        fscanf(iptr, "%d", &row2);
        fscanf(iptr, "%d", &column2);
        malloc2dint(&B, row2, column2);
        for (int i = 0; i < row2; i++)
        {
            for (int j = 0; j < column2; j++)
            {
                fscanf(iptr, "%d\t", &B[i][j]);
            }
        }
        malloc2dint(&C, column2, row1);
    }
    MPI_Bcast(&column1, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&row1, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&column2, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&row2, 1, MPI_INT, 0, MPI_COMM_WORLD);

    int column1_local = column1 / squaredP;
    int row1_local = row1 / squaredP;
    int column2_local = column2 / squaredP;
    int row2_local = row2 / squaredP;
    int row3 = row1;
    int column3 = column2;

    // subarrays
    int **localA, **localB;
    int **col_message, **row_message, **col_receive, **row_receive, **localC;
    int row3_local = row1_local;
    int column3_local = column2_local;
    malloc2dint(&localA, column1_local, row1_local);
    malloc2dint(&localB, column2_local, row2_local);
    malloc2dint(&localC, column3_local, row3_local);
    malloc2dint(&col_message, column1_local, row1_local);
    malloc2dint(&row_message, column2_local, row2_local);
    malloc2dint(&col_receive, column1_local, row1_local);
    malloc2dint(&row_receive, column2_local, row2_local);

    int sizesA[2] = {row1, column1};
    int subsizesA[2] = {row1_local, column1_local};
    int startsA[2] = {0, 0};
    MPI_Datatype type, subarrtype;
    MPI_Type_create_subarray(2, sizesA, subsizesA, startsA, MPI_ORDER_C, MPI_INT, &type);
    MPI_Type_create_resized(type, 0, column1_local * sizeof(int), &subarrtype);
    MPI_Type_commit(&subarrtype);

    int *Aptr = NULL;
    int *Bptr = NULL;
    int *Cptr = NULL;
    if (my_rank == 0)
    {
        Aptr = &(A[0][0]);
        Bptr = &(B[0][0]);
        Cptr = &(C[0][0]);
    }

    int sendcounts[comm_size];
    int displs[comm_size];

    if (my_rank == 0)
    {
        for (int i = 0; i < comm_size; i++)
        {
            sendcounts[i] = 1;
        }
        int disp = 0;
        for (int i = 0; i < squaredP; i++)
        {
            for (int j = 0; j < squaredP; j++)
            {
                displs[i * squaredP + j] = disp;
                disp += 1;
            }
            disp += ((column1_local)-1) * squaredP;
        }
    }

    // initialize local C and receive
    for (int i = 0; i < row3_local; i++)
    {
        for (int j = 0; j < column3_local; j++)
        {
            localC[i][j] = 0;
        }
    }
    for (int i = 0; i < row1_local; i++)
    {
        for (int j = 0; j < column1_local; j++)
        {
            col_receive[i][j] = 0;
        }
    } 
    for (int i = 0; i < row2_local; i++)
    {
        for (int j = 0; j < column2_local; j++)
        {
            row_receive[i][j] = 0;
        }
    }
    
    //Communication phase, split matrix into P processors
    double start,end;
    MPI_Barrier(MPI_COMM_WORLD);
    start = MPI_Wtime();
    MPI_Scatterv(Aptr, sendcounts, displs, subarrtype, &(localA[0][0]), (row1_local) * (column1_local), MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Scatterv(Bptr, sendcounts, displs, subarrtype, &(localB[0][0]), (row2_local) * (column2_local), MPI_INT, 0, MPI_COMM_WORLD);

    col_message = localA;
    row_message = localB;

    /*for (int p=0; p<comm_size; p++) { 
        if (my_rank == p) {
            printf("Local process on rank %d is:\n", my_rank);
            for (int i=0; i<column1/squaredP; i++) {
                putchar('|');
                for (int j=0; j<column1/squaredP; j++) {
                    printf("%2d ", col_message[i][j]);
                }
                printf("|\n");
            }
        }
    }
    for (int p=0; p<comm_size; p++) {
        if (my_rank == p) {
            printf("Local process on rank %d is:\n", my_rank);
            for (int i=0; i<column1/squaredP; i++) {
                putchar('|');
                for (int j=0; j<column1/squaredP; j++) {
                    printf("%2d ", row_message[i][j]);
                }
                printf("|\n");
            }
        }
    }*/

    // SUMMA todo: fix broadcast
    int column_procs_sender = 0;
    int row_procs_sender;
    int col_receiver = 0;
    int row_receiver = 0;
    for (int k = 0; k <= (squaredP - 1); k++)
    {
        row_procs_sender = k;
        //if (my_rank == 0)
        //{printf("k:%d \n",k);}
        for (int i = 0; i <= (squaredP - 1); i++)
        {
            //if (my_rank == 0)
            //{printf("row sender:%d \n",row_procs_sender);}
            // broadcast submatrix A to processors in the same row
            for (int j = 0; j <= (squaredP - 1); j++)
            {
                //printf("sender:%d \n",row_procs_sender);
                //printf("receiver:%d \n",row_receiver);
                if (my_rank == row_procs_sender) //&& my_rank != row_receiver)
                {
                    if (row_procs_sender != row_receiver)
                    {
                        MPI_Send(&(col_message[0][0]), row1_local * column1_local, MPI_INT, row_receiver, 0, MPI_COMM_WORLD);
                        //printf("%d sent to %d for A\n", row_procs_sender, row_receiver);
                    }
                    else // receive my own message
                    {
                        col_receive = col_message;
                    }
                }
                else if (my_rank == row_receiver) //&& my_rank != row_procs_sender)
                {
                    if (row_procs_sender != row_receiver)
                    {
                        MPI_Recv(&(col_receive[0][0]), row1_local * column1_local, MPI_INT, row_procs_sender, 0, MPI_COMM_WORLD, &status);

                    }
                }
                row_receiver++;
            }
            row_procs_sender += squaredP;
        }
        for (int j = 0; j <= (squaredP - 1); j++)
        {
            //if (my_rank == 0)
            //{printf("column sender %d\n",column_procs_sender);}
            // broadcast submatrix B to processors in the same column
            for (int l = 0; l <= (squaredP - 1); l++)
            {
                //if (my_rank == 0)
                //{printf("column receiver %d\n",col_receiver);}
                if (my_rank == column_procs_sender)
                {
                    if (column_procs_sender != col_receiver)
                    {
                        MPI_Send(&(row_message[0][0]), row2_local * column2_local, MPI_INT, col_receiver, 0, MPI_COMM_WORLD);
                        //printf("%d sent to %d for B\n", column_procs_sender, col_receiver);
                    }
                    else //receive my own message
                    {
                        row_receive = row_message;
                    }
                }
                else if (my_rank == col_receiver )
                {
                    if (column_procs_sender != col_receiver)
                    {
                        MPI_Recv(&(row_receive[0][0]), row2_local * column2_local, MPI_INT, column_procs_sender, 0, MPI_COMM_WORLD, &status);
                    }
                    
                }
                col_receiver += squaredP;
            }
            column_procs_sender++;
            col_receiver = (col_receiver + 1) % squaredP;
            // printf("%d/n",col_receiver);
        }
        // calculate local submatrix C'
        for (int i = 0; i < row3_local; i++)
        {
            for (int k = 0; k < column3_local; k++)
            {
                for (int j = 0; j < column3_local; j++)
                {
                    localC[i][j] += col_receive[i][k] * row_receive[k][j];
                }
            }
        }
        col_receiver = 0;
        row_receiver = 0;
    }

    // Merge submatrix C' to C
    MPI_Gatherv(&(localC[0][0]), row3_local * column3_local, MPI_INT,
                Cptr, sendcounts, displs, subarrtype, 0, MPI_COMM_WORLD);
    
    end = MPI_Wtime();

    /*if (my_rank == 0)
    {
        printf("Global c\n");
        for (int i = 0; i < row3; i++)
        {
            for (int j = 0; j < column3; j++)
            {
                printf("%2d\t", C[i][j]);
            }
            printf("\n");
        }
    }*/

    if (my_rank == 0)
    {
        optr = fopen(argv[2], "w");

        if (optr == NULL)
        {
            printf("Error!");
            exit(1);
        }
        for (int i = 0; i < row3; i++)
        {
            for (int j = 0; j < column3; j++)
            {
                fprintf(optr, "%d\t", C[i][j]);
            }
            fprintf(optr, "\n");
        }
        fprintf(optr, "Running time: %es\n", end - start);
        fclose(iptr);
        fclose(optr);
        free2dint(&A);
        free2dint(&B);
        free2dint(&C);
    }

    free2dint(&localA);
    free2dint(&localB);
    free2dint(&localC);
    //free2dint(&col_message);
    //free2dint(&row_message);
    //free2dint(&col_receive);
    //free2dint(&row_receive);
    //int e = 0;
    //printf("%d",(3%4));
    MPI_Finalize();
    return 0;
}

/*for (int i = 0; i < row3 * column3; i++)
{
    C[i] = 0;
}

for (int i = 0; i < row3; i++)
{
    for (int k = 0; k < column3; k++)
    {
        for (int j = 0; j < column1; j++)
        {
            C[i * column3 + j] += A[i * column1 + k] * B[k * column2 + j];
        }
    }
}*/

// printf("%d,%d,%d,%d\n",column1,row1,column2,row2);

/*for (int p=0; p<comm_size; p++) {
    if (my_rank == p) {
        printf("Local process on rank %d is:\n", my_rank);
        for (int i=0; i<column1/squaredP; i++) {
            putchar('|');
            for (int j=0; j<column1/squaredP; j++) {
                printf("%2d ", localA[i][j]);
            }
            printf("|\n");
        }
    }
}
for (int p=0; p<comm_size; p++) {
    if (my_rank == p) {
        printf("Local process on rank %d is:\n", my_rank);
        for (int i=0; i<column1/squaredP; i++) {
            putchar('|');
            for (int j=0; j<column1/squaredP; j++) {
                printf("%2d ", localB[i][j]);
            }
            printf("|\n");
        }
    }
}*/

/*int row3 = row1;
int column3 = column2;
int *C = malloc(column3 * row3 * sizeof(int));*/

/*if (my_rank == 0)
{
    optr = fopen(argv[2], "w");

    if (optr == NULL)
    {
        printf("Error!");
        exit(1);
    }
    for (int i = 0; i < row3; i++)
    {
        for (int j = 0; j < column3; j++)
        {
            fprintf(optr, "%d\t", C[i * column3 + j]);
        }
        fprintf(optr, "\n");
    }
}*/
// fprintf(optr, "Running time: %es\n", end - start);