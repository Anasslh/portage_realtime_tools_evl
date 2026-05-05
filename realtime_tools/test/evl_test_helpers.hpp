// Copyright 2026 ICube Laboratory, University of Strasbourg
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// ─────────────────────────────────────────────────────────────────────────────
// PHASE 1 — EVL/Xenomai RT test infrastructure
//
// Ce fichier fournit quatre services indépendants composables :
//
//   1. Détection runtime de Xenomai/EVL         → is_evl_available()
//   2. Setup de l'environnement RT              → setup_rt_environment()
//   3. Comptage de mode switches OOB→IB         → ASSERT_NO_MODE_SWITCH({ })
//   4. Fixture GTest prête à l'emploi           → EvlTestFixture
//
// Flags CMake attendus (définis par CMakeLists.txt) :
//   HAVE_EVL           → libevl trouvée + kernel EVL présent à la compilation
//   HAVE_EVL_GET_STATS → evl_get_stats() disponible dans la version installée
//                        (API moderne, libevl ≥ r33)
//                        Si absent, on bascule sur le fallback procfs.
// ─────────────────────────────────────────────────────────────────────────────

#ifndef REALTIME_TOOLS__EVL_TEST_HELPERS_HPP_
#define REALTIME_TOOLS__EVL_TEST_HELPERS_HPP_

#include <pthread.h>
#include <sys/mman.h>     // mlockall / munlockall
#include <sys/syscall.h>  // SYS_gettid
#include <unistd.h>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

// ── Includes EVL conditionnels ───────────────────────────────────────────────
// HAVE_EVL est défini par CMakeLists.txt si et seulement si libevl est trouvée.
// Sans ce flag le fichier compile normalement et tous les tests EVL se sautent.
#ifdef HAVE_EVL
#include <evl/evl.h>     // evl_init()
#include <evl/thread.h>  // evl_attach_self(), evl_detach_self()
                         // evl_get_stats() si HAVE_EVL_GET_STATS
#endif

namespace realtime_tools::test
{

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 1 — Détection runtime
// ─────────────────────────────────────────────────────────────────────────────
//
// Pourquoi une détection runtime en plus du flag compile-time ?
//
// HAVE_EVL=1 signifie que libevl était présente lors de la COMPILATION.
// Mais si le binaire est exécuté sur une machine dont le kernel n'a pas le
// module EVL chargé (ou pas de kernel EVL du tout), evl_init() échoue.
// La détection runtime rend le même binaire portable : même test exécutable
// sur Ubuntu standard (skip), PREEMPT_RT (skip), et Xenomai (run).

inline bool is_evl_available()
{
#ifdef HAVE_EVL
  // evl_init() est idempotente : sans effet si déjà appelée.
  // Retourne 0 sur succès, errno négatif si le module EVL n'est pas chargé.
  // L'appel ouvre /dev/evl/control ; si le device n'existe pas → ENOENT.
  static const int result = evl_init();
  return result >= 0;
#else
  return false;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 2 — Setup de l'environnement RT
// ─────────────────────────────────────────────────────────────────────────────
//
// Ces trois opérations sont absentes des tests existants, ce qui explique
// pourquoi ils passent fonctionnellement mais ne valident pas le comportement
// RT réel dans le domaine EVL.

struct RtSetupResult
{
  bool mlockall_ok;    // pages mémoire verrouillées en RAM
  bool evl_init_ok;    // cœur EVL initialisé
  bool sched_fifo_ok;  // thread appelant en SCHED_FIFO
  std::string message;
};

// Appeler une fois avant les tests RT (dans main() ou SetUpTestSuite()).
// priority : priorité SCHED_FIFO (1-99). 50 est un choix raisonnable.
inline RtSetupResult setup_rt_environment(int priority = 50)
{
  RtSetupResult res{false, false, false, ""};

  // ── 2a. mlockall(MCL_CURRENT | MCL_FUTURE) ────────────────────────────────
  // Verrouille TOUTES les pages mémoire du processus en RAM pour la durée de
  // l'exécution. Sans ça, le kernel peut décider de swapper une page pendant
  // qu'un thread RT tourne → page fault → latence spike de 1-10 ms.
  // C'est l'une des sources les plus communes de "ça passe les tests mais spike
  // en prod". MCL_FUTURE couvre aussi les pages allouées après l'appel.
  if (mlockall(MCL_CURRENT | MCL_FUTURE) == 0) {
    res.mlockall_ok = true;
  } else {
    res.message += "mlockall() échoué (besoin CAP_IPC_LOCK ou root) ; ";
  }

  // ── 2b. evl_init() ────────────────────────────────────────────────────────
  // Initialise le cœur EVL en ouvrant /dev/evl/control. DOIT être appelé
  // avant tout autre appel EVL. Si le device n'existe pas, le kernel n'a pas
  // de support EVL malgré la présence de libevl.
#ifdef HAVE_EVL
  const int evl_ret = evl_init();
  if (evl_ret >= 0) {
    res.evl_init_ok = true;
  } else {
    res.message +=
      "evl_init() échoué (errno=" + std::to_string(-evl_ret) + ") ; ";
  }
#else
  res.message += "HAVE_EVL non défini à la compilation ; ";
#endif

  // ── 2c. SCHED_FIFO ────────────────────────────────────────────────────────
  // Sans SCHED_FIFO, le thread de test est soumis au scheduler CFS de Linux.
  // Il peut être préempté à tout instant par un thread non-RT, ce qui invalide
  // toute mesure de latence ou de comportement RT.
  struct sched_param param{};
  param.sched_priority = priority;
  const int sched_ret =
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
  if (sched_ret == 0) {
    res.sched_fifo_ok = true;
  } else {
    res.message +=
      "SCHED_FIFO échoué (errno=" + std::to_string(sched_ret) +
      ", besoin root ou CAP_SYS_NICE) ; ";
  }

  return res;
}

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 3 — Comptage de mode switches
// ─────────────────────────────────────────────────────────────────────────────
//
// QU'EST-CE QU'UN MODE SWITCH sur Xenomai EVL ?
//
// Xenomai EVL implémente un "double scheduler" : le co-kernel EVL tourne au
// niveau de priorité le plus bas du kernel, invisible au scheduler Linux CFS.
// Un thread attaché à EVL avec evl_attach_self() vit dans le domaine OOB
// (out-of-band) : il est géré directement par le co-kernel, sans passer par
// le scheduler Linux.
//
// Problème : quand ce thread EVL appelle un syscall Linux bloquant
// (write, malloc interne, printf, pthread_mutex_lock sur un mutex POSIX...),
// le co-kernel DOIT repasser le thread dans le domaine in-band (IB) pour que
// Linux traite le syscall. Ce passage OOB→IB s'appelle un "stage switch".
//
// Un stage switch = latence non-bornée = la bibliothèque n'est PAS RT-safe.
// C'est exactement le bug invisible que les tests actuels ne détectent pas.
//
// ── Comment lire le compteur ? ────────────────────────────────────────────
//
// Approche A — evl_get_stats() [libevl ≥ r33]
//   Définie dans <evl/thread.h>. Prend le fd EVL du thread (retourné par
//   evl_attach_self()) et remplit un struct evl_thread_stats contenant nssw
//   (number of stage switches). Fait un ioctl sur /dev/evl/threads/<nom>.
//
// Approche B — fallback procfs [toutes versions EVL]
//   Chaque thread attaché à EVL apparaît dans /proc/evl/threads/<tid>.
//   Ce fichier texte contient les compteurs dont "nssw". On le lit et parse.
//   Plus lent (lecture fichier) mais universel. Utilisé si HAVE_EVL_GET_STATS
//   n'est pas défini.

// Stockage thread-local du fd EVL (retourné par evl_attach_self).
// Thread-local : chaque thread a son propre fd EVL, ses propres compteurs.
// inline + thread_local : une seule définition linkée, accessible depuis le header.
inline thread_local int tl_evl_fd = -1;

struct ModeSwitchSnapshot
{
  uint32_t nssw;  // valeur du compteur nssw au moment de la capture
  bool valid;     // false si EVL absent ou thread non attaché
};

// ── Lecture du compteur nssw ─────────────────────────────────────────────────

// Fallback procfs : lit /proc/evl/threads/<tid> et extrait le champ "nssw".
// Le fichier a un format "key: value" sur chaque ligne.
inline uint32_t read_nssw_from_procfs()
{
  const long tid = syscall(SYS_gettid);
  const std::string path = "/proc/evl/threads/" + std::to_string(tid);
  std::ifstream f(path);
  if (!f.is_open()) {
    return 0;
  }
  std::string line;
  while (std::getline(f, line)) {
    if (line.rfind("nssw:", 0) == 0) {
      uint32_t val = 0;
      std::istringstream ss(line.substr(5));
      ss >> val;
      return val;
    }
  }
  return 0;
}

inline ModeSwitchSnapshot capture_mode_switches()
{
#ifdef HAVE_EVL
  if (tl_evl_fd < 0) {
    return {0, false};
  }

#ifdef HAVE_EVL_GET_STATS
  // ── Approche A : evl_get_stats() ────────────────────────────────────────
  // evl_get_stats(efd, &stats) — efd = fd retourné par evl_attach_self().
  // stats.nssw = nombre cumulé de transitions OOB↔IB depuis l'attachement.
  struct evl_thread_stats stats{};
  if (evl_get_stats(tl_evl_fd, &stats) == 0) {
    return {stats.nssw, true};
  }
  // Si evl_get_stats échoue, on bascule sur le fallback.
#endif

  // ── Approche B : fallback procfs ────────────────────────────────────────
  return {read_nssw_from_procfs(), true};

#else
  return {0, false};
#endif
}

inline uint32_t mode_switches_since(const ModeSwitchSnapshot & before)
{
  if (!before.valid) {
    return 0;
  }
  const auto after = capture_mode_switches();
  if (!after.valid) {
    return 0;
  }
  // Soustraction correcte même si le compteur 32-bit déborde (wrap-around).
  return after.nssw - before.nssw;
}

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 4 — Fixture GTest de base pour les tests EVL
// ─────────────────────────────────────────────────────────────────────────────
//
// Usage :
//   class MonTestRT : public realtime_tools::test::EvlTestFixture { ... };
//
// SetUp()     : skip si EVL absent, mlockall, evl_attach_self, SCHED_FIFO
// TearDown()  : evl_detach_self, munlockall

class EvlTestFixture : public ::testing::Test
{
protected:
  void SetUp() override
  {
    // Skip propre sur les systèmes sans EVL — pas d'échec parasite sur Ubuntu.
    if (!is_evl_available()) {
      GTEST_SKIP() << "Xenomai EVL non disponible. "
                   << "Ce test requiert un kernel EVL chargé.";
    }

    // mlockall avant evl_attach pour éviter tout page fault pendant
    // l'attachement lui-même.
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
      std::cerr << "[EvlTestFixture] WARNING: mlockall() échoué. "
                << "Des page faults sont possibles.\n";
    }

#ifdef HAVE_EVL
    // Nom unique : "gtest_<pid>_<tid>".
    // EVL limite les noms à 32 caractères (TASK_COMM_LEN inclus).
    // Utilisation de gettid() (Linux ≥ 2.6) via syscall pour portabilité.
    const long tid = syscall(SYS_gettid);
    const std::string name =
      "gtest_" + std::to_string(::getpid()) + "_" + std::to_string(tid);

    // evl_attach_self() retourne un fd >= 0 sur succès, ou -errno sur échec.
    // Ce fd est le handle du thread dans le VFS EVL (/dev/evl/threads/<nom>).
    // On le stocke en thread_local pour capture_mode_switches().
    tl_evl_fd = evl_attach_self(name.c_str());
    if (tl_evl_fd < 0) {
      GTEST_SKIP()
        << "evl_attach_self() échoué (errno=" << -tl_evl_fd << "). "
        << "Vérifier : kernel EVL chargé, /dev/evl/ accessible, droits suffisants.";
    }

    // SCHED_FIFO après l'attachement EVL (EVL hérite la politique de scheduling).
    struct sched_param param{};
    param.sched_priority = 50;
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) != 0) {
      std::cerr << "[EvlTestFixture] WARNING: SCHED_FIFO échoué. "
                << "Besoin root ou CAP_SYS_NICE.\n";
    }
#endif
  }

  void TearDown() override
  {
#ifdef HAVE_EVL
    if (tl_evl_fd >= 0) {
      evl_detach_self();
      tl_evl_fd = -1;
    }
#endif
    munlockall();
  }
};

}  // namespace realtime_tools::test

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 5 — Macros publiques
// ─────────────────────────────────────────────────────────────────────────────

// ── SKIP_IF_NOT_EVL() ────────────────────────────────────────────────────────
// À placer en première ligne des TEST() qui nécessitent Xenomai.
// Permet une suite de tests unique qui fonctionne sur tous les OS :
//   - Ubuntu standard → skip
//   - PREEMPT_RT      → skip
//   - Xenomai EVL     → run
#define SKIP_IF_NOT_EVL()                                                        \
  do {                                                                           \
    if (!realtime_tools::test::is_evl_available()) {                             \
      GTEST_SKIP() << "Xenomai EVL non disponible sur ce système";               \
    }                                                                            \
  } while (0)

// ── ASSERT_NO_MODE_SWITCH({ ... }) ───────────────────────────────────────────
// Encadre un bloc et vérifie qu'aucun mode switch OOB→IB n'a eu lieu.
// Fait échouer le test si un mode switch est détecté.
//
// Prérequis : le thread courant doit être attaché à EVL
//             (via EvlTestFixture ou un appel direct à evl_attach_self()).
//
// Exemples :
//
//   // Doit passer : opération atomique, pas de syscall Linux
//   ASSERT_NO_MODE_SWITCH({
//     queue.push(42);
//   });
//
//   // Va ÉCHOUER intentionnellement (utile pour valider le détecteur) :
//   ASSERT_NO_MODE_SWITCH({
//     printf("hello\n");  // write() = syscall Linux = mode switch garanti
//   });
#define ASSERT_NO_MODE_SWITCH(block)                                             \
  do {                                                                           \
    const auto _snap = realtime_tools::test::capture_mode_switches();           \
    block                                                                        \
    const uint32_t _delta =                                                      \
      realtime_tools::test::mode_switches_since(_snap);                         \
    ASSERT_EQ(0u, _delta)                                                        \
      << "Mode switch détecté : " << _delta << " transition(s) OOB→IB.\n"       \
      << "Un syscall Linux a été appelé depuis le domaine EVL.\n"                \
      << "→ La section testée N'EST PAS RT-safe sur Xenomai.";                   \
  } while (0)

// ── EXPECT_NO_MODE_SWITCH({ ... }) ───────────────────────────────────────────
// Version non-fatale : affiche un warning mais ne fait pas échouer le test.
// Utile en phase d'investigation pour recenser tous les problèmes d'un coup.
#define EXPECT_NO_MODE_SWITCH(block)                                             \
  do {                                                                           \
    const auto _snap = realtime_tools::test::capture_mode_switches();           \
    block                                                                        \
    const uint32_t _delta =                                                      \
      realtime_tools::test::mode_switches_since(_snap);                         \
    EXPECT_EQ(0u, _delta)                                                        \
      << "Mode switch détecté : " << _delta << " transition(s) OOB→IB.\n"       \
      << "Un syscall Linux a été appelé depuis le domaine EVL.\n"                \
      << "→ La section testée N'EST PAS RT-safe sur Xenomai.";                   \
  } while (0)

#endif  // REALTIME_TOOLS__EVL_TEST_HELPERS_HPP_
