# Tests RT — Xenomai EVL pour `realtime_tools`

Documentation de la démarche de validation du comportement temps réel de la
bibliothèque `realtime_tools` sous Xenomai 4 / EVL.

---

## Contexte et problème

Les tests existants (`lock_free_queue_tests`, `realtime_mutex_tests`, etc.)
vérifient uniquement la **correction fonctionnelle** : push/pop fonctionne,
le mutex se lock/unlock, les types sont cohérents.

Ces tests **passent sur Xenomai** mais ne valident pas que la bibliothèque
fonctionne correctement dans le **domaine RT d'EVL**. Raisons :

| Problème | Conséquence |
|----------|-------------|
| Les threads de test sont des `std::thread` Linux ordinaires, jamais attachés à EVL | Les opérations testées ne s'exécutent pas dans le domaine OOB |
| Les mode switches OOB→IB ne sont pas détectés | Un syscall bloquant dans un thread EVL passe inaperçu |
| `mlockall()` jamais appelé | Des page faults peuvent se produire pendant les tests |
| Pas de `SCHED_FIFO` sur les threads de test | Le scheduler Linux CFS peut préempter à tout instant |
| `prio_inherit_mutex` utilise `PTHREAD_PRIO_INHERIT` (in-band) | Force un retour in-band à chaque lock/unlock depuis OOB |
| `std::this_thread::yield()` dans les tests lock-free | Syscall Linux bloquant depuis une perspective EVL |

**Un mode switch = la section n'est pas RT-safe sur Xenomai.**

---

## Plan d'action complet

### Phase 1 — Infrastructure de test EVL ✅ Fait

Fournir les outils de base réutilisables par toutes les phases suivantes :

- Détection runtime de Xenomai EVL (`is_evl_available()`)
- Setup de l'environnement RT (`mlockall`, `evl_init`, `SCHED_FIFO`)
- Comptage de mode switches OOB→IB (`ASSERT_NO_MODE_SWITCH`)
- Fixture GTest avec thread attaché à EVL (`EvlTestFixture`)
- Test de validation de l'infrastructure elle-même

**Fichiers créés :**
- `test/evl_test_helpers.hpp` — helpers et macros
- `test/evl_environment_tests.cpp` — validation de Phase 1
- `CMakeLists.txt` — détection EVL, flag `HAVE_EVL`, `HAVE_EVL_GET_STATS`

---

### Phase 2 — Tests RT-spécifiques par composant 🔲 À faire

Modifier les tests existants pour les exécuter depuis un thread EVL et
détecter les mode switches sur chaque composant.

**`realtime_mutex_tests`**

Le `prio_inherit_mutex` utilise `pthread_mutexattr_setprotocol(PTHREAD_PRIO_INHERIT)`.
Ce protocole est in-band : chaque `lock()`/`unlock()` depuis un thread OOB force
un passage OOB→IB. Le test doit **échouer intentionnellement** pour révéler
que ce mutex n'est pas RT-safe sur Xenomai — et ouvrir la voie à un
wrapper `evl_mutex`.

```
ASSERT_NO_MODE_SWITCH({
    mutex.lock();       // ← doit déclencher un mode switch
    mutex.unlock();
});
```

**`lock_free_queue_tests`**

Les `push()`/`pop()` sur atomiques matériels doivent être RT-safe.
- Remplacer `std::this_thread::yield()` par `evl_usleep(1)` dans les threads producteurs/consommateurs
- Vérifier absence de mode switch sur le hot-path

**`realtime_buffer_tests` / `realtime_thread_safe_box_tests`**

Vérifier que les opérations de copie du buffer ne causent pas de mode switch
(dépend du type T instancié).

**`thread_priority_tests`**

Ajouter un test vérifiant que `configure_sched_fifo()` place correctement le
thread dans le scheduler EVL (vérifiable via `evl ps -t`).

---

### Phase 3 — Benchmarks de latence et jitter 🔲 À faire

Nouveaux exécutables de benchmark (pas des unit-tests GTest) générant
des métriques exploitables par les scripts Python existants.

| Benchmark | Description | Métriques |
|-----------|-------------|-----------|
| `rt_periodic_latency` | Thread EVL à 1 kHz, `clock_nanosleep` | min/moy/max latence, histogramme jitter |
| `rt_queue_latency` | Producteur non-RT → queue → consommateur EVL | délai de transmission |
| `rt_mutex_contention` | Thread EVL vs thread Linux, mesure inversion priorité | temps de dépriorisation max |
| `rt_stress_robustness` | Tests fonctionnels sous `stress-ng` actif | mode switches, taux d'échec |

Sortie : fichier CSV + histogramme compatible avec les scripts `latmus`.

---

### Phase 4 — Portage sur Raspberry Pi 4/5 🔲 À faire

Exécuter les phases 1-3 sur les architectures ARM64 cibles et comparer
les résultats x86_64 vs RPi4 vs RPi5.

---

## Compilation

### Prérequis

- ROS 2 Jazzy installé
- Workspace colcon configuré
- Sur Xenomai : `libevl` installée (`apt install libevl-dev` ou compilation depuis source)

### Commande

```bash
# Sourcer ROS 2
source /opt/ros/jazzy/setup.bash

# Compiler le package avec les tests
colcon build --packages-select realtime_tools --cmake-args -DBUILD_TESTING=ON
```

Si `libevl` est détectée par CMake, le flag `HAVE_EVL` est activé
automatiquement et les tests EVL sont compilés avec le support EVL.
Si `libevl` est absente, les tests compilent quand même mais se sautent
à l'exécution via `GTEST_SKIP()`.

---

## Exécution des tests

### Lancer tous les tests du package

```bash
colcon test --packages-select realtime_tools
colcon test-result --verbose --packages-select realtime_tools
```

### Lancer uniquement les tests EVL (Phase 1)

Les tests EVL nécessitent les droits root pour `SCHED_FIFO` et `mlockall`.

```bash
# Sourcer l'environnement installé
source install/setup.bash

# Localiser l'exécutable
find install/ -name "evl_environment_tests"

# Lancer (root requis sur Xenomai)
sudo ./install/realtime_tools/lib/realtime_tools/evl_environment_tests
```

### Filtrer par groupe de tests

```bash
# Tous les tests de détection (fonctionne sur tous les OS)
sudo ./install/.../evl_environment_tests --gtest_filter="EnvironmentDetection.*"

# Tests du détecteur de mode switches (requiert Xenomai)
sudo ./install/.../evl_environment_tests --gtest_filter="ModeSwitchDetector.*"

# Tests de la fixture
sudo ./install/.../evl_environment_tests --gtest_filter="EvlFixtureSetup.*"
```

### Via colcon avec sudo

```bash
# -E préserve les variables d'environnement ROS 2
sudo -E colcon test --packages-select realtime_tools
```

---

## Résultats attendus par OS

| OS | `EnvironmentDetection` | `ModeSwitchDetector` | `EvlFixtureSetup` |
|----|----------------------|----------------------|-------------------|
| Ubuntu standard | ✅ Pass | ⏭ Skip (no EVL) | ⏭ Skip (no EVL) |
| PREEMPT_RT | ✅ Pass | ⏭ Skip (no EVL) | ⏭ Skip (no EVL) |
| Xenomai EVL (sans root) | ✅ Pass | ⏭ Skip (SCHED_FIFO fail) | ⏭ Skip |
| Xenomai EVL (avec root) | ✅ Pass | ✅ Pass | ✅ Pass |

Sur Xenomai avec root, le test `write_syscall_triggers_mode_switch` doit
**afficher un EXPECT_EQ failure** (mode switch détecté sur `fwrite`) —
c'est le comportement attendu, il valide que le détecteur fonctionne.

---

## Interprétation des logs EVL

### Vérifier que le thread est dans le domaine OOB

```bash
# Pendant l'exécution des tests (autre terminal)
evl ps -t
```

La colonne `STATE` doit afficher `T-OOB` pour les threads de test.

### Compter les mode switches manuellement

```bash
# Remplacer <tid> par le TID du thread de test
cat /proc/evl/threads/<tid>
```

Le champ `nssw` est le compteur de stage switches (mode switches OOB→IB).

---

## Structure des fichiers ajoutés

```
realtime_tools/
├── test/
│   ├── evl_test_helpers.hpp        # Phase 1 : infrastructure RT
│   ├── evl_environment_tests.cpp   # Phase 1 : validation de l'infrastructure
│   └── ...                         # tests existants (inchangés)
├── CMakeLists.txt                  # modifié : détection EVL + nouvelle cible
└── README_EVL.md                   # ce fichier
```
