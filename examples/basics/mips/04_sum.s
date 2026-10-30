# sum = 0; for (i = 1; i != 5; ++i) sum += i;  Exit value: 10.
main:
  addiu $t0, $zero, 1
  addiu $t1, $zero, 5      # exclusive upper bound
  addiu $v0, $zero, 0
loop:
  addu  $v0, $v0, $t0
  addiu $t0, $t0, 1
  bne   $t0, $t1, loop
  jr    $ra
