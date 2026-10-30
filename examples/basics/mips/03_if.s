# if (x < 5) return 1; else return 0;  Exit value: 1.
main:
  addiu $t0, $zero, 3      # x (with x = 7 the other path would be taken)
  addiu $t1, $zero, 5
  slt   $t2, $t0, $t1
  beq   $t2, $zero, otherwise
  addiu $v0, $zero, 1
  jr    $ra
otherwise:
  addiu $v0, $zero, 0
  jr    $ra
