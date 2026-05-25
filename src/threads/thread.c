#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif
 
#define THREAD_MAGIC 0xcd6abf4b
#define A 55
 
static struct list ready_list;
static struct list all_list;
static struct list sleep_list;
static struct thread *idle_thread;
static struct thread *initial_thread;
 
/* 
ALTERAÇÃO-MLFQS 1: variável global load_avg
O load_avg pertence ao sistema inteiro, não a uma
thread. Definida aqui, declarada extern no thread.h.
 */
fixed_point_t load_avg;
 
static struct lock tid_lock;
 
struct kernel_thread_frame 
  {
    void *eip;
    thread_func *function;
    void *aux;
  };
 
static long long idle_ticks;
static long long kernel_ticks;
static long long user_ticks;
 
#define TIME_SLICE 4
static unsigned thread_ticks;
 
bool thread_mlfqs;
 
static void kernel_thread (thread_func *, void *aux);
static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);
 
/* 
ALTERAÇÃO-MLFQS 2: função de comparação de prioridade
Usada por list_insert_ordered e list_max para manter
a ready_list ordenada por prioridade decrescente.
thread_priority(a, b) retorna true se a > b em prio.
 */
bool 
thread_priority (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) 
{
  struct thread *ta = list_entry (a, struct thread, elem);
  struct thread *tb = list_entry (b, struct thread, elem);
  return ta->priority > tb->priority;
}

/* Função auxiliar para comparar o tempo de espera das threads. */
bool 
thread_sleep_less (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
  struct thread *ta = list_entry (a, struct thread, elem);
  struct thread *tb = list_entry (b, struct thread, elem);
  
  return ta->sleep_ticks < tb->sleep_ticks;
}

void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);
 
  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);
  list_init (&sleep_list);
 
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
 
  /* 
  ALTERAÇÃO-MLFQS 3: inicializa load_avg como zero
  Feito aqui porque thread_init() roda uma única vez
  quando o kernel arranca — antes de qualquer thread.
   */
  load_avg = IntToFp(0);
}
 
void
thread_start (void) 
{
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);
  intr_enable ();
  sema_down (&idle_started);
}
 
void
thread_tick (void) 
{
  struct thread *t = thread_current ();
 
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;
 
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}
 
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}
 
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;
 
  ASSERT (function != NULL);
 
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;
 
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();
 
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;
 
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;
 
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;
 
  thread_unblock (t);
 
  return tid;
}
 
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);
 
  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}
 
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;
 
  ASSERT (is_thread (t));
 
  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
 
  /* 
  ALTERAÇÃO-MLFQS 4: list_insert_ordered em vez de push_back
  Insere a thread na posição correta da ready_list
  de acordo com a prioridade (maior prioridade na frente).
  Assim o next_thread_to_run() pode usar pop_front e
  sempre pegar a de maior prioridade.
   */
  list_insert_ordered (&ready_list, &t->elem, thread_priority, NULL);
  t->status = THREAD_READY;
  intr_set_level (old_level);
}
 
const char *
thread_name (void) 
{
  return thread_current ()->name;
}
 
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);
  return t;
}
 
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}
 
void
thread_exit (void) 
{
  ASSERT (!intr_context ());
 
#ifdef USERPROG
  process_exit ();
#endif
 
  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}
 
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());
 
  old_level = intr_disable ();
  if (cur != idle_thread) 
    /* 
    ALTERAÇÃO-MLFQS 5: list_insert_ordered em vez de push_back
    Mesmo motivo da ALTERAÇÃO-MLFQS 4 — mantém a lista ordenada
    quando a thread atual cede a CPU.
     */
    list_insert_ordered (&ready_list, &cur->elem, thread_priority, NULL);
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}
 
void thread_sleep(uint64_t sleep_ticks){
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());
  
  cur->sleep_ticks = sleep_ticks;
 
  old_level = intr_disable ();
  if (cur != idle_thread) 
    list_insert_ordered (&sleep_list, &cur->elem, thread_sleep_less, NULL);
  thread_block();
  intr_set_level (old_level);
}
 
void threads_wakeup(uint64_t allTicks){
  struct list_elem *e;
 
  ASSERT (intr_get_level () == INTR_OFF);
 
  for(e = list_begin(&sleep_list); e != list_end(&sleep_list); ){
    struct thread *t = list_entry(e, struct thread, elem);
    e = list_next(e);
    
    if(allTicks >= t->sleep_ticks){
      list_remove(&t->elem);
      /* 
      ALTERAÇÃO-MLFQS 6: insert_ordered em vez de thread_unblock
      threads_wakeup roda com interrupções desligadas.
      thread_unblock chamaria thread_yield que tem ASSERT
      de interrupções ligadas — causaria crash.
      Solução: insere direto na ready_list ordenada.
       */
      list_insert_ordered (&ready_list, &t->elem, thread_priority, NULL);
      t->status = THREAD_READY;
    } else{
        /*Como as threads estão ordenandas crescentimente 
          Assim que der falso em t.next também será, por isso encerro
          o loop 
        */ 
        break;
      }
  }
}
 
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;
 
  ASSERT (intr_get_level () == INTR_OFF);
 
  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}
 
void
thread_set_priority (int new_priority) 
{
  thread_current ()->priority = new_priority;
}
 
int
thread_get_priority (void) 
{
  return thread_current ()->priority;
}
 
/* 
ALTERAÇÃO-MLFQS 7: funções do MLFQS
Estas 4 funções são o motor do escalonador.
Antes eram stubs vazios com "Not yet implemented".
 */
 
/* Chamada a cada tick — incrementa o balde de CPU da
   thread atual em 1. Ignorada para a idle_thread. */
void
thread_mlfqs_increment_recent_cpu(void)
{
  /* Se for a thread idle, não faz nada */
  if (thread_current() == idle_thread)
    return;
 
  /* Soma 1 (inteiro) ao recent_cpu que está em ponto fixo.
     FpAddInt converte o 1 para ponto fixo antes de somar. */
  thread_current()->recent_cpu = FpAddInt(thread_current()->recent_cpu, 1);
}
 
/* Chamada 1x/segundo — recalcula a carga média do sistema.
   load_avg = (59/60)*load_avg + (1/60)*threads_prontas */
void
thread_mlfqs_update_load_avg(void)
{
  /* Coeficientes 59/60 e 1/60 em ponto fixo */
  fixed_point_t c59 = FpDiv(IntToFp(59), IntToFp(60));
  fixed_point_t c1  = FpDiv(IntToFp(1),  IntToFp(60));
 
  /* Conta threads prontas na ready_list */
  int ready = (int) list_size(&ready_list);
 
  /* Adiciona 1 se a thread atual não for a idle,
     pois ela também está ocupando a CPU agora */
  if (thread_current() != idle_thread)
    ready++;
 
  /* Aplica a fórmula da média exponencial */
  load_avg = FpAdd(FpMul(c59, load_avg), FpMulInt(c1, ready));
 
  /* load_avg nunca pode ser negativo */
  if (load_avg < 0)
    load_avg = 0;
}
 
/* Chamada 1x/segundo para cada thread via thread_foreach.
   Aplica o decaimento no recent_cpu — o passado é esquecido.
   recent = decayrecent + nice
   decay  = (2*avg) / (2*avg + 1) */
void
thread_mlfqs_update_recent_cpu(struct thread *t, void *aux UNUSED)
{
  /* Ignora a idle_thread — ela não tem recent_cpu */
  if (t == idle_thread)
    return;
 
  /* Calcula 2 * load_avg (aparece duas vezes na fórmula,
     por isso guarda numa variável para não calcular duas vezes) */
  fixed_point_t two_avg = FpMulInt(load_avg, 2);
 
  /* decay = (2*avg) / (2*avg + 1)
     FpAddInt soma 1 inteiro ao ponto fixo two_avg */
  fixed_point_t decay = FpDiv(two_avg, FpAddInt(two_avg, 1));
 
  /* Aplica o decaimento no recent_cpu atual */
  fixed_point_t temp = FpMul(decay, t->recent_cpu);
 
  /* Soma o nice — thread com nice alto acumula mais "débito"
     mesmo quando não usa CPU */
  t->recent_cpu = FpAddInt(temp, t->nice);
}
 
/* Chamada 1x/segundo para cada thread via thread_foreach.
   Recalcula a prioridade com a fórmula do MLFQS:
   p = PRI_MAX - (recent_cpu/4) - (nice*2) */
void
thread_mlfqs_recalc_priority(struct thread *t, void *aux UNUSED)
{
  /* Ignora a idle_thread — ela tem prioridade fixa PRI_MIN */
  if (t == idle_thread)
    return;
 
  /* Monta a fórmula de dentro para fora:
     1. IntToFp(PRI_MAX)         → 63 em ponto fixo
     2. FpDivInt(recent_cpu, 4)  → recent_cpu / 4
     3. FpSub(63, cpu/4)         → 63 - cpu/4
     4. IntToFp(nice * 2)        → nice*2 em ponto fixo
     5. FpSub(resultado, nice*2) → 63 - cpu/4 - nice*2
     6. FpToIntArredodamento     → converte para inteiro arredondado */

  int p = FpToIntArredodamento(FpSub(FpSub(IntToFp(PRI_MAX),FpDivInt(t->recent_cpu, 4)),IntToFp(t->nice * 2)));
 
  /* Garante que a prioridade fica dentro dos limites válidos */
  t->priority = p < PRI_MIN ? PRI_MIN : p > PRI_MAX ? PRI_MAX : p;
}
 

 
void
thread_set_nice(int nice)
{
  enum intr_level old = intr_disable();
  thread_current()->nice = nice;
  thread_mlfqs_recalc_priority(thread_current(), NULL);
  intr_set_level(old);
  thread_yield();  /* cede CPU — prioridade pode ter caído */
}
 
int
thread_get_nice(void)
{
  enum intr_level old = intr_disable();
  int n = thread_current()->nice;
  intr_set_level(old);
  return n;
}
 
int
thread_get_load_avg(void)
{
  enum intr_level old = intr_disable();
  int v = FpToIntArredodamento(FpMulInt(load_avg, 100));
  intr_set_level(old);
  return v;
}
 
int
thread_get_recent_cpu(void)
{
  enum intr_level old = intr_disable();
  int v = FpToIntZero(FpMulInt(thread_current()->recent_cpu, 100));
  intr_set_level(old);
  return v;
}
 
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);
 
  for (;;) 
    {
      intr_disable ();
      thread_block ();
      asm volatile ("sti; hlt" : : : "memory");
    }
}
 
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);
  intr_enable ();
  function (aux);
  thread_exit ();
}
 
struct thread *
running_thread (void) 
{
  uint32_t *esp;
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}
 
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}
 
static void
init_thread (struct thread *t, const char *name, int priority)
{
  enum intr_level old_level;
 
  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);
 
  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
 
  /* 
  ALTERAÇÃO-MLFQS 9: inicializa nice e recent_cpu em cada thread
  init_thread() roda toda vez que uma thread é criada.
  Sem isso os campos ficam com lixo de memória e a
  prioridade calculada sai errada desde o início.
   */
  t->nice       = 0;
  t->recent_cpu = IntToFp(0);
 
  t->magic = THREAD_MAGIC;
 
  old_level = intr_disable ();
  list_push_back (&all_list, &t->allelem);
  intr_set_level (old_level);
}
 
static void *
alloc_frame (struct thread *t, size_t size) 
{
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);
  t->stack -= size;
  return t->stack;
}
 
static struct thread *
next_thread_to_run (void) 
{
  if (list_empty (&ready_list))
    return idle_thread;
  else
    /* 
    ALTERAÇÃO-MLFQS 10: pop_front funciona porque a lista
    está sempre ordenada por prioridade.
    O primeiro elemento é sempre o de maior prioridade.
     */
    return list_entry (list_pop_front (&ready_list), struct thread, elem);
}
 
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);
 
  cur->status = THREAD_RUNNING;
  thread_ticks = 0;
 
#ifdef USERPROG
  process_activate ();
#endif
 
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}
 
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;
 
  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));
 
  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}
 
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;
 
  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);
 
  return tid;
}
 
/* 
ALTERAÇÃO-MLFQS 11: thread_sort_ready_list
Chamada pelo timer.c após recalcular todas as prioridades
(1x/segundo). Como o MLFQS muda prioridades de todas as
threads de uma vez, a lista pode ficar fora de ordem —
esta função reordena ela completamente.
 */
void
thread_sort_ready_list (void) 
{
  list_sort (&ready_list, thread_priority, NULL);
}
 
uint32_t thread_stack_ofs = offsetof (struct thread, stack);
 