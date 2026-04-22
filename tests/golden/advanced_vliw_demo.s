main:
  addiu $t0, $zero, 1
  addu $t1, $t0, $t0
  addiu $t2, $zero, 2
  addu $t3, $t2, $t2
  addu $v0, $t1, $t3
  jr $ra
