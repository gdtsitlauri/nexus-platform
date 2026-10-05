# SPMD reduction for any number of cores: core k sums 8k .. 8k+7, adds its partial sum to a
# shared total under the hardware lock, waits at the barrier, and core 0 returns the total
# 0 + 1 + ... + (8*cores - 1).   mips-sim run spmd_sum.s --mode parallel --cores 16 --interconnect mesh
main:
  j worker
worker:
  sll $t0, $a0, 3
  addiu $t1, $t0, 8
  addiu $t2, $zero, 0
loop:
  addu $t2, $t2, $t0
  addiu $t0, $t0, 1
  bne $t0, $t1, loop
  lui $t7, 2
  sw $zero, 0($t7)
  lw $t3, 256($zero)
  addu $t3, $t3, $t2
  sw $t3, 256($zero)
  sw $zero, 4($t7)
  sw $zero, 8($t7)
  addiu $v0, $t2, 0
  bne $a0, $zero, done
  lw $v0, 256($zero)
done:
  jr $ra
