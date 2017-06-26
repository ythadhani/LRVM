#ifndef __LIBRVM__
#define __LIBRVM__

#include "rvm_internal.h"
#include<string.h>

//Initialize the library with the specified directory as backup store.
rvm_t rvm_init(const char *directory);

//Map a segment from disk into memory. If the segment does not already exist, then create it and give it size size_to_create. If the segment exists but is shorter than size_to_create, then extend it to the given size. It is an error to call rvm_map on the same segment twice -- return (void *) -1 for this case.
void *rvm_map(rvm_t rvm, const char *segname, int size_to_create);

//Unmap a segment from memory.
void rvm_unmap(rvm_t rvm, void *segbase);

//Destroy a segment completely, erasing its backing store. This function should not be called on a segment that is currently mapped -- return immediately for this case. 
void rvm_destroy(rvm_t rvm, const char *segname);

//Begin a transaction that will modify the segments listed in segbases. If any of the specified segments is already being modified by a transaction, the call should fail and return (trans_t) -1. Note that trant_t needs to be able to be typecasted to an integer type.
trans_t rvm_begin_trans(rvm_t rvm, int numsegs, void **segbases);

//Declare that the library is about to modify a specified range of memory in the specified segment. The segment must be one of the segments specified in rvm_begin_trans. Your library needs to ensure that the old memory has been saved, in case an abort is executed. It is legal to call rvm_about_to_modify multiple times on the same memory area.
void rvm_about_to_modify(trans_t tid, void *segbase, int offset, int size);

//Commit all changes that have been made within the specified transaction. When the call returns, enough information should have been saved to disk so that, even if the program crashes, the changes will be seen by the program when it restarts.
void rvm_commit_trans(trans_t tid);

//Undo all changes that have happened within the specified transaction.
void rvm_abort_trans(trans_t tid);

//Apply any committed or aborted items in the log file(s) and shrink the log file(s) as much as possible.
void rvm_truncate_log(rvm_t rvm);

#endif
