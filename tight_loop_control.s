.equ N, 4096 # 4 KiB
.text
    li t0, 0x20000000 # current address
    li t1, N # count
    li t2, 1 # writing data
loop:
    beqz t1, end
    sw t2, 0(t0)
    addi t0, t0, 4
    addi t1, t1, -4
    j loop
end:
    li a0, 0 # return 0
    li a7, 93
    ecall

# 213152 KiB
# 213344 KiB
# 213100 KiB

# 399756 KiB
# 399660 KiB
# 399776 KiB
# 399731
