# Portage de `realtime_tools` vers Xenomai/EVL — Contexte, spécifications et plan de travail

> Document de travail pour Claude Code.  
> Projet Ingénieur RIO 2026 — Télécom Physique Strasbourg  
> Dernière mise à jour : juin 2026

---

## 0. Instructions pour Claude Code — par où commencer

**Avant d'écrire une seule ligne de code, exécute ces étapes dans l'ordre :**

### 0.1 Auditer l'état du dépôt git

```bash
# 1. Lister les branches existantes
git branch -a

# 2. Vérifier les modifications en cours
git status
git diff --stat

# 3. Regarder les derniers commits pour comprendre ce qui a déjà été fait
git log --oneline -20

# 4. Vérifier si une structure de backend existe déjà
find . -name "rt_mutex*" -o -name "rt_thread*" -o -name "rt_backend*" 2>/dev/null
find . -path "*/backends/*" 2>/dev/null
grep -r "RT_BACKEND\|evl_attach\|evl_mutex\|rt::mutex\|rt::thread_attach" --include="*.hpp" --include="*.cpp" --include="*.cmake" . 2>/dev/null
```

### 0.2 Vérifier que le point de départ est sain

```bash
# Les tests existants doivent passer AVANT toute modification
colcon test --packages-select realtime_tools
# ou si pas dans un workspace ROS :
mkdir -p build && cd build && cmake .. && make && ctest -V
```

### 0.3 Comparer l'état réel avec l'architecture cible (section 3)

Après l'audit, détermine :
- Ce qui est **déjà fait** (structure partielle, headers existants…)
- Ce qui **manque** par rapport à l'architecture cible
- S'il y a des **conflits ou incohérences** entre ce qui existe et ce document

Produis un rapport d'audit court avant de commencer à coder, sous cette forme :
```
AUDIT GIT :
- Branche courante : ...
- Derniers commits : ...
- Déjà implémenté : ...
- Manquant par rapport au plan : ...
- Incohérences détectées : ...
→ Prochaine action : ...
```

---

## 1. Contexte du projet

Le projet vise à porter la stack ROS 2 temps réel (`ros2_control` + `realtime_tools`) sur trois
noyaux cibles : **Ubuntu PREEMPT_RT**, **Xenomai 4/EVL** et à terme **QNX**, sur architectures
x86_64 et Raspberry Pi 4/5.

`realtime_tools` est le package ros-controls qui fournit les primitives de communication
inter-thread temps réel utilisées par `ros2_control` :
- `RealtimePublisher<T>` — publication ROS 2 depuis un thread RT
- `RealtimeBuffer<T>` — échange double-buffer RT/non-RT sans verrou
- `RealtimeThreadSafeBox<T>` — boîte thread-safe paramétrable
- `AsyncFunctionHandler` — exécution asynchrone depuis contexte RT
- `realtime_helpers` — helpers système (priorité, affinité, verrouillage mémoire)

---

## 2. Décisions architecturales — NE PAS REMETTRE EN QUESTION

Ces décisions ont été arrêtées après analyse et ne doivent pas être remises en cause :

### 2.1 L'API publique ne change pas

Les classes publiques (`RealtimePublisher`, `RealtimeBuffer`, `RealtimeThreadSafeBox`) et
leurs méthodes (`try_publish()`, `readFromRT()`, `writeFromNonRT()`…) restent **strictement
inchangées**. Un utilisateur de `ros2_control` ne modifie rien dans son code.

### 2.2 Pas de `#ifdef RT_BACKEND_XENOMAI` dans les headers publics

Les `#if defined(__linux__)` existants dans `mutex.hpp` sont acceptables car ils gèrent des
différences de libc standard (Linux vs macOS). En revanche, `#if defined(__XENOMAI__)` dans
un header public est **interdit** — la détection est fragile et pollue le code pour tous les
utilisateurs sans Xenomai.

### 2.3 Sélection du backend par include path CMake

Le choix du backend (posix ou xenomai) se fait **uniquement au niveau CMake**, en ajoutant
le bon répertoire dans `target_include_directories`. Les headers publics ne voient jamais
quelle implémentation est sélectionnée.

```cmake
# CMakeLists.txt
option(RT_BACKEND "RT backend to use: posix (default) or xenomai" "posix")

if(RT_BACKEND STREQUAL "xenomai")
  target_include_directories(realtime_tools PUBLIC
    include/backends/xenomai
  )
  target_link_libraries(realtime_tools PUBLIC evl)
else()
  target_include_directories(realtime_tools PUBLIC
    include/backends/posix
  )
endif()
```

### 2.4 Pourquoi cette approche (rappel)

Sur Xenomai 4/EVL, un thread doit être explicitement attaché au noyau EVL via
`evl_attach_self()` avant tout accès à une primitive EVL. Sans cet appel :
- Tout `evl_lock_mutex()` échoue avec `EPERM`
- Un `pthread_mutex_lock()` depuis un thread EVL peut déclencher un *stage switch*
  (sortie involontaire du domaine temps réel)

Ce n'est pas un simple remplacement d'appel — c'est une différence de cycle de vie du thread.
La couche d'abstraction `rt_backend` encapsule cette différence proprement.

---

## 3. Architecture cible complète

```
realtime_tools/
├── include/
│   ├── realtime_tools/                    ← API PUBLIQUE — ne pas modifier les signatures
│   │   ├── mutex.hpp                      ← inclut backends/posix/rt_mutex.hpp ou backends/xenomai/rt_mutex.hpp
│   │   ├── realtime_publisher.hpp         ← inchangé (ajouter rt::thread_attach() dans publishingLoop)
│   │   ├── realtime_buffer.hpp            ← inchangé
│   │   ├── realtime_thread_safe_box.hpp   ← inchangé (déjà template sur MutexT)
│   │   ├── async_function_handler.hpp     ← inchangé (ajouter rt::thread_attach() dans le lambda)
│   │   ├── realtime_helpers.hpp           ← inchangé (délègue à rt_configure)
│   │   └── lock_free_queue.hpp            ← inchangé (pas de primitives RT)
│   │
│   └── backends/
│       ├── posix/                         ← backend Linux/PREEMPT_RT
│       │   ├── rt_mutex.hpp               ← pthread_mutex_t + PRIO_INHERIT (code actuel de mutex.hpp)
│       │   ├── rt_thread.hpp              ← thread_attach() = no-op
│       │   ├── rt_configure.hpp           ← configure_realtime() = SCHED_FIFO + mlockall
│       │   └── rt_condition.hpp           ← std::condition_variable wrapper
│       │
│       └── xenomai/                       ← backend Xenomai 4/EVL
│           ├── rt_mutex.hpp               ← evl_mutex wrapper (BasicLockable)
│           ├── rt_thread.hpp              ← thread_attach() = evl_attach_self()
│           ├── rt_configure.hpp           ← configure_realtime() = politique EVL + mlockall
│           └── rt_condition.hpp           ← evl_wait_flags ou std::condition_variable si suffisant
│
├── src/
│   └── realtime_helpers.cpp               ← délègue à rt_configure.hpp du backend actif
│
└── CMakeLists.txt                         ← option RT_BACKEND, configure include path
```

**Règle fondamentale :** `mutex.hpp` public fait juste :
```cpp
#include <backends/posix/rt_mutex.hpp>   // résolu vers posix/ ou xenomai/ par CMake
using prio_inherit_mutex = rt::mutex;    // alias public inchangé
```

---

## 4. Spécifications des primitives backend

### 4.1 `rt::thread_attach()` — PRIORITÉ 1 (commencer ici)

Fichiers : `backends/posix/rt_thread.hpp` et `backends/xenomai/rt_thread.hpp`

```cpp
namespace rt {

/**
 * Attache le thread appelant au domaine temps réel.
 * Doit être appelé au tout début de la fonction du thread, avant tout lock.
 *
 * - posix    : no-op, retourne true immédiatement
 * - xenomai  : appelle evl_attach_self(name.c_str()), retourne false si échec
 *
 * Idempotent : un double appel ne doit pas planter.
 */
bool thread_attach(const std::string& name = "");

} // namespace rt
```

Points d'injection dans `realtime_tools` :
- **Première ligne** de `RealtimePublisher::publishingLoop()`
- **Première ligne** du lambda de thread dans `AsyncFunctionHandler`

Validation : le thread démarre, aucun EPERM, `evl_get_self()` retourne un fd valide (Xenomai).

---

### 4.2 `rt::mutex` — PRIORITÉ 2

Fichiers : `backends/posix/rt_mutex.hpp` et `backends/xenomai/rt_mutex.hpp`

```cpp
namespace rt {

/**
 * Mutex RT-safe, concept C++ BasicLockable.
 * Compatible std::unique_lock<rt::mutex> et std::lock_guard<rt::mutex>.
 *
 * - posix   : pthread_mutex_t + PTHREAD_PRIO_INHERIT + PTHREAD_MUTEX_ROBUST
 *             (déplacer le code actuel de mutex.hpp ici)
 * - xenomai : evl_mutex wrappé, appelable uniquement depuis thread attaché
 *
 * Garanties communes :
 *   - Priority inheritance
 *   - try_lock() strictement non-bloquant (chemin RT dans RealtimeBuffer)
 *   - Utilisable depuis thread RT et non-RT
 */
class mutex {
public:
  mutex();
  ~mutex();
  mutex(const mutex&) = delete;
  mutex& operator=(const mutex&) = delete;

  void lock();
  void unlock() noexcept;
  bool try_lock();

  using native_handle_type = /* pthread_mutex_t* ou evl_mutex* */;
  native_handle_type native_handle();
};

} // namespace rt
```

Après implémentation, mettre à jour `mutex.hpp` public :
```cpp
#include <rt_backend/rt_mutex.hpp>
namespace realtime_tools {
  using prio_inherit_mutex = rt::mutex;          // alias inchangé
  using prio_inherit_recursive_mutex = rt::mutex; // à adapter si nécessaire
}
```

---

### 4.3 `rt::configure_realtime()` — PRIORITÉ 3

Fichiers : `backends/posix/rt_configure.hpp` et `backends/xenomai/rt_configure.hpp`

```cpp
namespace rt {

// posix   : sched_setscheduler(SCHED_FIFO, priority) + mlockall
// xenomai : politique EVL + mlockall
std::pair<bool, std::string> configure_realtime(int priority);

// commune aux deux backends — mlockall(MCL_CURRENT | MCL_FUTURE)
std::pair<bool, std::string> lock_memory();

} // namespace rt
```

`realtime_helpers.cpp` délègue vers ces fonctions. Les signatures publiques de
`realtime_helpers.hpp` ne changent pas.

---

### 4.4 `rt::condition_variable` — PRIORITÉ 4 (implémenter en dernier)

```cpp
namespace rt {
class condition_variable {
public:
  void notify_one() noexcept;
  void notify_all() noexcept;

  template<typename Predicate>
  void wait(std::unique_lock<rt::mutex>& lock, Predicate pred);
};
} // namespace rt
```

**Important :** tester d'abord sur Xenomai si `std::condition_variable` couplée à un
`std::mutex` ordinaire fonctionne pour les threads non-RT de `RealtimePublisher`.
N'implémenter le wrapper EVL que si les tests échouent.

---

## 5. Plan de travail — étapes dans l'ordre

```
ÉTAPE 0 — Audit (avant tout)
  → git log, git status, find pour voir ce qui existe déjà
  → Lancer les tests existants, vérifier qu'ils passent
  → Produire le rapport d'audit (voir section 0.3)

ÉTAPE 1 — Structure CMake
  → Ajouter option RT_BACKEND dans CMakeLists.txt
  → Créer les répertoires include/backends/posix/ et include/backends/xenomai/
  → Vérifier que ça compile toujours avec RT_BACKEND=posix (aucun changement fonctionnel)
  → Les tests existants doivent toujours passer

ÉTAPE 2 — rt::thread_attach() — backend posix
  → Écrire backends/posix/rt_thread.hpp (no-op)
  → Injecter dans publishingLoop() et AsyncFunctionHandler
  → Tests existants toujours verts

ÉTAPE 3 — rt::thread_attach() — backend xenomai
  → Écrire backends/xenomai/rt_thread.hpp (evl_attach_self)
  → Compiler avec RT_BACKEND=xenomai sur la machine Xenomai
  → Valider : thread démarre sans EPERM

ÉTAPE 4 — rt::mutex — backend posix
  → Déplacer le code de detail::mutex depuis mutex.hpp vers backends/posix/rt_mutex.hpp
  → mutex.hpp public devient un alias vers rt::mutex
  → Tests existants toujours verts (comportement identique)

ÉTAPE 5 — rt::mutex — backend xenomai
  → Écrire backends/xenomai/rt_mutex.hpp avec evl_mutex
  → Valider : lock/unlock depuis non-RT, try_lock depuis RT, aucun stage switch

ÉTAPE 6 — rt::configure_realtime()
  → Extraire realtime_helpers vers les deux backends
  → Valider : thread promu RT, latences mesurables (latmus / cyclictest)

ÉTAPE 7 — rt::condition_variable
  → Tester std::condition_variable sur Xenomai côté non-RT
  → Implémenter wrapper EVL si nécessaire

ÉTAPE 8 — Tests d'intégration
  → Tous les tests unitaires passent sur les deux backends
  → Benchmark latence PREEMPT_RT vs EVL
  → Valider sur Raspberry Pi 4
```

---

## 6. Règles de validation — ne passer à l'étape suivante que si

- [ ] Les tests unitaires existants de `realtime_tools` passent toujours
- [ ] Aucun `#ifdef RT_BACKEND` ou `#ifdef __XENOMAI__` dans les headers publics
- [ ] Le backend posix compile et passe les tests sur Ubuntu sans Xenomai installé
- [ ] Chaque primitive testée individuellement avant d'être intégrée dans `realtime_tools`

---

## 7. Points de vigilance

**`evl_mutex` nécessite `thread_attach` préalable** — ne jamais construire un `evl_mutex`
depuis un thread non attaché. Utiliser `evl_get_self()` pour vérifier si nécessaire.

**`try_lock` est en chemin RT** — dans `RealtimeBuffer::readFromRT()`, le `try_lock` est
appelé depuis le thread temps réel. Il doit être **strictement non-bloquant** sur les deux
backends.

**`mlockall` une seule fois** — l'appeler dans `configure_realtime()` uniquement,
pas dans le constructeur de `rt::mutex`.

**Ordre de destruction** — le `evl_mutex` doit être détruit avant que le thread EVL soit
détaché. Vérifier l'ordre de destruction dans `RealtimePublisher`.

**Sur Xenomai, tester avec un vrai noyau EVL** — les erreurs EPERM sont silencieuses
sur un kernel Linux standard sans EVL chargé.