/**
 * CS 2110 - Fall 2011 - Homework #11
 * Edited by: Tomer Elmalem 
 *
 * list.h
 */

#ifndef LIST_H
#define LIST_H
#include <pthread.h>
#include <semaphore.h>


/***************************************
 ** Datatype definitions DO NOT TOUCH **
 ***************************************/

/* Forward declaration 
Don't know what a forward declaration is? 
Consult the fountain of knowledge: http://en.wikipedia.org/wiki/Forward_declaration */
struct lnode;

/* The linked list struct.  Has a head pointer. */
typedef struct llist
{
  struct lnode* head; /* Head pointer either points to a node with data or NULL */
  unsigned int size; /* Size of the linked list */
  pthread_mutex_t lock;
  sem_t sem;
} list;

/* A function pointer type that points to a function that takes in a void* and returns nothing call it list_op */
typedef void (*list_op)(void*);
/* A function pointer type that points to a function that takes in a const void* and returns an int call it list_pred */
typedef int (*list_pred)(const void*);
/* A function pointer type that points to a function that takes in 2 const void*'s and returns an int call it equal_op 
   this should return 0 if the data is not equal and 1 (or non-zero) if it is equal*/
typedef int (*equal_op)(const void*, const void*);


/**************************************************
** Prototypes for linked list library functions. **
**                                               **
** For more details on their functionality,      **
** check list.c.                                 **
***************************************************/
typedef struct lnode
{
  struct lnode* prev; /* Pointer to previous node */
  struct lnode* next; /* Pointer to next node */
  void* data; /* User data */
} node;
/* Creating */
list* create_list(void);

/* Adding */
void push_front(list* llist, void* data);
void push_back(list* llist, void* data);

/* Removing */
int remove_front(list* llist, list_op free_func);
int remove_index(list* llist, int index, list_op free_func);
int remove_back(list* llist, list_op free_func);
int remove_data(list* llist, const void* data, equal_op compare_func, list_op free_func);
int remove_if(list* llist, list_pred pred_func, list_op free_func);

/* Querying List */
void* front(list* llist);
void* back(list* llist);
void* get_index(list* llist, int index);
int is_empty(list* llist);
int size(list* llist);

/* Searching */
void* find_occurrence(list* llist, const void* search, equal_op compare_func);

/* Freeing */
void empty_list(list* llist, list_op free_func);

/* Traversal */
void traverse(list* llist, list_op do_func);

#endif