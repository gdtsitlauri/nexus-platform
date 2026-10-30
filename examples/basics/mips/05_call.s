# A leaf function needs no frame; its caller saves its own return address.
# Exit value: 9. In this tiny caller, s0 is reserved for the saved address.
main:
  addu  $s0, $ra, $zero
  addiu $a0, $zero, 8
  jal   increment
  addu  $ra, $s0, $zero
  jr    $ra
increment:
  addiu $v0, $a0, 1
  jr    $ra
