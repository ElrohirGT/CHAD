#include "fiobject.h"
#include "http.h"
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* *****************************************************************************
Welcum
***************************************************************************** */

/*
In C we can't return tuples or two parameters at the same time, an alternative
would be to make a `Result` type kind of like Rust, but that would involve using
a lot of (void*).

This is bad... (why you ask? Think of `void*` as `any` in
typescript). So a nice middle ground between not using void* (so we can keep
some static analysis on our code) and simply ignoring errors is passing a
parameter that changes it's value in case of an error.

Please, **ALWAYS CHECK THE ERR PARAMETER** after calling a function that
requires it! It's like programming in Go but for C...
*/
typedef size_t *UWU_Err;
static const UWU_Err NO_ERROR = 0;
static const UWU_Err NOT_FOUND = (size_t *)1;
static const UWU_Err MALLOC_FAILED = (size_t *)2;
static const UWU_Err ARENA_ALLOC_FAILED = (size_t *)3;
static const UWU_Err NO_SPACE_LEFT = (size_t *)4;
static const UWU_Err HASHMAP_INITIALIZATION_ERROR = (size_t *)5;

typedef int UWU_Bool;
static const UWU_Bool TRUE = 1;
static const UWU_Bool FALSE = 0;

// A panic represents an irrecoverable error.
//
// The program somehow got into an irrecoverable state and there's no other
// option other than to panic, because continuing would hide a bug!
//
// Common examples of appropriate places to panic include:
// * Accessing items out of bounds.
// * Trying to close a connection that has already been closed.
// * If your function can fail but it doesn't use the `UWU_ERR` API then it
// should PANIC!
void UWU_PANIC(const char *format, ...) {
  va_list args;

  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);

  exit(1);
}

/* *****************************************************************************
Enums
***************************************************************************** */

// Represents all the possible values for a connection status.
typedef enum {
  DISCONNETED,
  ACTIVE,
  BUSY,
  INACTIVE,
} UWU_ConnStatus;

// Represents all the "type codes" of messages the server can receive from the
// client.
typedef enum {
  LIST_USERS = 1,
  GET_USER,
  CHANGE_STATUS,
  SEND_MESSAGE,
  GET_MESSAGES,
} UWU_ServerMessages;

// Represents all the "type codes" of messages the client receives from the
// server.
typedef enum {
  ERROR = 50,
  LISTED_USERS,
  GOT_USER,
  REGISTERED_USER,
  CHANGED_STATUS,
  GOT_MESSAGE,
  GOT_MESSAGES,
} UWU_ClientMessages;

typedef enum {
  // The user you tried to access doesn't exist!
  USER_NOT_FOUND,
  // The status you want to change to doesn't exist!
  INVALID_STATUS,
  // The message you wish to send is empty!
  EMPTY_MESSAGE,
  // You're trying to communicate with a disconnected user!
  USER_ALREADY_DISCONNECTED,
} UWU_Errors;

/* *****************************************************************************
Arenas
***************************************************************************** */

// The arena is a simple abstraction over the concept of storing data on the
// heap. Think of it as a fixed append-only stack, you can only append elements
// to the end or remove them all together.
typedef struct {
  size_t capacity;
  size_t size;
  uint8_t *data;
} UWU_Arena;

// Initializes a new arena with the specified capacity!
UWU_Arena UWU_Arena_init(size_t capacity, UWU_Err err) {
  UWU_Arena arena = {};
  arena.data = malloc(sizeof(uint8_t) * capacity);

  if (arena.data == NULL) {
    err = MALLOC_FAILED;
    return arena;
  }

  arena.capacity = capacity;
  arena.size = 0;

  return arena;
}

// Tries to allocate on the arena.
//
// - arena: The specific arena to use for allocation.
// - size: The amount of bytes to allocate.
// - err: The err parameter, refer to the start of lib for an explanation.
//
// Success: Returns a pointer to the first byte of the memory region requested.
// Failure: Sets err equal to `UWU_ARENA_FAILED_ALLOCATION`.
void *UWU_Arena_alloc(UWU_Arena *arena, size_t size, size_t *err) {
  UWU_Bool has_space = arena->size + size <= arena->capacity;
  if (!has_space) {
    err = ARENA_ALLOC_FAILED;
    return NULL;
  } else {
    void *mem_start = &arena->data[arena->size];
    arena->size += size;
    return mem_start;
  }
}

// Resets the arena for future use.
// IT DOES NOT FREE THE MEMORY OF THE ARENA! Use `deinit` for that.
void UWU_Arena_reset(UWU_Arena *arena) { arena->size = 0; }

// Resets and frees all memory associated with this arena.
// DO NOT USE an arena that has already been deinited!
void UWU_Arena_deinit(UWU_Arena arena) {
  arena.capacity = 0;
  arena.size = 0;
  free(arena.data);
}

/* *****************************************************************************
Strings
***************************************************************************** */

// Represents a string "slice"
//
// Only the creator of the original slice needs to free this memory!
typedef struct {
  // Contains all the data of this string
  //
  // CAREFUL: It may or may not be a "null terminated string"!
  char *data;
  // The length of data that is considered to be "this string"
  size_t length;
} UWU_String;

UWU_Bool UWU_String_startsWith(UWU_String *str, UWU_String *prefix) {
  if (str->length < prefix->length) {
    return FALSE;
  }

  if (0 == memcmp(str->data, prefix->data, prefix->length)) {
    return TRUE;
  }

  return FALSE;
}

UWU_Bool UWU_String_endsWith(UWU_String *str, UWU_String *postfix) {
  if (str->length < postfix->length) {
    return FALSE;
  }

  size_t start_idx = str->length - postfix->length;
  if (0 == memcmp(&str->data[start_idx], postfix->data, postfix->length)) {
    return TRUE;
  }

  return FALSE;
}

// Returns `TRUE` if `first` goes first alphabetically speaking.
// False otherwise.
UWU_Bool UWU_String_firstGoesFirst(UWU_String *first, UWU_String *other) {
  size_t min_length = first->length;
  if (other->length < min_length) {
    min_length = other->length;
  }

  for (size_t i = 0; i < min_length; i++) {
    char char_first = first->data[i];
    char char_other = other->data[i];
    if (char_first == char_other) {
      continue;
    }

    if (char_first < char_other) {
      return TRUE;
    } else {
      return FALSE;
    }
  }

  return FALSE;
}

// Attempts to combine two strings.
//
// The caller owns the resulting string.
UWU_String UWU_String_combineWithOther(UWU_String *first, UWU_String *second) {
  UWU_String str = {.length = first->length + second->length};
  str.data = malloc(str.length);

  if (str.data == NULL) {
    UWU_PANIC("Malloc failed when trying to combine two strings!");
    str.length = 0;
    return str;
  }

  memcpy(str.data, first->data, first->length);
  memcpy(&str.data[first->length], second->data, second->length);

  return str;
}

// Attempts to combine two strings.
//
// The caller owns the resulting string.
UWU_String UWU_String_tryCombineWithOther(UWU_String *first, UWU_String *second,
                                          UWU_Err err) {
  UWU_String str = {.length = first->length + second->length};
  str.data = malloc(str.length);

  if (str.data == NULL) {
    err = MALLOC_FAILED;
    str.length = 0;
    return str;
  }

  memcpy(str.data, first->data, first->length);
  memcpy(&str.data[first->length], second->data, second->length);

  return str;
}

// Frees the specified `UWU_String` that was originally allocated by a `malloc`
// call. This function ONLY FREES THE `data` field inside `UWU_String`.
void UWU_String_freeWithMalloc(UWU_String *str) { free(str->data); }

// Attempts to converts from a `UWU_String` to a null terminated string.
char *UWU_String_tryToCStr(UWU_String *str, UWU_Err err) {
  char *c_str = malloc(str->length + 1);

  if (c_str == NULL) {
    err = MALLOC_FAILED;
    return c_str;
  }

  for (size_t i = 0; i < str->length + 1; i++) {
    c_str[i] = str->data[i];
  }
  c_str[str->length] = 0;

  return c_str;
}

// Converts from `UWU_String` into a null-terminated string (C string).
//
// If malloc fails then panics.
char *UWU_String_toCStr(UWU_String *str) {
  char *c_str = malloc(str->length + 1);

  if (c_str == NULL) {
    UWU_PANIC("Can't convert UWU_String into C_str! (len: %d, data: %s)",
              str->length, str->data);
    return c_str;
  }

  for (size_t i = 0; i < str->length + 1; i++) {
    c_str[i] = str->data[i];
  }
  c_str[str->length] = 0;

  return c_str;
}

// Tries to obtain a char at the specified index.
// Panics if the index is out of bounds!
char UWU_String_charAt(UWU_String *str, size_t idx) {
  if (idx < 0 || idx >= str->length) {
    UWU_PANIC("Can't access character out of bounds of string!");
    return 0;
  }

  return str->data[idx];
}

// Copies a fiobj into a `UWU_String`.
//
// `obj` must be a `FIOBJ_T_STRING`, if not this function panics!
//
// To free this data please see `UWU_String_free`.
UWU_String UWU_String_copyFromFio(FIOBJ obj, UWU_Err err) {
  UWU_String str = {};

  if (!FIOBJ_TYPE_IS(obj, FIOBJ_T_STRING)) {
    UWU_PANIC("Trying to copy from a Fio object that is not a string!");
    return str;
  }

  fio_str_info_s c_str = fiobj_obj2cstr(obj);
  char *data = malloc(c_str.len);
  for (size_t i = 0; i < c_str.len; i++) {
    data[i] = c_str.data[i];
  }

  str.data = data;
  str.length = c_str.len;

  return str;
}

// Copies `src` into a new `UWU_String`.
UWU_String UWU_String_copy(UWU_String *src, UWU_Err err) {
  UWU_String str = {};
  str.data = malloc(src->length);

  if (src->data == NULL) {
    err = MALLOC_FAILED;
    return str;
  }

  memcpy(str.data, src->data, src->length);
  str.length = src->length;

  return str;
}

uint8_t UWU_String_getChar(UWU_String *str, size_t idx) {
  if (idx >= 0 && idx < str->length) {
    return str->data[idx];
  }

  char *c_str = UWU_String_toCStr(str);

  UWU_PANIC("Out of bound access on String `%s` with Idx `%d`", c_str, idx);
  free(c_str);

  return 0;
}

// Checks if the given `a` string is equal to the other `b` string.
UWU_Bool UWU_String_equal(UWU_String *a, UWU_String *b) {
  if (a->length != b->length) {
    return FALSE;
  }

  return 0 == memcmp(a->data, b->data, a->length);
}

/* *****************************************************************************
Server Users
***************************************************************************** */

typedef struct {
  UWU_String username;
  UWU_ConnStatus status;
  time_t last_action;
  // The Websocket connection associated with this user.
  // This can be null!
  ws_s *ws;
} UWU_User;

UWU_User UWU_User_copyFrom(UWU_User *src, UWU_Err err) {
  UWU_User copy = {};

  UWU_String user_name_copy = UWU_String_copy(&src->username, err);
  if (err != NO_ERROR) {
    err = MALLOC_FAILED;
    return copy;
  }

  copy.username = user_name_copy;
  copy.status = src->status;
  copy.last_action = src->last_action;
  copy.ws = src->ws;

  return copy;
}

// Frees all resources associated with this user.
//
// This does not include the user itself!
void UWU_User_free(UWU_User *ref) { UWU_String_freeWithMalloc(&ref->username); }

// Represents a node inside the linked list.
struct UWU_UserListNode {
  // The linked list will always hold two nodes at the extremes.
  // This nodes are the sentinels nodes, all other nodes should NOT BE sentinels
  // nodes.
  UWU_Bool is_sentinel;
  // The data the node is holding.
  UWU_User data;
  // The previous node in the list.
  struct UWU_UserListNode *previous;
  // The next node in the list.
  struct UWU_UserListNode *next;
};

// Creates a new `UWU_UserListNode` with `data`.
//
// Both `next` and `previous` pointers are set to `NULL`.
struct UWU_UserListNode UWU_UserListNode_newWithValue(UWU_User data) {
  struct UWU_UserListNode node = {
      .data = data,
      .previous = NULL,
      .next = NULL,
      .is_sentinel = FALSE,
  };

  return node;
}

// Creates a copy from `other` and allocates it on the heap.
struct UWU_UserListNode *UWU_UserListNode_copy(struct UWU_UserListNode *other,
                                               UWU_Err err) {
  struct UWU_UserListNode *copy = malloc(sizeof(struct UWU_UserListNode));
  if (copy == NULL) {
    err = MALLOC_FAILED;
    return NULL;
  }

  UWU_User data_copy = UWU_User_copyFrom(&other->data, err);
  if (err != NO_ERROR) {
    err = MALLOC_FAILED;
    return NULL;
  }

  copy->data = data_copy;
  copy->previous = other->previous;
  copy->next = other->next;
  copy->is_sentinel = other->is_sentinel;

  return copy;
}

// Free all the resources associated with this node.
void UWU_UserListNode_deinit(struct UWU_UserListNode *ref) {
  UWU_User_free(&ref->data);

  // Reset pointers...
  ref->next = NULL;
  ref->previous = NULL;
}

// Saves a list of users, the implementation is a linked list.
// Appending items to the start and end are O(1) operations.
// Since it's a collection, you can remove and add new users at any time.
//
// The list OWNS THE VALUES, so every addition is a copy, and every removal is a
// free!
typedef struct {
  // A pointer to the start of the list
  // By default is NULL.
  struct UWU_UserListNode *start;
  // A pointer to the end of the list
  // By default is NULL.
  struct UWU_UserListNode *end;

  // The length of this list!
  // This value will be valid as long as items are only added using the methods
  // from the `UWU_UserList` API
  size_t length;
} UWU_UserList;

// Attempts to find a user by it's name.
// Returns a reference to the found user. NULL otherwise.
UWU_User *UWU_UserList_findByName(UWU_UserList *list, UWU_String *name) {
  for (struct UWU_UserListNode *current = list->start; current != NULL;
       current = current->next) {
    if (current->is_sentinel) {
      continue;
    }

    if (UWU_String_equal(&current->data.username, name)) {
      return &current->data;
    }
  }

  return NULL;
}

// Tries to update the first user it finds with the given username.
// Returns true if it found a user to update. False otherwise.
UWU_Bool UWU_UserList_updateUserByName(UWU_UserList *list, UWU_String *name,
                                       UWU_User new_data) {
  for (struct UWU_UserListNode *current = list->start; current != NULL;
       current = current->next) {
    if (current->is_sentinel) {
      continue;
    }

    if (!UWU_String_equal(&current->data.username, name)) {
      continue;
    }

    current->data = new_data;
    return TRUE;
  }

  return FALSE;
}

UWU_UserList UWU_UserList_init(UWU_Err err) {
  UWU_String sentinel_name = {
      .data = "<sentinel>",
      .length = strlen("<sentinel>"),
  };
  UWU_User def_user = {.username = sentinel_name, .status = DISCONNETED};
  struct UWU_UserListNode start_node = UWU_UserListNode_newWithValue(def_user);
  struct UWU_UserListNode *start_copy = UWU_UserListNode_copy(&start_node, err);
  start_copy->is_sentinel = TRUE;

  struct UWU_UserListNode end_node = UWU_UserListNode_newWithValue(def_user);
  struct UWU_UserListNode *end_copy = UWU_UserListNode_copy(&end_node, err);
  end_copy->is_sentinel = TRUE;

  // Setup references...
  start_copy->next = end_copy;
  end_copy->previous = start_copy;

  UWU_UserList def_list = {.start = start_copy, .end = end_copy, .length = 0};
  return def_list;
}

// Destroys all pointers and frees all memory of all nodes.
void UWU_UserList_deinit(UWU_UserList *list) {
  struct UWU_UserListNode *current = list->start;

  while (current != NULL) {
    struct UWU_UserListNode *tmp = current;
    current = current->next;
    UWU_UserListNode_deinit(tmp);
  }

  free(list->end);
  free(list->start);
}

// Inserts a specified node to the start of the list.
// Remember, the list owns the values so this will create a copy of `*node`
// and therefore it can fail!
void UWU_UserList_insertStart(UWU_UserList *list, struct UWU_UserListNode *node,
                              UWU_Err err) {
  struct UWU_UserListNode *copy = UWU_UserListNode_copy(node, err);
  if (err != NO_ERROR) {
    return;
  }

  struct UWU_UserListNode *second_node = list->start->next;

  // Update already existing nodes...
  second_node->previous = copy;
  list->start->next = copy;

  // Update copy node...
  copy->previous = list->start;
  copy->next = second_node;

  list->length += 1;
}

// Inserts a specified node to the end of the list.
// Remember, the list owns the values so this will create a copy of `*node`
// and therefore it can fail!
void UWU_UserList_insertEnd(UWU_UserList *list, struct UWU_UserListNode *node,
                            UWU_Err err) {

  struct UWU_UserListNode *copy = UWU_UserListNode_copy(node, err);
  if (err != NO_ERROR) {
    return;
  }

  struct UWU_UserListNode *ante_node = list->end->previous;

  // Update already existing nodes...
  ante_node->next = copy;
  list->end->previous = copy;

  // Update copy node...
  copy->previous = ante_node;
  copy->next = list->end;

  list->length += 1;
}

// Tries to remove a user by it's username, if the username is not found then
// it simply does nothing.
//
// REMEMBER!! This frees the associated memory of the removed node.
void UWU_UserList_removeByUsernameIfExists(UWU_UserList *list,
                                           UWU_String *username) {

  for (struct UWU_UserListNode *current = list->start; current != NULL;
       current = current->next) {
    if (current->is_sentinel) {
      continue;
    }

    UWU_String current_username = current->data.username;

    if (UWU_String_equal(username, &current_username)) {
      struct UWU_UserListNode *previous = current->previous;
      struct UWU_UserListNode *next = (current->next);

      // Update references from list...
      previous->next = current->next;
      next->previous = current->previous;

      UWU_UserListNode_deinit(current);
      free(current); // The node is always on the heap thanks to
                     // `UWU_UserListNode_copy`!
      list->length -= 1;
      current = previous;
    }
  }
}

// Represents a message on a given chat history.
//
// The ChatEntry should own it's memory! So it should receive a copy of
// `content` and `origin_username`.
typedef struct {
  // The content of the message.
  UWU_String content;
  // The username of the person sending the message.
  UWU_String origin_username;
} UWU_ChatEntry;

UWU_ChatEntry UWU_ChatEntry_copy(UWU_ChatEntry *src, UWU_Err err) {
  UWU_ChatEntry def = {};

  UWU_String content_copy = UWU_String_copy(&src->content, err);
  if (err != NO_ERROR) {
    return def;
  }
  UWU_String username_copy = UWU_String_copy(&src->origin_username, err);
  if (err != NO_ERROR) {
    return def;
  }

  UWU_ChatEntry dest = {
      .content = content_copy,
      .origin_username = username_copy,
  };

  return dest;
}

void UWU_ChatEntry_free(UWU_ChatEntry *src) {
  UWU_String_freeWithMalloc(&src->content);
  UWU_String_freeWithMalloc(&src->origin_username);
}
// Represents a message history of a certain chat
//
// Messages are stored on the `*messages` buffer. If the buffer is full the
// oldest data is overridden.
//
// To iterate the data in order please obtain an iterator using:
// `UWU_ChatHistory_iter()`
typedef struct {
  // A pointer to an array of `ChatEntry`.
  UWU_ChatEntry *messages;
  // The name of the channel that points to this history the server state
  UWU_String channel_name;
  // The number of chat messages filling the array.
  size_t count;
  // How much memory is left in the array of `ChatEntry`.
  size_t capacity;
  // The idx of the next message to insert in the array.
  size_t next_idx;
} UWU_ChatHistory;

// Creates a new ChatHistory with the specified capacity for messages.
UWU_ChatHistory UWU_ChatHistory_init(size_t capacity, UWU_String channel_name,
                                     UWU_Err err) {
  UWU_ChatHistory ht = {};

  ht.messages = malloc(sizeof(UWU_ChatEntry[capacity]));
  if (ht.messages == NULL) {
    err = MALLOC_FAILED;
    return ht;
  }

  ht.capacity = capacity;
  ht.count = 0;
  ht.next_idx = 0;
  ht.channel_name = channel_name;

  return ht;
}

void UWU_ChatHistory_deinit(UWU_ChatHistory *ht) {
  for (size_t i = 0; i < ht->count; i++) {
    UWU_ChatEntry_free(&ht->messages[i]);
  }
  free(ht->messages);
  UWU_String_freeWithMalloc(&ht->channel_name);
}

// Adds a new entry to the ChatHistory.
//
// If the ChatHistory is already full then it wraps around and adds it to the
// back.
void UWU_ChatHistory_addMessage(UWU_ChatHistory *hist, UWU_ChatEntry *entry) {
  UWU_Err err = NO_ERROR;
  size_t next_idx = hist->next_idx % hist->capacity;
  UWU_ChatEntry clone = UWU_ChatEntry_copy(entry, err);
  if (err != NO_ERROR) {
    UWU_PANIC("Fatal: Failed to copy msg into chat history!");
    return;
  }

  hist->messages[next_idx] = clone;

  hist->count += 1;
  hist->next_idx += 1;
}

// Gives limits for iterating over a `UWU_ChatHistory` in insertion order.
// `start` and `end` ARE NOT indexes! Make sure to apply the % operator
// because they can grow far beyond what the collection could hold!
typedef struct {
  size_t start;
  size_t end;
} UWU_ChatHistory_Iterator;

UWU_ChatHistory_Iterator UWU_ChatHistory_iter(UWU_ChatHistory *ht) {
  UWU_ChatHistory_Iterator iter = {};

  iter.start = 0;
  iter.end = ht->count;

  if (ht->count >= ht->capacity) {
    iter.end = ht->next_idx;
    iter.start = iter.end - ht->count;
  }

  return iter;
}

// Gets an item from the history!
// Panics if index is outside of bounds.
UWU_ChatEntry UWU_ChatHistory_get(UWU_ChatHistory *ht, size_t idx) {
  UWU_ChatEntry entry = {};
  if (idx < 0 || idx >= ht->capacity) {
    UWU_PANIC("Trying to get a ChatEntry (idx: %d) from ChatHistory (count: "
              "%d, capacity: %d)",
              idx, ht->count, ht->capacity);
    return entry;
  }

  entry = ht->messages[idx];
  return entry;
}
