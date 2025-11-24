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
Makefile
```

## Nettoyage
    make clean
    make distclean

## Jalon 1
Validé — 3/3 sur Lulu
```
JALON 1, TEST 1:
TEST RESULT: OK
============

JALON 1, TEST 2:
TEST RESULT: OK
============

JALON 1, TEST 5:
TEST RESULT: OK
============

JALON 1: 3/3 successul tests, 0 failures
```