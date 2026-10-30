/* Build: mpicc examples/basics/parallel/hello_mpi.c -o hello-mpi
   Run: mpiexec -n 2 ./hello-mpi   Expected: rank 0 received 42 from rank 1. */
#include <mpi.h>
#include <stdio.h>

int main(int argc, char **argv) {
  MPI_Init(&argc, &argv);
  int rank, size;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  if (size != 2) {
    if (rank == 0) fprintf(stderr, "run with exactly 2 ranks\n");
    MPI_Finalize();
    return 1;
  }
  int value = 42;
  if (rank == 1) {
    MPI_Send(&value, 1, MPI_INT, 0, 0, MPI_COMM_WORLD);
  } else {
    MPI_Recv(&value, 1, MPI_INT, 1, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    printf("rank 0 received %d from rank 1\n", value);
  }
  MPI_Finalize();
  return value == 42 ? 0 : 1;
}
