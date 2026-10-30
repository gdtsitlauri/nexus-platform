# Same fixed work as sum.nx: sum 1..8, for 1, 2, 4 or 8 cores. Result: 36.
# NEXUS supplies a0=worker ID, a1=worker count at the 'worker' entry.
# Core k owns k+1, k+1+P, ... <= 8. Lock protects the shared result at address 256.
main:
  j worker
worker:
  addiu $t0, $a0, 1
  addiu $t1, $zero, 9
  addiu $t2, $zero, 0
loop:
  slt   $t3, $t0, $t1
  beq   $t3, $zero, combine
  addu  $t2, $t2, $t0
  addu  $t0, $t0, $a1
  j loop
combine:
  lui   $t7, 2              # 0x20000: acquire, release, barrier at offsets 0,4,8
  sw    $zero, 0($t7)
  lw    $t3, 256($zero)
  addu  $t3, $t3, $t2
  sw    $t3, 256($zero)
  sw    $zero, 4($t7)
  sw    $zero, 8($t7)
  addu  $v0, $t2, $zero
  bne   $a0, $zero, done
  lw    $v0, 256($zero)      # core 0 returns the agreed total after the barrier
done:
  jr    $ra
