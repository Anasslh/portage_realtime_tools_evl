# CONTEXT.md — Projet RTtoolsROS2_for_ethercat

> Ce fichier est destiné à donner le contexte complet du projet à Claude Code.
> Il a été généré à partir des rapports, slides et documents du projet RIO 2026.

---

## 1. Présentation générale

Ce projet est un **projet ingénieur RIO** réalisé à **Télécom Physique Strasbourg** (TPS),
en lien avec le laboratoire **ICube**, équipe **IRIS** (Robotique et Innovation pour la Santé),
à l'IHU (Institut Hospitalo-Universitaire) de Strasbourg.

**Tuteurs client :** Manuel YGUEL (ingénieur de recherche ICube, docteur en IA/Robotique),
Thibault POIGNONEC  
**Tuteur école :** Quentin BRAMAS  
**Étudiants :**
- Luc MONTIER — responsable OS, noyaux RT, ROS 2
- Mathieu PAILHÉ — responsable tests, protocole expérimental, documentation
- Anass LISSANE EL HAQ — responsable vérification comportement RT, analyse des latences

**Dépôt GitHub du projet :**  
https://github.com/Fenrisul-fr/RTtoolsROS2_for_ethercat

---

## 2. Contexte technique

### 2.1 Pourquoi ce projet ?

L'équipe IRIS a développé la bibliothèque **`ethercat_driver_ros2`**, un pilote permettant
d'intégrer EtherCAT à ROS 2 via `ros2_control` et la pile **IgH EtherCAT Master**.

Dans les applications robotiques médicales, la **stabilité temporelle**, la **latence** et le
**déterminisme** sont critiques. Le projet vise à fournir des outils de mesure, d'analyse et
de comparaison pour caractériser rigoureusement les performances temporelles de cette
bibliothèque sur différentes configurations logicielles et matérielles.

### 2.2 Architecture générale du système

```
Matériel (capteurs, moteurs, esclaves EtherCAT)
        ↕
Pile IgH EtherCAT (espace noyau Linux)
        ↕
ethercat_driver_ros2   ←→   ROS 2 Control
                                  ↕
                            Contrôleurs ROS 2
```

- **EtherCAT** : protocole Ethernet industriel maître-esclave, un seul paquet traverse tous
  les esclaves "à la volée" → latence minimale.
- **IgH EtherCAT Master** : implémentation open-source fonctionnant dans l'espace noyau.
- **`ethercat_driver_ros2`** : interface entre ROS 2 (espace utilisateur) et la pile IgH (noyau).
- **`ros2_control`** : gestion des actionneurs/capteurs du robot, architecture modulaire.
- **`realtime_tools`** : package ROS 2 fournissant des structures RT-safe (lock-free) pour
  les échanges entre threads RT et non-RT.

---

## 3. Objectifs du projet

### 3.1 Objectifs principaux (promo 2026)

Ce projet reprend et complète le travail de la promo 2025.

1. **Mise en place des environnements temps réel :**
   - PC sous Ubuntu 24.04 avec PREEMPT_RT / Xenomai 4 (EVL) + ROS 2 Jazzy
   - Cartes SD pour Raspberry Pi 4 & 5 avec PREEMPT_RT / Xenomai 4 + ROS 2 Jazzy

2. **Développement et exécution de tests temps réel** via `realtime_tools` (ROS 2) pour mesurer :
   - Latence (min / moyenne / max / écart-type)
   - Jitter (histogramme de distribution)
   - Stabilité de période
   - Fréquence maximale atteignable
   - Synchronisation temporelle

3. **Portage et évaluation** des Real-Time Tools POSIX de ROS 2 sur tous les OS cibles.

4. **Production d'images système de référence** reproductibles.

5. **Documentation complète** via GitHub + Sphinx.

6. **Benchmarks comparatifs** sur différentes architectures.

### 3.2 Livrables attendus

- Images système de référence (Ubuntu PREEMPT_RT, Xenomai 4/EVL) pour PC x86_64,
  Raspberry Pi 4, Raspberry Pi 5
- Documentation Sphinx reproductible (installation, validation, tests)
- Suite de benchmarks (latence, jitter, stabilité, fréquence max, synchro horloge)
- Analyseur de trames EtherCAT (lecture `.pcap`, décodage, export CSV, rapport auto)

---

## 4. OS et architectures cibles

| OS / Noyau         | Architecture(s)           | Statut (fin promo 2025) |
|--------------------|---------------------------|--------------------------|
| Ubuntu standard    | x86_64                    | ✅ Référence de base      |
| Ubuntu PREEMPT_RT  | x86_64                    | ✅ Fonctionnel            |
| Xenomai 4 / EVL    | x86_64                    | ✅ Fonctionnel + mesuré   |
| Xenomai 4 / EVL    | Raspberry Pi 4 (ARM64)    | ✅ Fonctionnel            |
| PREEMPT_RT         | Raspberry Pi 4 (ARM64)    | ⚠️ Difficile (pas officiel Ubuntu Pro sur ARM) |
| Xenomai 4 / EVL    | Raspberry Pi 5 (ARM64)    | 🔄 En cours (compilation OK, boot instable) |

**Note importante :** Ubuntu Pro ne propose PREEMPT_RT officiel que pour x86_64.
Sur Raspberry Pi, l'approche retenue est Xenomai 4 / EVL (patch manuel ou linux-evl).

---

## 5. Package `realtime_tools` — Tests existants

### 5.1 Tests unitaires déjà implémentés

Ces tests vérifient les **fonctions du package** `realtime_tools`, pas les performances RT
(pas de mesure de latence ou de délais).

| Test                | Description |
|---------------------|-------------|
| `realtime_publisher`  | Vérifie la communication publisher/subscriber |
| `realtime_buffer`     | Test lecture/écriture concurrente entre threads |
| `clock`               | Vérifie la cohérence de l'écoulement du temps |
| `priority`            | Test de modification de priorité des threads |
| `mutex`               | Vérifie les verrous mutex temps réel |
| `lock_free_queue`     | Test FIFO et absence de blocage producteurs/consommateurs |
| `server_goal_handle`  | Gestion des callbacks temps réel (remplace ROS 2) |
| `thread_box`          | Structure pour accélérer l'accès aux données des threads |
| `async_thread`        | Exécution de fonctions dans des threads non RT |

### 5.2 Résultats obtenus

- **Ubuntu standard** : 89% tests passés (1 échec sur 9) — 26.50 sec
- **PREEMPT_RT** : 100% tests passés (0 échec sur 9) — 19.64 sec

### 5.3 Ce qui manque / à faire (objectifs R3)

- Porter les tests sur Raspberry Pi 4
- Comparer les résultats entre PC et RPI4
- Mettre en place des **programmes de test RT Tools ROS 2** mesurant :
  - la latence, le jitter, la stabilité de période
  - la fréquence maximale atteignable
  - la synchronisation temporelle
- Ces tests doivent tourner sur **toutes les architectures** (x86_64, RPi4, RPi5)

---

## 6. Outils de mesure utilisés

| Outil          | Usage |
|----------------|-------|
| `cyclictest`   | Mesure latence de réveil de tâches périodiques (PREEMPT_RT) |
| `latmus`       | Mesure des latences EVL/Xenomai 4, génère histogrammes |
| `stress-ng`    | Charge artificielle système (CPU, mémoire, I/O, disque) |
| `evl ps`       | Visualisation état des threads EVL |
| `evl trace`    | Capture détaillée lors de pics de latence |
| `wireshark`    | Capture de trames EtherCAT (fichiers `.pcap`) |
| Python/matplotlib | Scripts de post-traitement et visualisation des résultats |

### Méthodologie de mesure latmus (sur Intel x86_64)

1. Calibration : `latmus -t`
2. Mesures périodiques à **1 kHz** pendant 2 à 5 minutes
3. Génération d'histogramme des latences
4. Stress système parallèle avec `stress-ng` (CPU, E/S, mémoire, disque)
5. Enregistrement dans fichier texte + analyse Python/matplotlib

### Métriques collectées

- Nombre total d'échantillons
- Latence moyenne / min / max
- Nombre de « spikes » au-dessus d'un seuil (ex : 50 µs)
- Histogramme de distribution du jitter
- Nombre de dépassements d'échéance
- Fréquence de boucle effective vs théorique

---

## 7. Résultats clés (promo 2025, hérités)

### PC Intel x86_64

- PREEMPT_RT et EVL : latences moyennes du même ordre de grandeur sans charge extrême
- Sous charge (`stress-ng`) : EVL conserve un comportement plus déterministe (pire cas plus serré)
- Latences max observées : objectif < 50 µs sous charge contrôlée

### Raspberry Pi 4

- EVL fonctionnel (noyau BCM2711, libevl installée)
- Latences restent courtes et globalement stables même sous charge
- La valeur max sous charge dépasse la valeur sans charge de seulement ~3 µs
- La séparation EVL / Linux standard maintient le monde non-RT sans compromettre EVL

### Linux standard (sans noyau RT)

- Pics de latence réguliers atteignant **5 à 10 ms** selon config et charge
- Comportement non déterministe confirmé par `cyclictest`

---

## 8. Analyseur EtherCAT (développé promo 2025)

- Lit des fichiers de capture `.pcap`
- Filtre les trames EtherCAT
- Décode les datagrammes (commandes, adresses, Working Counter)
- Exporte les résultats en CSV
- Génère automatiquement un rapport structuré (topologie bus, liste esclaves, statistiques)
- Critère : traiter ≥ 10⁵ trames EtherCAT en < 30 s, 0% de trames valides mal décodées

---

## 9. Documentation

La documentation est produite avec **Sphinx** et hébergée sur GitHub.

- Couvre : installation OS, compilation noyau EVL, validation, scripts de test, collecte métriques
- Objectif : une personne extérieure doit pouvoir installer Xenomai 4 sur une machine de
  test en suivant uniquement la documentation, en moins d'une demi-journée, avec un taux
  de réussite de 100% sur au moins deux plateformes.

---

## 10. Références importantes

| Ressource | Lien |
|-----------|------|
| ethercat_driver_ros2 (code) | https://github.com/ICube-Robotics/ethercat_driver_ros2 |
| ethercat_driver_ros2 (doc) | https://icube-robotics.github.io/ethercat_driver_ros2/ |
| ROS 2 Jazzy | https://docs.ros.org/en/rolling/Releases/Release-Jazzy-Jalisco.html |
| ROS 2 Real-Time Programming | https://docs.ros.org/en/jazzy/Tutorials/Demos/Real-Time-Programming.html |
| IgH EtherCAT | https://etherlab.org/en_GB/ethercat |
| Xenomai / EVL | https://gitlab.denx.de/Xenomai/xenomai/wikis/Setting_Up |
| Dépôt fork projet | https://github.com/Fenrisul-fr/RTtoolsROS2_for_ethercat |

---

## 11. Priorités actuelles (Revue R3 — en cours)

1. Avoir une image Xenomai 4 et PREEMPT_RT stable pour RPI4
2. Porter les tests existants sur RPI4
3. Comparer les résultats entre PC et RPI4
4. Mettre en place des programmes de test RT Tools ROS 2 mesurant latence, jitter,
   stabilité de période, fréquence max atteignable, synchronisation temporelle
5. (Bonus) Stabiliser EVL sur Raspberry Pi 5
6. (Bonus/suite) Intégrer `ethercat_driver_ros2` dans la chaîne de tests complète

---

*Fichier généré depuis les documents du projet (rapports mensuels jan/fév 2026, rapport final, slides R1/R2).*
