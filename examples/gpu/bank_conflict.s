# Shared-memory bank conflicts: lane idx writes word idx * stride, stride in $s1 (1: conflict free; 32: every
# lane of a warp hits the same bank).
main:
kernel:
  lui   $t8, 4
  ori   $t8, $t8, 16384
  addiu $s1, $zero, 32
  mult  $a3, $s1
  mflo  $t1
  sll   $t1, $t1, 2
  addu  $t2, $t8, $t1
  sw    $a0, 0($t2)
  addu  $v0, $zero, $a0
  jr    $ra
