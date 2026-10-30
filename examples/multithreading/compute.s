# Compute-bound thread: 200 iterations of ALU work, no memory accesses.
main:
  addiu $t1, $zero, 0
  addiu $t2, $zero, 200
  addiu $v0, $zero, 1
loop:
  addu  $v0, $v0, $t1
  xor   $t3, $v0, $t1
  sll   $t3, $t3, 1
  addiu $t1, $t1, 1
  bne   $t1, $t2, loop
  xori  $v0, $v0, 255
  jr    $ra
