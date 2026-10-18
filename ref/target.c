// gcc reference build: solve one state given as -DSTATE_P/-DSTATE_O ranks
// and print the solution length through Ripes ecalls
#define IDA_NO_MAIN
#include "../ida.c"

static void print_int(int x)
{
    register int a0 __asm__("a0") = x;
    register int a7 __asm__("a7") = 1;
    __asm__ volatile("ecall" : : "r"(a0), "r"(a7));
}

int main(void)
{
    uint8_t moves[MAX_DEPTH];
    print_int(solve(STATE_P, STATE_O, moves));
    return 0;
}
