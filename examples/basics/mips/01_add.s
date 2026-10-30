# Two inputs and one result. Exit value: 12 (5 + 7).
main:
  addiu $t0, $zero, 5
  addiu $t1, $zero, 7
  addu  $v0, $t0, $t1
  jr    $ra
