main:
  lui $t3, 1
  addiu $t0, $zero, 3
  sw $t0, 256($t3)
  addiu $t0, $zero, 1
  sw $t0, 260($t3)
  addiu $t1, $zero, 0
loop:
  beq $t1, $zero, loop
  addu $v0, $t1, $zero
  jr $ra
interrupt_handler:
  addiu $t1, $zero, 77
  jr $ra
