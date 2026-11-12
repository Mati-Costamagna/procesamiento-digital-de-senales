# Simulación de la Parte 1 y la Parte 2 en la PC, con el MISMO código del
# firmware (TP4/source) y la misma CMSIS-DSP.
#   make CMSIS_DSP=/ruta/a/CMSIS-DSP
DEPS       ?= $(HOME)/deps
CMSIS_DSP  ?= $(DEPS)/CMSIS-DSP

SRC        := ../../source

CFLAGS := -D__GNUC_PYTHON__ -O2 -std=gnu11 -Wall -Wextra -I$(SRC) -I$(CMSIS_DSP)/Include -I$(CMSIS_DSP)/PrivateInclude
DSP    := $(CMSIS_DSP)/Source/FilteringFunctions/arm_lms_f32.c \
          $(CMSIS_DSP)/Source/FilteringFunctions/arm_lms_init_f32.c \
          $(CMSIS_DSP)/Source/FilteringFunctions/arm_lms_norm_f32.c \
          $(CMSIS_DSP)/Source/FilteringFunctions/arm_lms_norm_init_f32.c
COMMON := $(SRC)/lms.c $(SRC)/txt_out.c $(SRC)/coefs.c $(DSP)

all: p1_sim p2_sim

p1_sim: p1_sim.c $(SRC)/parte1.c $(COMMON)
	$(CC) $(CFLAGS) $^ -lm -o $@

p2_sim: p2_sim.c $(SRC)/parte2.c $(COMMON)
	$(CC) $(CFLAGS) $^ -lm -o $@

clean:
	rm -f p1_sim p2_sim

.PHONY: all clean
