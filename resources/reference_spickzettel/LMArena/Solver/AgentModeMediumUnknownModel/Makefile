# ============================================================================
#  Coupled THM multiphysics engine -- build driver
#
#  make            optimised build (default: -O3 -march=native)
#  make debug      bounds/alias/FP checked build  (-fcheck=all -g)
#  make test       run the verification suite
#  make run        run the benchmark at the default size
#  make clean
#
#  The module dependency order is explicit and linear -- the modules compile
#  in exactly this sequence, so no dependency generator (and no cmake) is
#  needed.
# ============================================================================

FC      ?= gfortran
BUILD   := build
SRC     := src
OUT     := out

FFLAGS_COMMON := -ffree-line-length-none -J$(BUILD) -I$(BUILD)
FFLAGS_OPT    := -O3 -march=native -funroll-loops -fopenmp -fmax-errors=5
FFLAGS_DEBUG  := -O0 -g -fcheck=all -fbacktrace -fopenmp -Wall
FFLAGS        ?= $(FFLAGS_OPT)
LDFLAGS       ?= -fopenmp

MODULES := mod_kinds mod_util mod_mesh mod_linalg mod_layout mod_physics mod_solver
MODSRC  := $(addprefix $(SRC)/,$(addsuffix .f90,$(MODULES)))
MODOBJ  := $(addprefix $(BUILD)/,$(addsuffix .o,$(MODULES)))

APPSRC  := app/thm_bench.f90
APPOBJ  := $(BUILD)/thm_bench.o
APP     := $(BUILD)/thm_bench
TEST    := $(BUILD)/test_vnv

.PHONY: all debug test bench run clean dirs

all: dirs $(APP) $(TEST)

dirs:
	@mkdir -p $(BUILD) $(OUT)

$(BUILD)/%.o: $(SRC)/%.f90 | dirs
	$(FC) $(FFLAGS_COMMON) $(FFLAGS) -c $< -o $@

# enforce the compilation order
$(BUILD)/mod_util.o:    $(BUILD)/mod_kinds.o
$(BUILD)/mod_mesh.o:    $(BUILD)/mod_util.o
$(BUILD)/mod_linalg.o:  $(BUILD)/mod_util.o
$(BUILD)/mod_layout.o:  $(BUILD)/mod_mesh.o
$(BUILD)/mod_physics.o: $(BUILD)/mod_layout.o $(BUILD)/mod_linalg.o
$(BUILD)/mod_solver.o:  $(BUILD)/mod_physics.o

$(APPOBJ): $(APPSRC) $(MODOBJ)
	$(FC) $(FFLAGS_COMMON) $(FFLAGS) -c $< -o $@

$(APP): $(APPOBJ) $(MODOBJ)
	$(FC) $(FFLAGS) $(MODOBJ) $(APPOBJ) $(LDFLAGS) -o $@

$(BUILD)/test_vnv.o: tests/test_vnv.f90 $(MODOBJ)
	$(FC) $(FFLAGS_COMMON) $(FFLAGS) -c $< -o $@

$(TEST): $(BUILD)/test_vnv.o $(MODOBJ)
	$(FC) $(FFLAGS) $(MODOBJ) $(BUILD)/test_vnv.o $(LDFLAGS) -o $@

debug:
	$(MAKE) FFLAGS="$(FFLAGS_DEBUG)" all

bench: $(APP)

test: all
	@echo "=== verification suite ==="
	@OMP_NUM_THREADS=$${OMP_NUM_THREADS:-2} $(TEST)

run: $(APP)
	@OMP_NUM_THREADS=$${OMP_NUM_THREADS:-2} $(APP) --n $${N:-16}

clean:
	rm -rf $(BUILD) $(OUT)
