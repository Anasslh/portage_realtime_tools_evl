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
// Tests de validation de Phase 1 — evl_test_helpers.hpp
//
// Ce fichier teste l'INFRASTRUCTURE de test elle-même avant de l'appliquer
// aux composants de realtime_tools. C'est la méthode "test the tests".
//
// Plan des tests :
//
//   Groupe 1 — EnvironmentDetection
//     Vérifie que is_evl_available() et setup_rt_environment() se comportent
//     correctement, avec et sans kernel EVL.
//
//   Groupe 2 — ModeSwitchDetector (nécessite EVL)
//     Valide que le détecteur de mode switches FONCTIONNE :
//       - Un appel non-RT (write vers stderr) DÉCLENCHE une détection
//       - Une computation pure NE DÉCLENCHE PAS de détection
//     Ce groupe est le test le plus important : si ces deux assertions
//     passent, le détecteur est fiable pour les tests des phases suivantes.
//
//   Groupe 3 — EvlFixture (nécessite EVL)
//     Vérifie que la fixture attache correctement le thread et que
//     tl_evl_fd est valide pendant le test.
// ─────────────────────────────────────────────────────────────────────────────

#include <gmock/gmock.h>

#include <atomic>
#include <cstdio>   // fwrite
#include <cstring>  // strlen
#include <thread>
#include <vector>

#include "evl_test_helpers.hpp"

using realtime_tools::test::EvlTestFixture;
using realtime_tools::test::ModeSwitchSnapshot;
using realtime_tools::test::capture_mode_switches;
using realtime_tools::test::is_evl_available;
using realtime_tools::test::mode_switches_since;
using realtime_tools::test::setup_rt_environment;
using realtime_tools::test::tl_evl_fd;

// ─────────────────────────────────────────────────────────────────────────────
// Groupe 1 — EnvironmentDetection
// Ces tests passent sur TOUS les OS (pas de SKIP_IF_NOT_EVL).
// ─────────────────────────────────────────────────────────────────────────────

// Vérifie que is_evl_available() retourne un booléen cohérent.
// Sur Ubuntu standard → false. Sur Xenomai → true.
// Ce test ne peut pas "échouer" car les deux retours sont valides.
TEST(EnvironmentDetection, is_evl_available_returns_bool)
{
  const bool available = is_evl_available();
  // Juste enregistrer le résultat pour avoir l'info dans les logs CI.
  std::cout << "[INFO] is_evl_available() = " << std::boolalpha << available
            << "\n";
  // Appel idempotent : le deuxième appel doit retourner la même valeur.
  EXPECT_EQ(available, is_evl_available());
}

// Vérifie que setup_rt_environment() retourne un résultat structuré.
// Les opérations individuelles peuvent échouer (sans root), mais la fonction
// ne doit jamais crasher ou lancer d'exception.
TEST(EnvironmentDetection, setup_rt_environment_does_not_crash)
{
  const auto res = setup_rt_environment(50);

  std::cout << "[INFO] setup_rt_environment():\n"
            << "  mlockall_ok    = " << std::boolalpha << res.mlockall_ok << "\n"
            << "  evl_init_ok    = " << res.evl_init_ok << "\n"
            << "  sched_fifo_ok  = " << res.sched_fifo_ok << "\n"
            << "  message        = \"" << res.message << "\"\n";

  // Sur Xenomai avec droits root, les trois doivent passer.
  // Sur Ubuntu sans root, certains peuvent échouer — ce n'est pas une erreur
  // de ce test, juste une info pour le diagnostic.
  if (is_evl_available()) {
    // Si EVL est disponible, evl_init doit réussir.
    EXPECT_TRUE(res.evl_init_ok)
      << "evl_init() échoué malgré kernel EVL détecté : " << res.message;
  }
}

// Vérifie que is_evl_available() est thread-safe (pas de race sur l'init).
TEST(EnvironmentDetection, is_evl_available_thread_safe)
{
  constexpr int N = 8;
  std::vector<bool> results(N);
  std::vector<std::thread> threads;

  for (int i = 0; i < N; ++i) {
    threads.emplace_back([&results, i]() {
      results[i] = is_evl_available();
    });
  }
  for (auto & t : threads) {
    t.join();
  }

  // Tous les threads doivent obtenir la même réponse.
  for (int i = 1; i < N; ++i) {
    EXPECT_EQ(results[0], results[i])
      << "is_evl_available() a retourné des valeurs différentes selon le thread";
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Groupe 2 — ModeSwitchDetector (nécessite EVL)
//
// C'est le groupe le plus critique de Phase 1.
// Il valide que le détecteur de mode switches est opérationnel.
//
// Structure : tous les tests héritent de EvlTestFixture qui :
//   - Skip si EVL absent
//   - Attache le thread à EVL (tl_evl_fd valide)
//   - Configure SCHED_FIFO + mlockall
// ─────────────────────────────────────────────────────────────────────────────

class ModeSwitchDetector : public EvlTestFixture
{
};

// ── Test 2.1 : capture retourne un snapshot valide ───────────────────────────
// Si le thread est attaché à EVL, capture_mode_switches() doit retourner
// valid=true. C'est la condition minimale pour que les autres tests aient du sens.
TEST_F(ModeSwitchDetector, capture_returns_valid_snapshot_when_attached)
{
  const auto snap = capture_mode_switches();
  ASSERT_TRUE(snap.valid)
    << "capture_mode_switches() retourne valid=false alors que le thread est "
    << "attaché à EVL (tl_evl_fd=" << tl_evl_fd << "). "
    << "Vérifier que HAVE_EVL_GET_STATS ou le fallback procfs fonctionne.";
}

// ── Test 2.2 : zéro mode switch sur computation pure ─────────────────────────
// Une boucle d'additions entières ne fait aucun syscall Linux.
// Ce test vérifie que le détecteur ne génère pas de faux positifs.
// Si ce test échoue, il y a un bug dans le détecteur lui-même (faux positif).
TEST_F(ModeSwitchDetector, pure_computation_triggers_no_mode_switch)
{
  volatile int64_t acc = 0;

  ASSERT_NO_MODE_SWITCH({
    for (int i = 0; i < 100000; ++i) {
      acc += i;
    }
  });

  (void)acc;  // empêche l'optimisation par le compilateur
}

// ── Test 2.3 : write() déclenche un mode switch ───────────────────────────────
// fwrite vers stderr appelle le syscall write() → passage OOB→IB garanti.
// Ce test vérifie que le détecteur détecte bien les vrais mode switches.
// Si ce test échoue (0 mode switches détectés), le détecteur ne fonctionne
// pas et tous les tests de Phase 2+ seraient des faux négatifs.
//
// Note : on utilise EXPECT (pas ASSERT) pour avoir les deux compteurs affichés.
TEST_F(ModeSwitchDetector, write_syscall_triggers_mode_switch)
{
  const auto snap_before = capture_mode_switches();

  // fwrite → write() = syscall in-band = mode switch garanti depuis OOB.
  const char msg[] = "[EVL mode switch test]\n";
  fwrite(msg, 1, strlen(msg), stderr);

  const uint32_t delta = mode_switches_since(snap_before);

  // On attend AU MOINS 1 mode switch.
  EXPECT_GT(delta, 0u)
    << "PROBLÈME DÉTECTEUR : fwrite() n'a pas provoqué de mode switch.\n"
    << "Le détecteur retourne toujours 0. Causes possibles :\n"
    << "  - evl_get_stats() retourne une erreur silencieuse\n"
    << "  - Le fallback procfs ne trouve pas /proc/evl/threads/<tid>\n"
    << "  - Le thread n'est pas réellement dans le domaine OOB\n"
    << "Vérifier avec : evl ps -t (doit montrer le thread en OOB)";

  std::cout << "[INFO] Mode switches causés par fwrite() : " << delta << "\n";
}

// ── Test 2.4 : opérations atomiques pures — pas de mode switch ───────────────
// Les opérations std::atomic avec memory_order_relaxed utilisent des
// instructions CPU (LOCK XADD, etc.) sans syscall. Doivent être RT-safe.
TEST_F(ModeSwitchDetector, atomic_operations_no_mode_switch)
{
  std::atomic<int> counter{0};

  ASSERT_NO_MODE_SWITCH({
    for (int i = 0; i < 10000; ++i) {
      counter.fetch_add(1, std::memory_order_relaxed);
    }
  });

  EXPECT_EQ(10000, counter.load());
}

// ── Test 2.5 : EXPECT_NO_MODE_SWITCH est non-fatal ───────────────────────────
// Vérifie que EXPECT_NO_MODE_SWITCH ne stoppe pas le test même si un
// mode switch est détecté. Le test doit continuer jusqu'à la fin.
TEST_F(ModeSwitchDetector, expect_variant_is_non_fatal)
{
  bool reached_after_macro = false;

  // Ce bloc VA causer un mode switch (fwrite), mais EXPECT ne stoppe pas.
  EXPECT_NO_MODE_SWITCH({
    const char msg[] = "[test non-fatal]\n";
    fwrite(msg, 1, strlen(msg), stderr);
  });

  // Cette ligne DOIT être atteinte même si le EXPECT_NO_MODE_SWITCH échoue.
  reached_after_macro = true;
  EXPECT_TRUE(reached_after_macro)
    << "EXPECT_NO_MODE_SWITCH a stoppé le test (comportement inattendu, "
    << "il devrait se comporter comme EXPECT_EQ, pas ASSERT_EQ)";
}

// ── Test 2.6 : deux snapshots consécutifs sans opération ─────────────────────
// mode_switches_since(snap) doit retourner 0 si rien n'a été exécuté entre
// les deux captures. Vérifie la cohérence de la mesure.
TEST_F(ModeSwitchDetector, two_captures_without_operation_give_zero)
{
  const auto snap1 = capture_mode_switches();
  const auto snap2 = capture_mode_switches();

  ASSERT_TRUE(snap1.valid);
  ASSERT_TRUE(snap2.valid);
  EXPECT_EQ(0u, snap2.nssw - snap1.nssw)
    << "Deux captures consécutives sans aucune opération donnent un delta "
    << "non-nul (" << (snap2.nssw - snap1.nssw) << "). "
    << "Problème possible : capture_mode_switches() elle-même fait un mode switch.";
}

// ─────────────────────────────────────────────────────────────────────────────
// Groupe 3 — EvlFixture
// Vérifie que la fixture elle-même est correctement configurée.
// ─────────────────────────────────────────────────────────────────────────────

class EvlFixtureSetup : public EvlTestFixture
{
};

// Vérifie que le fd EVL est valide pendant le test (SetUp a réussi).
TEST_F(EvlFixtureSetup, evl_fd_is_valid_during_test)
{
  EXPECT_GE(tl_evl_fd, 0)
    << "tl_evl_fd est invalide dans le test alors que SetUp n'a pas skip. "
    << "Bug dans EvlTestFixture::SetUp().";
}

// Vérifie que SCHED_FIFO est actif sur le thread de test.
TEST_F(EvlFixtureSetup, thread_is_sched_fifo)
{
  int policy = 0;
  struct sched_param param{};
  ASSERT_EQ(0, pthread_getschedparam(pthread_self(), &policy, &param));
  EXPECT_EQ(SCHED_FIFO, policy)
    << "Le thread de test n'est pas en SCHED_FIFO. "
    << "setup_rt_environment() a peut-être échoué (besoin root ?).";
  EXPECT_EQ(50, param.sched_priority);
}

// Vérifie que le thread apparaît dans /proc/evl/threads/ (indépendant de l'API).
TEST_F(EvlFixtureSetup, thread_visible_in_evl_procfs)
{
  const long tid = syscall(SYS_gettid);
  const std::string path = "/proc/evl/threads/" + std::to_string(tid);
  std::ifstream f(path);
  EXPECT_TRUE(f.is_open())
    << "Le thread n'apparaît pas dans /proc/evl/threads/" << tid << ".\n"
    << "Soit l'attachement EVL a échoué silencieusement, "
    << "soit le procfs EVL n'est pas monté.";
}

// ─────────────────────────────────────────────────────────────────────────────
// main()
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char ** argv)
{
  testing::InitGoogleTest(&argc, argv);

  // setup_rt_environment() une fois pour le processus de test principal.
  // Les threads créés par la fixture feront leur propre setup.
  const auto env = setup_rt_environment(50);
  std::cout << "[SETUP] RT environment:\n"
            << "  mlockall  : " << (env.mlockall_ok ? "OK" : "FAILED") << "\n"
            << "  evl_init  : " << (env.evl_init_ok ? "OK" : "FAILED") << "\n"
            << "  sched_fifo: " << (env.sched_fifo_ok ? "OK" : "FAILED") << "\n";
  if (!env.message.empty()) {
    std::cout << "  warnings  : " << env.message << "\n";
  }

  return RUN_ALL_TESTS();
}
