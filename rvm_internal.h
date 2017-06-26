#ifndef __LIBRVMINT__
#define __LIBRVMINT__

#include<string>
#include<iostream>
#include<vector>

using namespace std;

typedef string rvm_t; 
typedef int trans_t; 

typedef struct record_t
{
	int offset;
	int size;
	//No data needed for redo record, directly apply offset and size on data in segment_t entry
	char* data;
}record_t;

typedef struct segment_t
{
	char is_mapped;
	char being_modified; //Set to 1 in rvm_begin_trans if it is a valid mapped segment
	trans_t trans_id;
	char *seg_address;
	string seg_name;
	int seg_size;
	rvm_t rvm;
	vector<record_t*> records;

}segment_t;


#endif
