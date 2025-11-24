# Compilateur && options
CC = gcc
CFLAGS = -Wall -Wextra -Werror -Isrc/include

# Dossiers
SRCDIR = src/core
BUILDDIR = build

# Executables
EXEC = tadmor erraid
EXEC_PATHS = $(addprefix $(BUILDDIR)/, $(EXEC))

# Sources
SRC_TADMOR = $(SRCDIR)/tadmor.c
SRC_ERRAID = $(SRCDIR)/erraid.c
SRC_TIMING = $(SRCDIR)/timing.c
SRC_COMMAND = $(SRCDIR)/command.c

# Objets
OBJ_TADMOR = $(BUILDDIR)/tadmor.o
OBJ_ERRAID = $(BUILDDIR)/erraid.o
OBJ_TIMING = $(BUILDDIR)/timing.o
OBJ_COMMAND = $(BUILDDIR)/command.o

# Construction globale

all: $(EXEC_PATHS)

$(BUILDDIR)/tadmor: $(OBJ_TADMOR)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILDDIR)/erraid: $(OBJ_ERRAID) $(OBJ_TIMING) $(OBJ_COMMAND)
	$(CC) $(CFLAGS) -o $@ $^
	cp $(BUILDDIR)/erraid ./erraid

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR)
	rm -f erraid
	rm -rf .sy5-2025-2026-projet-erraid-autotests.nosync

distclean: clean
	rm -f $(EXEC_PATHS)

launch: all
	@if [ -z "$(ARG)" ]; then \
		echo "ERREUR il manque l'argument suivant ARG=<path>"; \
		exit 1; \
	fi
	$(BUILDDIR)/erraid $(ARG)

.PHONY: all clean distclean