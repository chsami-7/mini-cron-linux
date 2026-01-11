# Compilateur && options
CC = gcc
CFLAGS = -Wall -Wextra -Werror -Isrc/include -MMD -MP

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
SRC_TACHE = $(SRCDIR)/tache.c
SRC_IO = $(SRCDIR)/io_utils.c
SRC_API_ERRAID = $(SRCDIR)/api_erraid.c
SRC_GESTION_TACHES = $(SRCDIR)/gestion_taches.c
SRC_API_TADMOR = $(SRCDIR)/api_tadmor.c

# Objets
OBJ_TADMOR = $(BUILDDIR)/tadmor.o
OBJ_ERRAID = $(BUILDDIR)/erraid.o
OBJ_TIMING = $(BUILDDIR)/timing.o
OBJ_COMMAND = $(BUILDDIR)/command.o
OBJ_TACHE = $(BUILDDIR)/tache.o
OBJ_IO = $(BUILDDIR)/io_utils.o
OBJ_API_ERRAID = $(BUILDDIR)/api_erraid.o
OBJ_GESTION_TACHES = $(BUILDDIR)/gestion_taches.o
OBJ_API_TADMOR = $(BUILDDIR)/api_tadmor.o

# Construction globale

all: $(EXEC_PATHS)

$(BUILDDIR)/tadmor: $(OBJ_TADMOR) $(OBJ_TIMING) $(OBJ_COMMAND) $(OBJ_TACHE) $(OBJ_IO) $(OBJ_API_TADMOR)
	$(CC) $(CFLAGS) -o $@ $^
	cp $(BUILDDIR)/tadmor ./tadmor

$(BUILDDIR)/erraid: $(OBJ_ERRAID) $(OBJ_TIMING) $(OBJ_COMMAND) $(OBJ_TACHE) $(OBJ_IO) $(OBJ_API_ERRAID) $(OBJ_GESTION_TACHES)
	$(CC) $(CFLAGS) -o $@ $^
	cp $(BUILDDIR)/erraid ./erraid

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR)
	rm -rf .sy5-2025-2026-projet-erraid-autotests.nosync

distclean: clean
	rm -f erraid tadmor

launch: all
	@if [ -z "$(ARG)" ]; then \
		echo "ERREUR il manque l'argument suivant ARG=<path>"; \
		exit 1; \
	fi
	$(BUILDDIR)/erraid $(ARG)

jalon1: clean all
	./test.sh 1

jalon2: clean all
	./test.sh 2

jalon: clean all
	./test.sh

.PHONY: all clean distclean jalon

-include $(wildcard $(BUILDDIR)/*.d)