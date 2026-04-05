#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <windows.h>

#include "winwmkit.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Interne uniquement.
 *
 * Ce header ne fait PAS partie de l'API publique.
 * Il sert à piloter la boucle d'événements interne de la librairie :
 * - thread de fond
 * - queue MPSC
 * - réception / normalisation des événements Windows
 * - dispatch vers les callbacks publics
 *
 * Idée générale :
 * - plusieurs producteurs peuvent push des événements dans la queue
 * - un seul consommateur lit la queue et appelle les callbacks
 * - start() initialise tout
 * - stop() arrête proprement tout et libère les ressources
 */

typedef enum {
  WWMK_INTERNAL_EVENT_NONE = 0,

  WWMK_INTERNAL_EVENT_WINDOW_CREATED,
  WWMK_INTERNAL_EVENT_WINDOW_DESTROYED,
  WWMK_INTERNAL_EVENT_WINDOW_MOVED,
  WWMK_INTERNAL_EVENT_WINDOW_RESIZED,
  WWMK_INTERNAL_EVENT_WINDOW_FOCUSED,

  WWMK_INTERNAL_EVENT_MONITOR_ADDED,
  WWMK_INTERNAL_EVENT_MONITOR_REMOVED,
  WWMK_INTERNAL_EVENT_MONITOR_UPDATED,

  WWMK_INTERNAL_EVENT_QUIT
} WWMK_InternalEventType;

typedef struct {
  WWMK_InternalEventType type;

  uintptr_t window_id;
  uintptr_t monitor_id;

  /*
   * Optionnel selon l'événement.
   * Utile pour éviter de re-query immédiatement dans certains cas.
   */
  WWMK_Rect rect;
} WWMK_InternalEvent;

/*
 * Callback interne de dispatch.
 *
 * En pratique, cette callback fera le pont entre l'événement interne
 * et la callback publique enregistrée par l'utilisateur.
 */
typedef void (*WWMK_InternalEventCallback)(const WWMK_InternalEvent *event,
                                           void *userdata);

/*
 * Queue MPSC simple :
 * - plusieurs producteurs
 * - un seul consommateur
 *
 * NOTE IMPL:
 * Tu peux commencer avec une queue protégée par CRITICAL_SECTION
 * + CONDITION_VARIABLE ou semaphore.
 * Pas besoin de lock-free tout de suite.
 */
typedef struct {
  WWMK_InternalEvent *events;
  size_t capacity;

  size_t head;
  size_t tail;

  CRITICAL_SECTION lock;
  CONDITION_VARIABLE cv;

  bool closed;
} WWMK_EventQueue;

/*
 * Etat interne de la boucle d'événements.
 *
 * NOTE IMPL:
 * - worker_thread = thread qui attend les signaux / hooks / messages Windows
 * - running = vrai tant que la boucle doit continuer
 * - stop_requested = permet une sortie propre
 * - callback/userdata = cible finale du dispatch
 */
typedef struct {
  HANDLE worker_thread;
  DWORD worker_thread_id;

  bool running;
  bool stop_requested;

  WWMK_EventQueue queue;

  WWMK_InternalEventCallback callback;
  void *callback_userdata;

  /*
   * NOTE IMPL:
   * Si tu utilises WinEvent hooks, stocke-les ici.
   * Exemple possible plus tard :
   * HWINEVENTHOOK window_hook;
   * HWINEVENTHOOK focus_hook;
   * etc.
   */
} WWMK_EventLoop;

/*
 * Initialise la queue.
 *
 * NOTE IMPL:
 * - allouer le buffer
 * - capacity > 0 obligatoire
 * - head/tail à 0
 * - init CRITICAL_SECTION + CONDITION_VARIABLE
 */
int wwmk_event_queue_init(WWMK_EventQueue *queue, size_t capacity);

/*
 * Détruit la queue.
 *
 * NOTE IMPL:
 * - libérer le buffer
 * - marquer closed = true
 * - réveiller les éventuels waiters avant destruction si nécessaire
 */
void wwmk_event_queue_destroy(WWMK_EventQueue *queue);

/*
 * Push par un producteur.
 *
 * Retourne 1 si succès, 0 sinon.
 *
 * NOTE IMPL:
 * - refuser si queue fermée
 * - selon ton choix : soit refuser si pleine, soit overwrite, soit grow
 * - au début, "refuser si pleine" est largement suffisant
 */
int wwmk_event_queue_push(WWMK_EventQueue *queue,
                          const WWMK_InternalEvent *event);

/*
 * Pop bloquant pour le consommateur unique.
 *
 * Retourne :
 * - 1 si un événement a été lu
 * - 0 si arrêt / fermeture
 *
 * NOTE IMPL:
 * - attendre sur CONDITION_VARIABLE tant que la queue est vide
 * - sortir si closed == true et plus rien à lire
 */
int wwmk_event_queue_pop_wait(WWMK_EventQueue *queue, WWMK_InternalEvent *out);

/*
 * Pop non bloquant, si jamais tu veux l'utiliser en interne.
 *
 * Retourne 1 si succès, 0 sinon.
 */
int wwmk_event_queue_pop_nowait(WWMK_EventQueue *queue,
                                WWMK_InternalEvent *out);

/*
 * Ferme la queue et réveille le consommateur.
 *
 * NOTE IMPL:
 * - closed = true
 * - WakeAllConditionVariable(&queue->cv)
 */
void wwmk_event_queue_close(WWMK_EventQueue *queue);

/*
 * Initialise l'event loop interne.
 *
 * NOTE IMPL:
 * - memset / état propre
 * - init de la queue
 * - enregistrer callback + userdata
 * - ne démarre PAS encore le thread ici si tu veux séparer init/start
 */
int wwmk_event_loop_init(WWMK_EventLoop *loop, size_t queue_capacity,
                         WWMK_InternalEventCallback callback, void *userdata);

/*
 * Détruit l'event loop interne.
 *
 * NOTE IMPL:
 * - appeler stop si nécessaire
 * - détruire la queue
 * - nettoyer tous les handles/hooks restants
 */
void wwmk_event_loop_destroy(WWMK_EventLoop *loop);

/*
 * Démarre la boucle.
 *
 * NOTE IMPL IMPORTANT:
 * - créer le thread worker
 * - installer les hooks / mécanismes Windows nécessaires
 * - marquer running = true seulement si tout a réussi
 * - éviter les doubles start()
 */
int wwmk_event_loop_start(WWMK_EventLoop *loop);

/*
 * Demande l'arrêt et attend la fin du thread.
 *
 * NOTE IMPL IMPORTANT:
 * - stop_requested = true
 * - push un event QUIT ou réveiller proprement le thread
 * - désinstaller les hooks Windows
 * - attendre Join/WaitForSingleObject(worker_thread, ...)
 * - fermer le handle du thread
 * - running = false en fin de parcours
 *
 * Très important :
 * stop() doit être idempotent ou au moins tolérant à un double appel.
 */
int wwmk_event_loop_stop(WWMK_EventLoop *loop);

/*
 * Fonction du thread worker.
 *
 * NOTE IMPL:
 * - boucle principale interne
 * - récupère les événements source Windows
 * - les transforme en WWMK_InternalEvent
 * - les push dans la queue
 * - le consommateur unique peut être ce même thread, ou un second modèle
 *
 * Pour commencer simple, tu peux faire :
 *   thread unique = reçoit Windows + dispatch directement après normalisation
 * Mais si tu veux garder la vraie queue MPSC, ce point peut évoluer.
 */
DWORD WINAPI wwmk_event_loop_thread_main(LPVOID arg);

/*
 * Transforme un événement interne en événement public.
 *
 * NOTE IMPL:
 * - mapping InternalEvent -> WWMK_Event
 * - ne pas exposer de détails internes
 */
int wwmk_event_loop_translate_event(const WWMK_InternalEvent *internal_event,
                                    WWMK_Event *public_event);

/*
 * Dispatch final vers la callback publique utilisateur.
 *
 * NOTE IMPL:
 * - traduire d'abord l'événement interne
 * - ignorer proprement ceux qui ne doivent pas sortir publiquement
 * - appeler la callback enregistrée si elle existe
 */
void wwmk_event_loop_dispatch(WWMK_EventLoop *loop,
                              const WWMK_InternalEvent *event);

#ifdef __cplusplus
}
#endif
