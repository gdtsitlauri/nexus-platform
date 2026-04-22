#include <cstdlib>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include <mpi.h>

namespace {

struct Options {
  std::string kernel = "all";
  int size = 8;
};

Options parse_options(int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "--kernel" && index + 1 < argc) {
      options.kernel = argv[++index];
    } else if (arg == "--size" && index + 1 < argc) {
      options.size = std::max(1, std::atoi(argv[++index]));
    } else {
      throw std::runtime_error("unknown argument: " + arg);
    }
  }
  return options;
}

void emit_result(const std::string& kernel, int ranks, int size, long long checksum) {
  std::cout << "benchmark=mpi"
            << " kernel=" << kernel
            << " ranks=" << ranks
            << " size=" << size
            << " checksum=" << checksum
            << " status=ok\n";
}

long long reduction_kernel(int rank, int ranks, int size) {
  long long local = 0;
  for (int index = 0; index < size; ++index) {
    local += static_cast<long long>((rank + 1) * (index + 1));
  }
  long long global = 0;
  MPI_Reduce(&local, &global, 1, MPI_LONG_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
  return global + ranks;
}

long long ring_kernel(int rank, int ranks, int size) {
  int token = rank + size;
  const int next = (rank + 1) % ranks;
  const int previous = (rank + ranks - 1) % ranks;
  MPI_Sendrecv_replace(&token, 1, MPI_INT, next, 0, previous, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
  long long reduced = 0;
  const long long contribution = static_cast<long long>(token);
  MPI_Reduce(&contribution, &reduced, 1, MPI_LONG_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
  return reduced;
}

}  // namespace

int main(int argc, char** argv) {
  MPI_Init(&argc, &argv);

  int rank = 0;
  int ranks = 0;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &ranks);

  int exit_code = 0;
  try {
    const Options options = parse_options(argc, argv);
    if (options.kernel == "reduce" || options.kernel == "all") {
      const long long checksum = reduction_kernel(rank, ranks, options.size);
      if (rank == 0) {
        emit_result("reduce", ranks, options.size, checksum);
      }
    }
    if (options.kernel == "ring" || options.kernel == "all") {
      const long long checksum = ring_kernel(rank, ranks, options.size);
      if (rank == 0) {
        emit_result("ring", ranks, options.size, checksum);
      }
    }
  } catch (const std::exception& error) {
    if (rank == 0) {
      std::cerr << "error: " << error.what() << '\n';
    }
    exit_code = 1;
  }

  MPI_Finalize();
  return exit_code;
}
