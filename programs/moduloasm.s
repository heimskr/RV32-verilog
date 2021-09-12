addi sp, sp, -32
sw   s0, 28(sp)
addi s0, sp, 32
lui  a5, 65536
addi a5, a5, -174
sw   a5, -20(s0)
lw   a4, -20(s0)
addi a5, zero, 10
rem a6, a4, a5
sw a6, -24(s0)
mv a4, zero
lw a5, -24(s0)
sw a5, 0(a4)
j 0
