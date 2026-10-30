/* Same fixed work as sum.nx and sum_openmp.c: sum 1..8, result 36.
   mpicc examples/flow/sum_mpi.c -o sum-mpi
   mpiexec -n 2 ./sum-mpi */
#include <mpi.h>
#include <stdio.h>

int main(int argc, char **argv) {
  MPI_Init(&argc, &argv);
  int rank, size;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  int local = 0;
  for (int i = rank + 1; i <= 8; i += size) {
    local += i;
  }
  int total = 0;
  MPI_Reduce(&local, &total, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
  int failed = rank == 0 && total != 36;
  if (rank == 0) printf("sum=%d\n", total);
  MPI_Bcast(&failed, 1, MPI_INT, 0, MPI_COMM_WORLD);
  MPI_Finalize();
  return failed;
}
