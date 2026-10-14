# A preemptive round-robin kernel for the NEXUS CPU (pipeline model with --io-demo --interrupt-demo, and the SoC).
# Three user tasks need 2, 4 and 3 time slices (the same jobs as examples/os/rr_kernel.jobs). The timer interrupt
# saves the running task's context (pc, t0, t1) in its process control block, charges it one slice, and dispatches
# the next task in round-robin order that still needs time. Every dispatch writes the task number to the UART, so
# the UART shows the kernel's Gantt chart; nexus-os computes the same chart from the job file.
# Exit code: number of slices (9) if every task made progress, 0 otherwise.
# Memory map (bytes): 2000 current task, 2004 slice count, PCB i at 2008 + 16 i: pc, t0, t1, slices left;
# 2056 address of the finish routine. Kernel registers: $t2-$t9 (the tasks use only $t0 and $t1).
main:
  addu $s7, $ra, $zero        # keep the exit address
  jal get_task                # the jal trick yields instruction addresses: the slot after jal is a trampoline
  j task
get_task:
  sw $ra, 2008($zero)         # all tasks run the same code, each with its own t0/t1
  sw $ra, 2024($zero)
  sw $ra, 2040($zero)
  jal get_finish
  j finish
get_finish:
  sw $ra, 2056($zero)
  sw $zero, 2012($zero)
  sw $zero, 2016($zero)
  sw $zero, 2028($zero)
  sw $zero, 2032($zero)
  sw $zero, 2044($zero)
  sw $zero, 2048($zero)
  addiu $t5, $zero, 2         # slices needed by task 0, 1, 2
  sw $t5, 2020($zero)
  addiu $t5, $zero, 4
  sw $t5, 2036($zero)
  addiu $t5, $zero, 3
  sw $t5, 2052($zero)
  sw $zero, 2000($zero)       # current = task 0
  sw $zero, 2004($zero)       # slices = 0
  lui $t8, 1                  # UART: report the first dispatch ('0')
wait0:
  lw $t6, 4($t8)
  beq $t6, $zero, wait0
  addiu $t9, $zero, 48
  sw $t9, 0($t8)
  addiu $t6, $zero, 40        # timer: interrupt after 40 cycles
  sw $t6, 256($t8)
  addiu $t6, $zero, 1
  sw $t6, 260($t8)
  addiu $t0, $zero, 0
  addiu $t1, $zero, 0
  j task
task:
  addiu $t0, $t0, 1           # the task's work: count
  j task
interrupt_handler:
  lw $t2, 2000($zero)         # c = current task
  addiu $t3, $zero, 16
  mult $t2, $t3
  mflo $t4                    # PCB offset of c
  sw $ra, 2008($t4)           # save the context (the interrupted pc is in $ra)
  sw $t0, 2012($t4)
  sw $t1, 2016($t4)
  lw $t5, 2020($t4)           # charge one slice
  addiu $t5, $t5, -1
  sw $t5, 2020($t4)
  lw $t6, 2004($zero)
  addiu $t6, $t6, 1
  sw $t6, 2004($zero)
  addiu $t7, $zero, 3         # look at most at 3 tasks, starting after c
next:
  addiu $t2, $t2, 1
  addiu $t8, $zero, 3
  bne $t2, $t8, nowrap
  addiu $t2, $zero, 0
nowrap:
  mult $t2, $t3
  mflo $t4
  lw $t5, 2020($t4)
  bne $t5, $zero, dispatch
  addiu $t7, $t7, -1
  bne $t7, $zero, next
  lw $ra, 2056($zero)         # every task is done
  jr $ra
dispatch:
  sw $t2, 2000($zero)
  lui $t8, 1
wait:
  lw $t6, 4($t8)              # UART: report the dispatched task
  beq $t6, $zero, wait
  addiu $t9, $t2, 48
  sw $t9, 0($t8)
  lw $ra, 2008($t4)           # restore its context
  lw $t0, 2012($t4)
  lw $t1, 2016($t4)
  addiu $t6, $zero, 40        # re-arm the timer for the next slice
  sw $t6, 256($t8)
  addiu $t6, $zero, 1
  sw $t6, 260($t8)
  jr $ra                      # return from the interrupt into the dispatched task
finish:
  addu $ra, $s7, $zero
  addiu $v0, $zero, 0
  lw $t0, 2012($zero)         # every task must have counted
  beq $t0, $zero, done
  lw $t0, 2028($zero)
  beq $t0, $zero, done
  lw $t0, 2044($zero)
  beq $t0, $zero, done
  lw $v0, 2004($zero)
done:
  jr $ra
