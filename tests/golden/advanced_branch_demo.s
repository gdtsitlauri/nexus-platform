main:
  addiu $t0, $zero, 0
  addiu $t1, $zero, 4
loop:
  addiu $t0, $t0, 1
  bne $t0, $t1, loop
  addu $v0, $t0, $zero
  jr $ra
