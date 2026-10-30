# ALU result immediately used: forwarding, no load-use stall. Exit: 10.
main:
  addiu $t0, $zero, 5
  addu  $v0, $t0, $t0
  jr    $ra
