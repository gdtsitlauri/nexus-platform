# Block reduction in shared memory (run with --block 16): every thread writes gid+1 to shared[idx], then the
# block halves the active range with __syncthreads() between steps; every thread returns the block's sum.
main:
kernel:
  addiu $t0, $a0, 1          # value = global id + 1
  lui   $t8, 4
  ori   $t8, $t8, 16384      # shared memory base 0x44000
  sll   $t1, $a3, 2
  addu  $t2, $t8, $t1        # &shared[idx]
  sw    $t0, 0($t2)
  lui   $t9, 4
  ori   $t9, $t9, 32768      # barrier address 0x48000
  sw    $zero, 0($t9)        # __syncthreads()
  addiu $t3, $zero, 8        # s = blockDim / 2
  addiu $s0, $zero, 2
loop:
  slt   $t4, $a3, $t3        # idx < s ?
  beq   $t4, $zero, skip
  sll   $t5, $t3, 2
  addu  $t6, $t2, $t5        # &shared[idx + s]
  lw    $t7, 0($t6)
  lw    $t0, 0($t2)
  addu  $t0, $t0, $t7
  sw    $t0, 0($t2)
skip:
  sw    $zero, 0($t9)        # __syncthreads()
  div   $t3, $s0
  mflo  $t3                  # s = s / 2
  bne   $t3, $zero, loop
  lw    $v0, 0($t8)          # shared[0] holds the block sum
  jr    $ra
