# minirubik on RV32I: optimal 2x2x2 solver by IDA* with two pattern databases
# Ripes' assembler has no .if, so this file goes through cpp first:
#   gcc -E -P -x assembler-with-cpp -DRENDER=0 cube.S > cube-cli.s
# RENDER=1 adds the LED matrix renderer and nothing else.
# Exit code is the number of failed cases, 0 means every case passed.

#ifndef RENDER
#define RENDER 0
#endif

        .data
#include "pdb.s"

# one case is 16 bytes: 14 digits, NUL, expected length (255 = unknown)
cases:
#ifdef STATE
        .string STATE
#ifdef EXPECT
        .byte   EXPECT
#else
        .byte   255
#endif
#else
        .string "12345671111111"    # solved
        .byte   0
        .string "23165742131312"    # R B2 D
        .byte   3
        .string "21345671111111"    # distance 11
        .byte   11
#endif
        .byte   0                   # end of cases

# move m turns face m / 3 by m % 3 + 1 quarter turns, 4 bytes per name
names:
        .byte   82, 32, 0, 0        # "R "
        .byte   82, 50, 32, 0       # "R2 "
        .byte   82, 39, 32, 0       # "R' "
        .byte   66, 32, 0, 0        # "B "
        .byte   66, 50, 32, 0       # "B2 "
        .byte   66, 39, 32, 0       # "B' "
        .byte   68, 32, 0, 0        # "D "
        .byte   68, 50, 32, 0       # "D2 "
        .byte   68, 39, 32, 0       # "D' "
colon:  .byte   58, 32, 0, 0        # ": "
newline:
        .byte   10, 0, 0, 0
failmsg:
        .byte   33, 33, 10, 0       # "!!\n"

# same move model as solver.c: position i takes the cubie at source[f][i]
# and adds twist[f][i] to its orientation
source: .byte   1, 4, 2, 0, 3, 5, 6, 0
        .byte   0, 1, 2, 4, 5, 6, 3, 0
        .byte   0, 2, 5, 3, 1, 4, 6, 0
twist:  .byte   1, 2, 0, 2, 1, 0, 0, 0
        .byte   0, 0, 0, 1, 2, 1, 2, 0
        .byte   0, 0, 0, 0, 0, 0, 0, 0

# (6 - i)! * 4 and 3^(5 - i) * 4, so ranks come out as byte offsets
fact4:  .word   2880, 480, 96, 24, 8, 4, 0
pow4:   .word   972, 324, 108, 36, 12, 4

cube_p: .zero   8                   # cubie at each position
cube_o: .zero   8                   # its orientation
tmp:    .zero   8
moves:  .zero   12                  # solution as move codes
frames: .zero   192                 # saved levels, 16 bytes each
# where each of the nine moves resumes, indexed by move code; Ripes cannot
# put a text label in .word, so solve fills it in
resume: .zero   36

        .text
main:
        la      s0, cases
        li      tp, 0               # failed cases, solve uses every s register
next_case:
        lbu     t0, 0(s0)
        beqz    t0, done
        jal     ra, parse           # a0 = p * 4, a1 = o * 4, -1 if invalid
        bltz    a0, case_failed
        jal     ra, solve           # a0 = length, -1 if none
        bltz    a0, case_failed
        mv      s1, a0
        lbu     t0, 15(s0)          # expected length
        li      t1, 255
        beq     t0, t1, length_ok
        bne     t0, s1, case_failed
length_ok:
        jal     ra, print_case
        jal     ra, replay          # a0 = 0 if the path reaches solved
        bnez    a0, case_failed
        j       case_done
case_failed:
        addi    tp, tp, 1
        la      a0, failmsg
        li      a7, 4
        ecall
case_done:
        addi    s0, s0, 16
        j       next_case
done:
        mv      a0, tp
        li      a7, 93
        ecall

# parse the case at s0 into cube_p/cube_o and return the ranks as byte
# offsets: a0 = p * 4, a1 = o * 4, or a0 = -1 if the state is invalid
parse:
        la      t0, cube_p
        la      t1, cube_o
        li      t2, 0               # i
        li      t3, 0               # seen cubies, one bit each
        li      t4, 0               # orientation sum
        li      t6, 7
parse_loop:
        add     a2, s0, t2
        lbu     a3, 0(a2)           # cubie digit
        lbu     a4, 7(a2)           # orientation digit
        addi    a3, a3, -49
        addi    a4, a4, -49
        bgeu    a3, t6, invalid     # unsigned compare also catches < '1'
        li      a5, 3
        bgeu    a4, a5, invalid
        li      a5, 1
        sll     a5, a5, a3
        and     a6, t3, a5
        bnez    a6, invalid         # cubie seen twice
        or      t3, t3, a5
        add     t4, t4, a4
        add     a2, t0, t2
        sb      a3, 0(a2)
        add     a2, t1, t2
        sb      a4, 0(a2)
        addi    t2, t2, 1
        bne     t2, t6, parse_loop
        lbu     a3, 14(s0)
        bnez    a3, invalid         # longer than 14 digits
mod3:
        addi    t4, t4, -3          # sum <= 14, subtract until negative
        bgez    t4, mod3
        addi    t4, t4, 3
        bnez    t4, invalid
        # Lehmer rank: add (6 - i)! * 4 for every later cubie that is smaller
        li      a0, 0
        la      a5, fact4
        li      t2, 0
rank_i:
        add     a2, t0, t2
        lbu     a3, 0(a2)
        lw      a6, 0(a5)
        addi    t5, t2, 1
rank_j:
        beq     t5, t6, rank_next
        add     a2, t0, t5
        lbu     a4, 0(a2)
        bgeu    a4, a3, rank_skip
        add     a0, a0, a6
rank_skip:
        addi    t5, t5, 1
        j       rank_j
rank_next:
        addi    a5, a5, 4
        addi    t2, t2, 1
        bne     t2, t6, rank_i
        # base-3 rank of the first six orientations, digit 2 adds twice
        li      a1, 0
        la      a5, pow4
        li      t2, 0
orient_rank:
        add     a2, t1, t2
        lbu     a3, 0(a2)
        lw      a6, 0(a5)
orient_add:
        beqz    a3, orient_next
        add     a1, a1, a6
        addi    a3, a3, -1
        j       orient_add
orient_next:
        addi    a5, a5, 4
        addi    t2, t2, 1
        addi    a3, t2, -6
        bnez    a3, orient_rank
        ret
invalid:
        li      a0, -1
        ret

# IDA*: a0 = p * 4, a1 = o * 4 -> a0 = length and moves filled, or -1
solve:
        or      t0, a0, a1
        bnez    t0, unsolved
        li      a0, 0
        ret
unsolved:
        # bound starts at h(root) = max of the two pattern databases
        srli    t0, a0, 2
        la      t1, pdb_perm
        add     t1, t1, t0
        lbu     a2, 0(t1)
        srli    t0, a1, 2
        la      t1, pdb_orient
        add     t1, t1, t0
        lbu     t0, 0(t1)
        bge     a2, t0, bound_set
        mv      a2, t0
bound_set:
        # one base register per face and table, so every turn is fixed code
        la      s3, move_perm
        li      t0, 20160           # one face of move_perm in bytes
        add     s4, s3, t0
        add     s10, s4, t0
        la      s11, move_orient
        li      t0, 2916            # one face of move_orient
        add     a4, s11, t0
        add     a5, a4, t0
        la      t1, resume
        la      t0, f0t2
        sw      t0, 0(t1)
        la      t0, f0t3
        sw      t0, 4(t1)
        la      t0, f0end
        sw      t0, 8(t1)
        la      t0, f1t2
        sw      t0, 12(t1)
        la      t0, f1t3
        sw      t0, 16(t1)
        la      t0, f1end
        sw      t0, 20(t1)
        la      t0, f2t2
        sw      t0, 24(t1)
        la      t0, f2t3
        sw      t0, 28(t1)
        la      t0, f2end
        sw      t0, 32(t1)
        li      t4, 1
        li      t5, 2
        la      a3, frames
iteration:
        # one depth-first pass, root at depth 0 with no previous face
        mv      s8, a0
        mv      s9, a1
        li      s5, 3
        addi    s6, a2, -1          # moves left after a child of the root
        mv      s7, a3
        j       node
descend0:
        li      a6, 0
        j       descend
descend1:
        li      a6, 1
        j       descend
descend2:
        li      a6, 2
descend:
        beqz    s6, found           # h = 0 only at solved
        # save this level, 16 bytes, the child becomes the node
        sw      t6, 0(s7)
        sw      s8, 4(s7)
        sw      s9, 8(s7)
        sw      s5, 12(s7)
        addi    s7, s7, 16
        mv      s8, s1
        mv      s9, s2
        mv      s5, a6
        addi    s6, s6, -1
node:
        # never turn the same face twice: skip the face in s5
        beqz    s5, face1
face0:
        mv      s1, s8
        mv      s2, s9
f0t1:
        add     t0, s3, s1
        lhu     s1, 0(t0)           # next permutation offset
        lbu     t1, 2(t0)           # its pattern database value
        add     t2, s11, s2
        lhu     s2, 0(t2)
        lbu     t3, 2(t2)
        blt     s6, t1, f0t2       # h > moves left
        blt     s6, t3, f0t2
        jal     t6, descend0      # t6 is where this level resumes
f0t2:
        add     t0, s3, s1
        lhu     s1, 0(t0)           # next permutation offset
        lbu     t1, 2(t0)           # its pattern database value
        add     t2, s11, s2
        lhu     s2, 0(t2)
        lbu     t3, 2(t2)
        blt     s6, t1, f0t3       # h > moves left
        blt     s6, t3, f0t3
        jal     t6, descend0      # t6 is where this level resumes
f0t3:
        # last turn of the face: nothing reuses the ranks if it is pruned,
        # so test first and load them only for a child that is entered
        add     t0, s3, s1
        lbu     t1, 2(t0)
        blt     s6, t1, f0end       # h > moves left
        add     t2, s11, s2
        lbu     t3, 2(t2)
        blt     s6, t3, f0end
        lhu     s1, 0(t0)
        lhu     s2, 0(t2)
        jal     t6, descend0      # t6 is where this level resumes
f0end:
        beq     s5, t4, face2
face1:
        mv      s1, s8
        mv      s2, s9
f1t1:
        add     t0, s4, s1
        lhu     s1, 0(t0)           # next permutation offset
        lbu     t1, 2(t0)           # its pattern database value
        add     t2, a4, s2
        lhu     s2, 0(t2)
        lbu     t3, 2(t2)
        blt     s6, t1, f1t2       # h > moves left
        blt     s6, t3, f1t2
        jal     t6, descend1      # t6 is where this level resumes
f1t2:
        add     t0, s4, s1
        lhu     s1, 0(t0)           # next permutation offset
        lbu     t1, 2(t0)           # its pattern database value
        add     t2, a4, s2
        lhu     s2, 0(t2)
        lbu     t3, 2(t2)
        blt     s6, t1, f1t3       # h > moves left
        blt     s6, t3, f1t3
        jal     t6, descend1      # t6 is where this level resumes
f1t3:
        # last turn of the face: nothing reuses the ranks if it is pruned,
        # so test first and load them only for a child that is entered
        add     t0, s4, s1
        lbu     t1, 2(t0)
        blt     s6, t1, f1end       # h > moves left
        add     t2, a4, s2
        lbu     t3, 2(t2)
        blt     s6, t3, f1end
        lhu     s1, 0(t0)
        lhu     s2, 0(t2)
        jal     t6, descend1      # t6 is where this level resumes
f1end:
        beq     s5, t5, ascend
face2:
        mv      s1, s8
        mv      s2, s9
f2t1:
        add     t0, s10, s1
        lhu     s1, 0(t0)           # next permutation offset
        lbu     t1, 2(t0)           # its pattern database value
        add     t2, a5, s2
        lhu     s2, 0(t2)
        lbu     t3, 2(t2)
        blt     s6, t1, f2t2       # h > moves left
        blt     s6, t3, f2t2
        jal     t6, descend2      # t6 is where this level resumes
f2t2:
        add     t0, s10, s1
        lhu     s1, 0(t0)           # next permutation offset
        lbu     t1, 2(t0)           # its pattern database value
        add     t2, a5, s2
        lhu     s2, 0(t2)
        lbu     t3, 2(t2)
        blt     s6, t1, f2t3       # h > moves left
        blt     s6, t3, f2t3
        jal     t6, descend2      # t6 is where this level resumes
f2t3:
        # last turn of the face: nothing reuses the ranks if it is pruned,
        # so test first and load them only for a child that is entered
        add     t0, s10, s1
        lbu     t1, 2(t0)
        blt     s6, t1, f2end       # h > moves left
        add     t2, a5, s2
        lbu     t3, 2(t2)
        blt     s6, t3, f2end
        lhu     s1, 0(t0)
        lhu     s2, 0(t2)
        jal     t6, descend2      # t6 is where this level resumes
f2end:
ascend:
        # every move tried: back up one level, the child was s8/s9
        beq     s7, a3, deeper
        addi    s7, s7, -16
        mv      s1, s8
        mv      s2, s9
        lw      t6, 0(s7)
        lw      s8, 4(s7)
        lw      s9, 8(s7)
        lw      s5, 12(s7)
        addi    s6, s6, 1
        jr      t6
deeper:
        addi    a2, a2, 1
        li      t0, 12
        bne     a2, t0, iteration
        li      a0, -1
        ret
found:
        # every saved resume address names a move, look each one up
        sw      t6, 0(s7)
        la      t0, moves
        mv      t1, a3
copy:
        lw      t2, 0(t1)
        la      t3, resume
        li      a6, 0
find:
        lw      a7, 0(t3)
        beq     a7, t2, code_found
        addi    t3, t3, 4
        addi    a6, a6, 1
        j       find
code_found:
        sb      a6, 0(t0)
        addi    t0, t0, 1
        addi    t1, t1, 16
        bgeu    s7, t1, copy
        mv      a0, a2
        ret

# print "<state>: <moves>" for the case at s0, s1 moves
print_case:
        mv      a0, s0
        li      a7, 4
        ecall
        la      a0, colon
        ecall
        la      t0, moves
        add     t1, t0, s1
print_move:
        beq     t0, t1, print_end
        lbu     t2, 0(t0)
        slli    t2, t2, 2
        la      a0, names
        add     a0, a0, t2
        ecall
        addi    t0, t0, 1
        j       print_move
print_end:
        la      a0, newline
        ecall
        ret

# apply the s1 moves to cube_p/cube_o and check that the result is solved,
# a0 = 0 if it is
replay:
        la      s2, moves
        add     s3, s2, s1
replay_move:
        beq     s2, s3, replay_check
        lbu     t0, 0(s2)
        li      t1, 0               # face = move / 3 without a divide
        li      t2, 3
face_of:
        blt     t0, t2, have_face
        addi    t0, t0, -3
        addi    t1, t1, 1
        j       face_of
have_face:
        addi    s4, t0, 1           # quarter turns
        slli    t1, t1, 3
        la      s5, source
        add     s5, s5, t1
        la      s6, twist
        add     s6, s6, t1
quarter:
        # copy, then pull each position from source and add twist mod 3
        la      t0, cube_p
        la      t1, cube_o
        la      t2, tmp
        lw      t3, 0(t0)
        sw      t3, 0(t2)
        lw      t3, 4(t0)
        sw      t3, 4(t2)
        li      t3, 0
pull_p:
        add     t4, s5, t3
        lbu     t4, 0(t4)
        add     t4, t2, t4
        lbu     t4, 0(t4)
        add     t5, t0, t3
        sb      t4, 0(t5)
        addi    t3, t3, 1
        li      t5, 7
        bne     t3, t5, pull_p
        lw      t3, 0(t1)
        sw      t3, 0(t2)
        lw      t3, 4(t1)
        sw      t3, 4(t2)
        li      t3, 0
pull_o:
        add     t4, s5, t3
        lbu     t4, 0(t4)
        add     t4, t2, t4
        lbu     t4, 0(t4)
        add     t5, s6, t3
        lbu     t5, 0(t5)
        add     t4, t4, t5          # at most 4
        addi    t5, t4, -3
#ifdef MOD3_BRANCHLESS
        srai    t6, t5, 31          # all ones when no reduction was needed
        andi    t6, t6, 3
        add     t4, t5, t6
#else
        bltz    t5, mod_done
        mv      t4, t5
mod_done:
#endif
        add     t5, t1, t3
        sb      t4, 0(t5)
        addi    t3, t3, 1
        li      t5, 7
        bne     t3, t5, pull_o
        addi    s4, s4, -1
        bnez    s4, quarter
        addi    s2, s2, 1
        j       replay_move
replay_check:
        la      t0, cube_p
        la      t1, cube_o
        li      t2, 0
        li      a0, 0
check_loop:
        add     t3, t0, t2
        lbu     t3, 0(t3)
        bne     t3, t2, not_solved
        add     t3, t1, t2
        lbu     t3, 0(t3)
        bnez    t3, not_solved
        addi    t2, t2, 1
        li      t3, 7
        bne     t2, t3, check_loop
        ret
not_solved:
        li      a0, 1
        ret
