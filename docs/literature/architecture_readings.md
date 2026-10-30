# Architecture readings (NEY221, EY321, NEY606)

Classic papers behind the topics of the architecture guides. Read the paper, then run the matching example.

| paper | topic | in NEXUS |
| --- | --- | --- |
| J. von Neumann, *First Draft of a Report on the EDVAC*, 1945 | the stored-program computer | [EY321 §0](../courses/EY321.md#0-a-short-history) |
| M. V. Wilkes, *The Best Way to Design an Automatic Calculating Machine*, 1951 | microprogrammed control | [EY321 §3](../courses/EY321.md#3-the-multi-cycle-cpu-hardwired-and-microprogrammed-control) |
| D. A. Patterson, D. R. Ditzel, *The Case for the Reduced Instruction Set Computer*, Computer Architecture News, 1980 | RISC, load/store | [NEY221 §3](../courses/NEY221.md#3-instruction-set-styles-accumulator-stack-register-memory-loadstore) |
| G. M. Amdahl, *Validity of the Single Processor Approach to Achieving Large Scale Computing Capabilities*, AFIPS, 1967 | Amdahl's law | [NEY606 §1](../courses/NEY606.md#1-measuring-performance-and-amdahls-law) |
| R. M. Tomasulo, *An Efficient Algorithm for Exploiting Multiple Arithmetic Units*, IBM J. R&D, 1967 | dynamic scheduling, renaming | [NEY606 §3](../courses/NEY606.md#3-dynamic-scheduling-scoreboard-and-tomasulo) |
| J. E. Smith, *A Study of Branch Prediction Strategies*, ISCA, 1981 | 2-bit counters | [NEY606 §4](../courses/NEY606.md#4-branch-prediction-and-speculation) |
| J. A. Fisher, *Very Long Instruction Word Architectures and the ELI-512*, ISCA, 1983 | VLIW | [NEY606 §5](../courses/NEY606.md#5-static-scheduling-vliw-and-software-pipelining) |
| B. R. Rau, *Iterative Modulo Scheduling*, MICRO, 1994 | software pipelining | [NEY606 §5](../courses/NEY606.md#5-static-scheduling-vliw-and-software-pipelining) |
| D. Kroft, *Lockup-Free Instruction Fetch/Prefetch Cache Organization*, ISCA, 1981 | non-blocking caches, MSHRs | [NEY606 §6](../courses/NEY606.md#6-advanced-memory-non-blocking-caches-and-prefetching) |
| A. J. Smith, *Cache Memories*, ACM Computing Surveys, 1982 | caches | [EY321 §5](../courses/EY321.md#5-memory-hierarchy-caches) |
| P. J. Denning, *Virtual Memory*, ACM Computing Surveys, 1970 | paging, replacement | [EY321 §6](../courses/EY321.md#6-virtual-memory) |

Books: Patterson, Hennessy, *Computer Organization and Design* (NEY221, EY321); Hennessy, Patterson,
*Computer Architecture: A Quantitative Approach* (NEY606).
