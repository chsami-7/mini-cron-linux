# Projet erraid & tadmor

## Lancer le projet

Depuis la racine du projet :

    make
    ./test.sh

Les exécutables sont générés dans `build/`

## Structure
```
src/
  core/        # .c
  include/     # .h
build/         # exécutables + .o
test.sh
erraid         # exécutable pour le test.sh
tadmor         # exécutable pour le test.sh
Makefile
```

## Nettoyage
    make clean
    make distclean