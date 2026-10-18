CC ?= cc
CFLAGS ?= -O3 -std=c99 -Wall -Wextra -Wpedantic
FRAMA_C ?= frama-c
CLANG_FORMAT := $(shell command -v clang-format-20 2>/dev/null || \
	command -v clang-format 2>/dev/null)
C_SOURCES := $(wildcard *.c *.h)
SAMPLE_STATE := 21345671111111
SAMPLE_SOLUTION := B' R' D2 R' B R B' R D2 B R'
VECTORS := tests/solutions.txt
# One per rejection path: short, long, cubie digit low, cubie digit high,
# orientation digit low, orientation digit high, non-digit, duplicate, parity.
INVALID_STATES := 1234567111111 123456711111111 02345671111111 82345671111111 \
	12345671111110 12345671111114 1234567111111a 11345671111111 12345671111112

.PHONY: all check gates ref asm ledtest prove clean indent

all: solver mini ida

solver: solver.c
	$(CC) $(CFLAGS) $< -o $@

mini: mini.c
	$(CC) $(CFLAGS) $< -o $@

pdb_gen: pdb_gen.c
	$(CC) $(CFLAGS) $< -o $@

pdb.h: pdb_gen
	./pdb_gen c >$@

ida: ida.c pdb.h
	$(CC) $(CFLAGS) -DIDA_COUNT $< -o $@

gates-bin: gates.c ida.c pdb.h
	$(CC) $(CFLAGS) -DIDA_COUNT $< -o $@

# H1 to H3 on the host; H3 solves all 3,674,160 states.
gates: gates-bin
	./gates-bin

# gcc reference build of ida.c for Ripes, one ELF per state
RV_CC ?= riscv64-elf-gcc
RV_CFLAGS := -O2 -march=rv32i -mabi=ilp32 -nostartfiles -static
REF_STATES := 54721631111111 21345671111111 62345713133111

ref/ida-%.elf: ref/target.c ref/start.s ida.c pdb.h
	$(RV_CC) $(RV_CFLAGS) $$(ref/rank.sh $*) ref/start.s ref/target.c -o $@

ref: $(REF_STATES:%=ref/ida-%.elf)
	@for s in $(REF_STATES); do \
		printf '%s ' $$s; \
		ripes --mode cli --src ref/ida-$$s.elf -t elf --proc RV32_ISS --iret | \
			awk 'NR == 1 { printf "length %s ", $$1 } /exited/ { printf "exit %s ", $$NF } \
				/retired/ { getline; print "iret", $$1 }'; \
	done
	@riscv64-elf-size -A ref/ida-$(firstword $(REF_STATES)).elf | awk '$$1 == ".text" { print ".text", $$2, "bytes" }'

# hand-written RV32I solver; Ripes' assembler has no .if, so cpp picks
# the renderer and the cases, and the two builds differ only in RENDER
CPP_ASM := gcc -E -P -x assembler-with-cpp

pdb.s: pdb_gen
	./pdb_gen asm >$@

cube-cli.s: cube.S pdb.s
	$(CPP_ASM) -DRENDER=0 $< -o $@

cube-gui.s: cube.S pdb.s
	$(CPP_ASM) -DRENDER=1 $< -o $@

cube-%.s: cube.S pdb.s
	$(CPP_ASM) -DRENDER=0 -DSTATE='"$*"' $< -o $@

# built-in cases on RV32_ISS, then each REF_STATES alone, then .text size
asm: cube-cli.s cube-gui.s $(REF_STATES:%=cube-%.s)
	@for src in cube-cli.s $(REF_STATES:%=cube-%.s); do \
		printf '%s ' $$src; \
		ripes --mode cli --src $$src -t asm --proc RV32_ISS --iret | \
			awk '/exited/ { printf "exit %s ", $$NF } \
				/retired/ { getline; print "iret", $$1 }'; \
	done
	@riscv64-linux-gnu-as -march=rv32i -mabi=ilp32 cube-cli.s -o cube-cli.o && \
		riscv64-linux-gnu-size -A cube-cli.o | \
		awk '$$1 == ".text" { print ".text", $$2, "bytes" } $$1 == ".data" { print ".data", $$2, "bytes" }'

# renderer in the CLI: draw into plain memory and compare each scrambled
# frame's checksum with the 3D sticker model in led_model.py
LED_SUMS := 63575002 370177724 1046056636

cube-ledtest.s: cube.S pdb.s
	$(CPP_ASM) -DRENDER=0 -DLED_TEST -DLED_MATRIX_0_BASE=0x20000000 \
		-DLED_MATRIX_0_WIDTH=35 $< -o $@

ledtest: cube-ledtest.s
	@sums=$$(ripes --mode cli --src cube-ledtest.s -t asm --proc RV32_ISS | \
		tr -d '\000' | grep -E '^-?[0-9]+$$' | tr '\n' ' '); \
		test "$$sums" = "$(LED_SUMS) " || { echo "LED checksums: $$sums"; exit 1; }; \
		echo "LED frames match led_model.py: $$sums"

check: solver mini $(VECTORS)
	./solver --self-test
	@expected=$$(mktemp); actual=$$(mktemp); \
		trap 'rm -f "$$expected" "$$actual"' 0 1 2 15; \
		count=0; \
		while IFS='|' read -r state solution; do \
			case "$$state" in ""|\#*) continue ;; esac; \
			printf '%s\n' "$$solution" >"$$expected"; \
			for binary in ./solver ./mini; do \
				$$binary "$$state" >"$$actual"; \
				status=$$?; \
				test $$status -eq 0 || { \
					echo "$$binary $$state: exit status $$status"; exit 1; }; \
				cmp -s "$$actual" "$$expected" || { \
					echo "$$binary $$state: output mismatch"; \
					echo "  expected: $$solution"; \
					printf '  got:      '; cat "$$actual"; \
					echo "  ($$(wc -c <"$$expected") bytes expected, \
$$(wc -c <"$$actual") produced)"; exit 1; }; \
			done; \
			count=$$((count + 1)); \
		done <$(VECTORS); \
		echo "$$count solution vectors matched by solver and mini"
	@for binary in ./solver ./mini; do \
		for bad in $(INVALID_STATES); do \
			$$binary "$$bad" >/dev/null 2>&1; \
			status=$$?; \
			test $$status -eq 2 || { \
				echo "$$binary $$bad: expected status 2, got $$status"; exit 1; }; \
		done; \
		$$binary >/dev/null 2>&1; \
		status=$$?; \
		test $$status -eq 2 || { \
			echo "$$binary with no argument: expected status 2, got $$status"; \
			exit 1; }; \
		$$binary $(SAMPLE_STATE) $(SAMPLE_STATE) >/dev/null 2>&1; \
		status=$$?; \
		test $$status -eq 2 || { \
			echo "$$binary with two arguments: expected status 2, got $$status"; \
			exit 1; }; \
		$$binary $(SAMPLE_STATE) >&- 2>/dev/null; \
		status=$$?; \
		test $$status -eq 1 || { \
			echo "$$binary with stdout closed: expected status 1, got $$status"; \
			exit 1; }; \
	done
	@./solver --self-test >&- 2>/dev/null; \
		status=$$?; \
		test $$status -eq 1 || { \
			echo "solver --self-test with stdout closed: expected 1, got $$status"; \
			exit 1; }
	@echo "invalid input rejected with status 2, unwritable stdout with status 1"

prove: solver.c
	@log=$$(mktemp); trap 'rm -f "$$log"' 0 1 2 15; \
		$(FRAMA_C) -wp -wp-fct quarter_turn,rank_state,valid,parse_state \
		-wp-rte -rte-verbose 0 -wp-prover alt-ergo -wp-timeout 20 \
		-wp-cache none solver.c >"$$log" 2>&1; rc=$$?; \
		grep -Fvx -e '[wp] Warning: Skipped RTE guards: unaligned pointers (\aligned not supported)' \
		-e '[wp] Warning: Skipped RTE guards: invalid function pointer calls (\valid_function not supported)' "$$log"; \
		test $$rc -eq 0 && awk '$$1 == "[wp]" && $$2 == "Proved" && $$3 == "goals:" && $$4 > 0 && $$4 == $$6 { ok = 1 } END { exit !ok }' "$$log" && \
		! grep -Eq '(^|[[:space:]])(Timeout|Unknown|Failed):' "$$log"

indent:
ifeq ($(CLANG_FORMAT),)
	$(error clang-format 20 not found)
endif
	@$(CLANG_FORMAT) --version | grep -q 'version 20' || \
		{ echo "error: clang-format version 20 required"; exit 1; }
	$(CLANG_FORMAT) -i $(C_SOURCES)

clean:
	$(RM) solver mini pdb_gen pdb.h ida gates-bin ref/*.elf pdb.s cube-*.s cube-*.o
