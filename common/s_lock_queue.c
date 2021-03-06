#include <s_lock_queue.h>
#include <s_mem.h>
#include <s_list.h>

#include <s_common.h>

struct s_lockq_elem {
	struct s_list link;
	void * data;
};

#define S_LOCKQ_ELEM_POOL_SIZE	1024

struct s_lock_queue {
	pthread_mutex_t m;
	pthread_spinlock_t sl;

	int use_mutex;
	
	struct s_list elem_head;

	struct s_lockq_elem elem_pool[S_LOCKQ_ELEM_POOL_SIZE];
	struct s_list elem_free_head;
	pthread_mutex_t free_m;

	// stat
	int nmalloc;
	int nget;
	int nfree;
};

struct s_lock_queue *
s_lock_queue_create( int use_mutex )
{
	struct s_lock_queue * q = s_malloc(struct s_lock_queue, 1);
	
	pthread_mutex_init(&q->m, NULL);
	pthread_spin_init(&q->sl, 0);

	q->use_mutex = use_mutex;

	s_list_init(&q->elem_head);

	s_list_init(&q->elem_free_head);
	int i;
	for(i = 0; i < S_LOCKQ_ELEM_POOL_SIZE; ++i) {
		struct s_lockq_elem * elem = &q->elem_pool[i];
		elem->data = NULL;
		s_list_insert(&q->elem_free_head, &elem->link);
	}

	pthread_mutex_init(&q->free_m, NULL);

	// stat
	q->nmalloc = q->nget = q->nfree = 0;

	return q;
}

void
s_lock_queue_destroy( struct s_lock_queue * q )
{
	// TODO : free elements

	s_free(q);
}

static struct s_lockq_elem * iget_free_elem(struct s_lock_queue * q)
{
#define MAX_BUF 10000000
	static struct s_lockq_elem g_buf[MAX_BUF];
	static int c = 0;
	return &g_buf[c++];

//	return s_malloc(struct s_lockq_elem, 1);
	
	pthread_mutex_lock(&q->free_m);

	struct s_lockq_elem * elem;
	if(!s_list_empty(&q->elem_free_head)) {
		q->nget++;
		elem = s_list_first_entry(&q->elem_free_head, struct s_lockq_elem, link);
		s_list_del(&elem->link);
	} else {
		q->nmalloc++;
		elem = s_malloc(struct s_lockq_elem, 1);
	}

	s_list_init(&elem->link);

	pthread_mutex_unlock(&q->free_m);

	return elem;
}

static void ifree_elem(struct s_lock_queue * q, struct s_lockq_elem * elem)
{
//	s_free(elem);
	return;
	
	q->nfree++;
	pthread_mutex_lock(&q->free_m);

	s_list_insert(&q->elem_free_head, &elem->link);

	pthread_mutex_unlock(&q->free_m);
}

void s_lock_queue_push(struct s_lock_queue * q, void * d)
{
	if(q->use_mutex) {
		pthread_mutex_lock(&q->m);
	} else {
		pthread_spin_lock(&q->sl);
	}

	struct s_lockq_elem * elem = iget_free_elem(q);

	elem->data = d;

	s_list_insert_tail(&q->elem_head, &elem->link);

	if(q->use_mutex) {
		pthread_mutex_unlock(&q->m);
	} else {
		pthread_spin_unlock(&q->sl);
	}
}

void * s_lock_queue_pop(struct s_lock_queue * q)
{
	if(q->use_mutex) {
		pthread_mutex_lock(&q->m);
	} else {
		pthread_spin_lock(&q->sl);
	}

	void * data = NULL;
	if(!s_list_empty(&q->elem_head)) {
		struct s_lockq_elem * elem = s_list_first_entry(&q->elem_head, struct s_lockq_elem, link);
		s_list_del(&elem->link);
		data = elem->data;
		ifree_elem(q, elem);
	}

	if(q->use_mutex) {
		pthread_mutex_unlock(&q->m);
	} else {
		pthread_spin_unlock(&q->sl);
	}

	return data;
}

void s_lock_queue_dump_stat(struct s_lock_queue * q)
{
	s_log("[lockq] nmalloc:%d, nget:%d, nfree:%d", q->nmalloc, q->nget, q->nfree);
}
