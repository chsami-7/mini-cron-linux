# Projet Planificateur de Tâches (Erraid & Tadmor)

**Auteurs :** Mehdi CHATER - Sami CHIOUKH - Said BESSID
**Projet :** Système L3 - Planificateur de tâches (Mini-Cron)

Ce document décrit en détail l'architecture logicielle, les choix de conception, les structures de données et les algorithmes implémentés pour le couple démon (`erraid`) et client (`tadmor`).

---

## 1. Vue d'Ensemble de l'Architecture

Le projet repose sur une architecture **Client-Serveur** locale communiquant via des Tubes.

Le système est divisé en deux entités principales qui interagissent de manière synchrone :

1.  **Le Démon (`erraid`)** :
    * C'est le processus serveur. Il peut tourner en tâche de fond (daemon) ou au premier plan (avec l'option `-F`).
    * **Mono-threadé & Événementiel** : Il n'utilise pas de threads multiples. À la place, il utilise l'appel système `select()` pour multiplexer l'attente des requêtes client et le réveil périodique (chaque minute) nécessaire à l'exécution des tâches planifiées.
    * **Persistance** : Il maintient l'état des tâches en mémoire (dans un tableau `Tache taches[]`) qui est synchronisé avec une arborescence sur le disque (`tasks/`).

2.  **Le Client (`tadmor`)** :
    * C'est l'interface utilisateur en ligne de commande (CLI).
    * Il est éphémère : il démarre, analyse les arguments, envoie une requête formatée au démon, attend la réponse, l'affiche à l'utilisateur, et se termine.

### Flux de Communication
Les échanges se font via deux tubes nommés situés par défaut dans `/tmp/$USER/erraid/pipes/` :
* `erraid-request-pipe` : Utilisé par le client pour envoyer des ordres (Opcode + Données) au démon.
* `erraid-reply-pipe` : Utilisé par le démon pour renvoyer le statut (OK/Erreur) et les données demandées.

---

## 2. Organisation du Code Source

L'organisation des fichiers sépare clairement les responsabilités

| Module | Fichiers Sources | Responsabilité |
| :--- | :--- | :--- |
| **Core Démon** | `src/core/erraid.c` | Point d'entrée, gestion des signaux, démonisation, boucle principale `select()`. |
| **Core Client** | `src/core/tadmor.c` | Point d'entrée CLI, parsing des arguments (`getopt`), affichage utilisateur. |
| **Logique Tâches** | `src/core/gestion_taches.c` | Chargement initial de l'arborescence, gestion globale de la liste des tâches, utilitaires de répertoires. |
| **Exécution** | `src/core/tache.c` | **Cœur de l'exécution** : gestion des `fork`, `exec`, redirections IO, et récursivité pour les commandes complexes (Séquences/Pipelines/Conditionnelles). |
| **Protocole (API)** | `src/core/api_erraid.c`<br>`src/core/api_tadmor.c` | Couche de communication : encode les requêtes (côté client) et aiguille les Opcodes vers les bonnes actions (côté serveur). |
| **Sérialisation** | `src/core/command.c`<br>`src/core/timing.c` | Conversion Structures <-> Binaire (Big-Endian), lecture/écriture des fichiers de configuration. |
| **Utils** | `src/core/io_utils.c` | Wrappers robustes (`read_full`, `write_full`) pour garantir l'intégrité des lectures/écritures système. |

---

## 3. Structures de Données Critiques

### 3.1. Représentation des Tâches (`Tache`)
Structure centrale stockée en mémoire dans un tableau statique `taches[2048]`. Elle agit comme un cache de l'état disque.

```c
typedef struct {
    int id;                     // Identifiant unique de la tâche
    char path[2048];            // Chemin absolu du répertoire de la tâche sur le disque
    Timing timing;              // Structure de planification horaire
    Command cmd;                // Structure de commande (Arbre de commandes)
    time_t derniere_execution;  // Timestamp (minute) pour éviter les exécutions multiples dans la même minute
} Tache;
```
### 3.2. Le Timing (Bitmaps)
Pour optimiser l'espace mémoire et la rapidité des tests d'exécution, la planification temporelle est représentée par des masques binaires (Bitmaps).

* **Minutes** : 60 bits stockés sur un `uint64_t`. Le bit *n* correspond à la minute *n*.
* **Heures** : 24 bits stockés sur un `uint32_t`. Le bit *n* correspond à l'heure *n*.
* **Jours** : 7 bits stockés sur un `uint8_t`. Le bit 0 est Dimanche, le bit 6 est Samedi.

**Algorithme de vérification :**
Une tâche est éligible à l'exécution si et seulement si le bit correspondant à l'instant actuel est à `1` dans les trois champs simultanément :
```c
if ((t->minutes & (1ULL << now->tm_min)) && 
    (t->heures  & (1U   << now->tm_hour)) && 
    (t->jours   & (1U   << now->tm_wday))) {
    // Exécuter
}
```

### 3.3. La Commande Récursive (Command)
Cette structure récursive est le cœur de la flexibilité du système. Elle permet de représenter des arbres de commandes arbitrairement complexes (Séquences, Pipelines, Conditionnelles).
```c
typedef struct Command {
    uint16_t type;          // Type : SI (Simple), SQ (Séquence), PL (Pipeline), IF (Condition)
    Arguments args;         // Utilisé uniquement si type == SI : contient argc et argv[]
    uint32_t nbcmds;        // Nombre de sous-commandes (fils)
    struct Command *cmds;   // Tableau dynamique récursif de sous-commandes
} Command;
```

## 4. Algorithmes et Logique Système

### 4.1. La Boucle Événementielle (`erraid.c` & `gestion_taches.c`)
Le démon est conçu pour être efficace et n'utilise pas d'attente active (*busy waiting*).

* **Calcul du Timeout** : La fonction `compute_minute_timeout` calcule le nombre exact de secondes restantes jusqu'à la prochaine minute pleine (ex: s'il est 12:00:15, le timeout sera de 45s).
* **Multiplexage IO** : L'appel système `select()` met le processus en sommeil. Il surveille deux événements :
    1.  Activité sur le descripteur du tube de requête (`fd_req`).
    2.  Expiration du délai (timeout) calculé précédemment.
* **Réveil et Dispatch** :
    * **Cas A (Requête Client)** : Si `fd_req` est actif, l'Opcode est lu et la requête est transmise à la fonction `handle_request`.
    * **Cas B (Nouvelle Minute)** : Si le timeout est atteint, le démon parcourt toutes les tâches via `doit_executer`. Si une tâche correspond à l'heure actuelle et n'a pas encore été marquée comme exécutée pour cette minute précise, `executer_tache` est appelée.

### 4.2. Exécution Récursive et Pipelines (`tache.c`)
La fonction `execute_recursive` gère le polymorphisme des commandes via une approche récursive en profondeur (DFS).

* **Commande Simple (SI)** :
    * Appel à `fork()`.
    * Le fils recouvre son code via `execvp()`.
    * Le père attend la fin via `waitpid()`.
* **Séquence (SQ)** :
    * Boucle itérative exécutant les sous-commandes une par une.
    * Le code de retour de la séquence est celui de la dernière commande exécutée.
* **Pipeline (PL)** :
    * Allocation dynamique d'un tableau de PIDs.
    * Création de tubes anonymes (`pipe()`) entre chaque segment.
    * **Gestion fine des descripteurs (`dup2`)** :
        * Le processus *i* connecte son `stdout` à l'entrée d'écriture du tube actuel.
        * Le processus *i* connecte son `stdin` à la sortie de lecture du tube précédent (sauvegardé dans `prev_fd`).
    * **Fermeture des descripteurs** : Point critique. Les bouts de lecture/écriture inutilisés sont fermés immédiatement dans le père et les fils pour éviter les blocages (*deadlocks*) causés par des "écrivains fantômes".
    * Le père attend tous ses fils. Le code de retour est celui du dernier élément du pipeline.
* **Conditionnelle (IF)** :
    * Exécute la première sous-commande (la condition).
    * Si son `WEXITSTATUS` est 0 (succès), exécute la deuxième sous-commande (THEN).
    * Sinon, exécute la troisième sous-commande (ELSE), si elle existe.

### 4.3. Gestion de la Persistance (Format de Sérialisation)
Pour respecter le sujet et garantir la portabilité des données (indépendance de l'architecture processeur), toutes les données binaires sont écrites en **Big-Endian**.

* **Écriture** : Fonctions `htobe16`, `htobe32`, `htobe64`.
* **Lecture** : Fonctions `be16toh`, `be32toh`, `be64toh`.
* **Arborescence Disque** :
    * Chaque tâche est un répertoire nommé par son ID entier (ex: `tasks/42/`).
    * La structure arborescente des commandes complexes est reproduite physiquement sur le disque via des sous-dossiers (`cmd/0/`, `cmd/1/`, etc.).
    * Les chaînes de caractères (arguments) sont stockées au format Pascal (`LENGTH` sur 4 octets + `DATA`).

---

## 5. Protocole de Communication

Le protocole est binaire, synchronisé et orienté message. L'intégrité des données est garantie par les wrappers `read_full` et `write_full` qui gèrent les interruptions et les buffers partiels.

**Scénario d'une requête standard :**

1.  **Client (Émission)** :
    * Ouvre `req_pipe` en écriture (l'appel bloque tant que le serveur n'a pas ouvert l'autre bout, assurant la synchro).
    * Envoie `OPCODE` (2 octets).
    * Envoie les paramètres spécifiques (Timing, ID, chaînes, etc.).
    * Ferme `req_pipe` (signale la fin de la requête au serveur).
    * Ouvre immédiatement `rep_pipe` en lecture pour attendre la réponse.
2.  **Serveur (Réception & Réponse)** :
    * Détecte l'ouverture du tube via `select`.
    * Lit `OPCODE` et les données.
    * Traite la demande (ex: créer un dossier, supprimer une tâche).
    * Ouvre `rep_pipe` en écriture.
    * Envoie `ANSTYPE` (OK/ER) suivi des données demandées ou du code erreur.
    * Ferme `rep_pipe`.

---

## 6. Gestion des Erreurs et Robustesse

* **Processus Zombies** : Le démon principal configure `sigaction(SIGCHLD, SIG_IGN)`. Cela informe le noyau que le processus père ne souhaite pas récupérer le code de retour de ses fils défunts, permettant au système de les nettoyer automatiquement sans créer de zombies.
* **Détachement** : L'appel `setsid()` est utilisé lors de la démonisation. Il crée une nouvelle session et détache le processus du terminal de contrôle, évitant qu'il ne soit tué lors de la fermeture du shell utilisateur.
* **IO Partielles** : Les lectures et écritures sur les tubes utilisent des boucles `while` pour gérer les cas où les tampons systèmes ne permettent pas de tout transférer en un seul appel système (robustesse réseau/tube).
* **Nettoyage Propre** :
    * La commande `tadmor -q` (TERMINATE) déclenche une procédure d'arrêt propre.
    * Le démon supprime les fichiers FIFO (tubes nommés) du système de fichiers.
    * Il libère la mémoire allouée dynamiquement (arbres de commandes) avant de quitter via `exit(0)`.

---

## 7. Compilation et Déploiement

Le projet utilise un `Makefile` standard pour l'automatisation.

* `make` : Compile les exécutables `erraid` et `tadmor` à la racine du projet.
* `make distclean` : Nettoie tous les fichiers objets et exécutables pour remettre le dépôt à neuf.
* **Dépendances** : Le projet n'utilise aucune librairie externe, se basant uniquement sur la librairie standard C (libc) et les appels système POSIX (`unistd.h`, `sys/types.h`, `fcntl.h`, `dirent.h`).
* **Portabilité** : Conçu pour les systèmes conformes POSIX, le projet a été testé et validé spécifiquement sur (`lulu`).