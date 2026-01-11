# Projet Erraid & Tadmor

Un planificateur de tâches (mini-cron) composé d'un démon (`erraid`) et d'un client en ligne de commande (`tadmor`).

## 🛠️ Compilation

À la racine du projet :
```bash
make
```

*Les exécutables `erraid` et `tadmor` sont générés à la racine et dans le dossier `build/`.*

## 🚀 Utilisation Rapide

### 1. Lancer le démon (`erraid`)

```bash
./erraid &          # Mode démon (arrière-plan)
# ou
./erraid -F         # Mode avant-plan (foreground)
# ou
./erraid -P <DIR>   # Spécifier un dossier de tubes personnalisé
```

### 2. Utiliser le client (`tadmor`)

Voici la liste exhaustive des commandes possibles :

#### A. Création de tâches simples (`-c`)

*L'option `-c` doit être suivie de la commande et ses arguments.*

```bash
# Tâche simple (exécutée chaque minute par défaut)
./tadmor -c echo "Bonjour"

# Tâche planifiée (Minutes, Heures, Jours 0=Dimanche)
./tadmor -m 0,30 -H 8-18 -d 1-5 -c echo "Au boulot"

# Tâche "abstraite" (jamais exécutée automatiquement, utile pour les combinaisons)
./tadmor -n -c ls -l

```

*Options de temps : `-m` (minutes 0-59), `-H` (heures 0-23), `-d` (jours 0-6).*

#### B. Création de tâches complexes (`-s`, `-p`, `-i`)

*Combine des IDs de tâches existantes. Les anciennes tâches sont supprimées.*

```bash
# Séquence (exécute ID 1 puis ID 2)
./tadmor -s <ID1> <ID2>

# Pipeline (stdout de ID 1 -> stdin de ID 2)
./tadmor -p <ID1> <ID2>

# Conditionnelle (SI ID1 réussit ALORS ID2 SINON ID3)
./tadmor -i <ID1> <ID2> <ID3>

```

#### C. Consultation et Logs

```bash
# Lister toutes les tâches planifiées
./tadmor -l

# Voir la sortie standard (stdout) de la dernière exécution
./tadmor -o <ID>

# Voir la sortie d'erreur (stderr) de la dernière exécution
./tadmor -e <ID>

# Voir l'historique des exécutions (dates et codes de retour)
./tadmor -x <ID>

```

#### D. Administration

```bash
# Supprimer une tâche spécifique
./tadmor -r <ID>

# Arrêter proprement le démon erraid (TERMINATE)
./tadmor -q

# Utiliser un dossier de tubes spécifique (valable pour toutes les commandes)
./tadmor -P /tmp/mon_dossier -l

```

## 📂 Structure du projet

```text
.
├── src/
│   ├── core/        # Fichiers sources (.c)
│   └── include/     # Fichiers d'en-tête (.h)
├── build/           # Fichiers objets (.o) et binaires compilés
├── tasks/           # Dossier de persistance des tâches (généré)
├── erraid           # Exécutable Serveur
├── tadmor           # Exécutable Client
├── test.sh          # Script des tests j1&j2
├── ARCHITECTURE.md  # Fichier d'arbo descriptif 
└── Makefile

```

## 🧹 Nettoyage

```bash
make clean       # Supprime les objets et le dossier build
make distclean   # Supprime tout (build + exécutables racine)

```