# Long multiplications with independent work beside them (NEY606).  An in-order core waits behind every
# mult; Tomasulo issues the independent additions around it, so the loop takes fewer cycles.
main:
  addiu $t0, $zero, 0      # i
  addiu $t1, $zero, 50     # trip count
  addiu $t2, $zero, 3
  addiu $v0, $zero, 0
loop:
  mult  $t2, $t2           # long latency
  mflo  $t3                # waits for the product
  addu  $v0, $v0, $t3      # depends on it
  addiu $t4, $t0, 7        # independent work ...
  xor   $t5, $t4, $t0
  addu  $t6, $t5, $t4
  addiu $t0, $t0, 1
  bne   $t0, $t1, loop
  xori  $v0, $v0, 0
  jr    $ra
