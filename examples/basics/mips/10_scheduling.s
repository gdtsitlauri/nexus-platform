# mflo waits for multiplication, but the addiu after it is independent.
# Exit: 16. Compare scoreboard and Tomasulo traces, not just total cycles.
main:
  addiu $t0, $zero, 3
  mult  $t0, $t0
  mflo  $t1
  addiu $t2, $zero, 7
  addu  $v0, $t1, $t2
  jr    $ra
