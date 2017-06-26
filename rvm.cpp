#include "rvm.h"
#include <sys/stat.h>
#include <iostream>
#include <string>
#include <map>
#include <fstream>
#include <stdlib.h> 
#include <vector>
#include<cstdlib>
#include<dirent.h>

using namespace std;

//Maps the segment name to the corresponding segment struct
map<string,segment_t*> segment_hash;
map<string,segment_t*>::iterator segment_hash_iter;

//Maps the starting address of the segment to its name
map<char*,string> segaddress_to_name;
map<char*,string>::iterator segaddress_to_name_iter;

//Maps the transaction id to the vector of segment objects being modified by the transaction
map< trans_t,vector<segment_t*> > transaction_to_modsegs;
map< trans_t,vector<segment_t*> >::iterator transaction_to_modsegs_iter;

static trans_t trans_id;

//Truncates the segment identified by segname within the rvm directory identified by rvm
void truncate_segment_log(rvm_t rvm, string segname)
{
	string log_file_path = rvm+"/"+segname+".log";
	string segment_file_path = rvm+"/"+segname;
	FILE * segFile;
	segFile = fopen(segment_file_path.c_str(),"r+");
	ifstream log_file(log_file_path.c_str());
	if (log_file.is_open() && segFile!=NULL) 
	{
		string log_record; 
		while (getline(log_file,log_record))
		{
			//Write the modifications specified by value to the external segment file at a position specified by offset 
			char* offset = strtok(&log_record[0],"|");			
			char* value = strtok(NULL,"|");
			fseek(segFile,atoi(offset),SEEK_SET);
			fwrite(value,sizeof(char),strlen(value),segFile);
		}
		log_file.close();
		fclose(segFile);	
		remove(log_file_path.c_str());		
	}
	
}

rvm_t rvm_init(const char *directory)
{
	mkdir(directory, S_IRWXU);
	return directory; 
}

void *rvm_map(rvm_t rvm, const char *segname, int size_to_create)
{
	segment_hash_iter= segment_hash.find(segname); 
	string file_path = rvm + "/" + segname;
	ifstream infile((const char*) file_path.c_str(), ios::binary | ios::ate | ios::app);
	segment_t* segment_entry;
	if(infile.good())
	{
		//External segment file already exists
		//Two possibilities: 1] In hash table (backup of this run) 2] Not in hash table (backup of an older run)
		
		if(segment_hash_iter!=segment_hash.end())
		{
			//Entry exists in hash table, the rvm directory has a backup for this run of the program. Directory wasn't already 				existing before we ran the program
			segment_entry = segment_hash_iter->second;
			if(segment_entry->is_mapped==1)
			{
				//Error to call rvm_map on the same segment twice
				cout<<"ERROR: Attempting to map an already mapped segment"<<endl;
				return (void*)-1;
			}
			else
			{
				//Segment was mapped eariler and then unmapped.
				segment_entry->is_mapped = 1;
				segment_entry->rvm = rvm;
				segment_entry->seg_size = size_to_create;
				segment_entry->being_modified = 0;
				segment_entry->trans_id=0;
				segment_entry->seg_address = new char[size_to_create];
				memset( segment_entry->seg_address, '\0', sizeof(char)*size_to_create );
			}
		}  
		else
		{
			//Backup file exists but no entry in hash map. This is a backup of a previous run of the program
			segment_entry = new segment_t;
			segment_entry->is_mapped = 1;
			segment_entry->rvm = rvm;
			segment_entry->being_modified = 0;
			segment_entry->trans_id=0;
			segment_entry->seg_name = segname;
			segment_entry->seg_size = size_to_create;
			segment_entry->seg_address = new char[size_to_create];
			memset( segment_entry->seg_address, '\0', sizeof(char)*size_to_create );
			segment_hash.insert ( pair<string,segment_t*>(segment_entry->seg_name,segment_entry) );
			
		}
		//Transfer file contents into memory-resident segment. Need to do this irrespective of intermediate conditions.
		truncate_segment_log(rvm,segname); 
		infile.seekg (0, infile.end);
		int length = infile.tellg();
		infile.seekg (0, infile.beg);

		infile.read (segment_entry->seg_address,length);
		infile.close();
	}
	else
	{
		//External segment file does not exist. This is the first time this segment has been mapped. It wasn't mapped even in earlier 			program executions.
		//Need to create external segment file on backing store and also add entry to hash table.
		ofstream o(file_path.c_str());
		o.close();
		segment_entry =  new segment_t;
		segment_entry->is_mapped = 1;
		segment_entry->rvm = rvm;
		segment_entry->being_modified = 0;
		segment_entry->trans_id=0;
		segment_entry->seg_name = segname;
		segment_entry->seg_size = size_to_create;
		segment_entry->seg_address = new char[size_to_create];
		memset( segment_entry->seg_address, '\0', sizeof(char)*size_to_create );
		segment_hash.insert ( pair<string,segment_t*>(segment_entry->seg_name,segment_entry) );
		
	}
	segaddress_to_name.insert ( pair<char*,string>(segment_entry->seg_address,segment_entry->seg_name) );
	return (void*)segment_entry->seg_address;
	
}

void rvm_unmap(rvm_t rvm, void *segbase)
{
	//On call to unmap, we do not delete the segment_entry from our hash map, we merely set the "is_mapped" to zero
	string segname =  segaddress_to_name[(char*)segbase];
	segment_t* segment_entry = segment_hash[segname];
	segment_entry->seg_address = NULL;
	segment_entry->is_mapped = 0;
	segment_entry->being_modified = 0;
	segment_entry->trans_id = 0;
	return;	
}

trans_t rvm_begin_trans(rvm_t rvm, int numsegs, void **segbases)
{
	vector<segment_t*> segs_to_modify;
	for(int i=0; i<numsegs; i++)
	{
		segaddress_to_name_iter= segaddress_to_name.find((char*)segbases[i]); 
		if(segaddress_to_name_iter!=segaddress_to_name.end())
		{
			segment_t* segment_entry = segment_hash[segaddress_to_name_iter->second];
			if(segment_entry->is_mapped)
			{	
				if(segment_entry->being_modified)
				{
					cout<<"ERROR: One of the segments specified is being modified by another transaction"<<endl;
					return (trans_t)-1;
				}
				else
				{
					//The valid segments that are a part of this transcation are pushed into the segs_to_modify vector
					segment_entry->being_modified=1;
					segs_to_modify.push_back(segment_entry);
				}
			}
			else
			{
				cout<<"ERROR: One of the segments specified in segbases is currently unmapped"<<endl;
				return (trans_t)-1;
			}
		}
		else
		{
			cout<<"ERROR: One of the segments specified in segbases is invalid or has not been mapped"<<endl;
			return (trans_t)-1;
		}
	}
	trans_id = trans_id+1;
	//numsegs = segs_to_modify.size()
	for(int i=0;i<numsegs;i++)  
	{
		(segs_to_modify[i])->trans_id = trans_id;
	}
	transaction_to_modsegs.insert ( pair< trans_t,vector<segment_t*> >(trans_id,segs_to_modify) );
	return trans_id;

}

void rvm_about_to_modify(trans_t tid, void *segbase, int offset, int size)
{
	segaddress_to_name_iter= segaddress_to_name.find((char*)segbase);
	if(segaddress_to_name_iter!=segaddress_to_name.end())
	{
		segment_t* segment_entry = segment_hash[segaddress_to_name_iter->second];
		
		if(segment_entry->trans_id==tid)
		{
			//This segbase is a part of a valid transaction

			if((offset+size)>segment_entry->seg_size)
			{
				cout<<"ERROR: Offset + size is greater than the segment size"<<endl;
				exit(1);
			}
			/* Creating undo/redo record for this segbase.
			A single record would suffice. 
			The data member is needed only for an undo record.
			For a redo record, offset and size are sufficient */
			record_t *rec = new record_t;
			rec->offset = offset;
			rec->size = size;
			rec->data = new char[size];
			memcpy(rec->data,((char*)segbase+offset),size);
			(segment_entry->records).push_back(rec);
		}
		else
		{
			cout<<"ERROR: The transaction id is invalid or the segment address was not specified in rvm_begin_trans"<<endl;
			return;
		}
	}
	else
	{
		cout<<"ERROR: The segment address specified has not been mapped"<<endl;
		return;
	}
}

void rvm_commit_trans(trans_t tid)
{
	transaction_to_modsegs_iter = transaction_to_modsegs.find(tid);
	if(transaction_to_modsegs_iter!=transaction_to_modsegs.end())
	{
		//This vector contains all segments that could have been modified under the scope of this transaction.
		vector<segment_t*> modified_segs = transaction_to_modsegs_iter->second;
		for(vector<segment_t*>::iterator sit = modified_segs.begin();sit != modified_segs.end(); ++sit)
		{
			segment_t* segment_entry = *sit;
			//This vector contains all redo records/modifications made to the segment specified by "segment_entry".
			vector<record_t*> redo_records = segment_entry->records;
			string file_path = segment_entry->rvm + "/" + segment_entry->seg_name + ".log";
			ofstream write_stream;
			write_stream.open (file_path.c_str(), ofstream::out | ofstream::ate | ofstream::app);
			for(vector<record_t*>::iterator rit = redo_records.begin();rit != redo_records.end(); ++rit)
			{
				/* Each line in the log file represents an (offset|value) tuple. The "value" is obtained by using offset and 					size in the redo record to extract the modified data chunk from the latest memory-resident segment. */
				record_t* record = *rit;
				write_stream<<record->offset<<"|";
				write_stream.write ((segment_entry->seg_address)+record->offset,record->size);
				write_stream<<endl;	
			}
			write_stream.close();
			(segment_entry->records).clear();
			segment_entry->trans_id=0;
			segment_entry->being_modified=0;
		}
		transaction_to_modsegs.erase(tid);
	}
	else
	{
		cout<<"ERROR: Invalid transaction id"<<endl;
		return;
	}
}

void rvm_abort_trans(trans_t tid)
{
	transaction_to_modsegs_iter = transaction_to_modsegs.find(tid);
	if(transaction_to_modsegs_iter!=transaction_to_modsegs.end())
	{
		vector<segment_t*> modified_segs = transaction_to_modsegs_iter->second;
		for(vector<segment_t*>::iterator sit = modified_segs.begin();sit != modified_segs.end(); ++sit)
		{
			segment_t* segment_entry = *sit;
			vector<record_t*> undo_records = segment_entry->records;
			/* It is important to note that undo_records are applied to the memory resident data segment in reverse order. This 				handles the case when multiple modifications are made to overlapping address ranges. */
			for(vector<record_t*>::reverse_iterator rit = undo_records.rbegin();rit != undo_records.rend(); ++rit)
			{
				record_t* record = *rit;
				memcpy((segment_entry->seg_address)+record->offset,record->data,record->size);
			}
			(segment_entry->records).clear();
			segment_entry->trans_id=0;
			segment_entry->being_modified=0;
		}
		transaction_to_modsegs.erase(tid);
	}
	else
	{
		cout<<"ERROR: Invalid transaction id"<<endl;
		return;
	}
}

void rvm_truncate_log(rvm_t rvm)
{
	DIR *pDIR;
        struct dirent *entry;
        if( (pDIR=opendir(rvm.c_str())) )
	{
		while((entry = readdir(pDIR)))
		{    
			string file_name = entry->d_name;
			int pos = file_name.find(".log");			
			if(pos!=-1)
			{     
 	                	string segname = file_name.substr(0,pos);
				// We truncate each log file individually by applying its contents to the external data segment.
				truncate_segment_log(rvm,segname);	 
			}
                }
		
                closedir(pDIR);
	}
	else
	{
		cout<<"ERROR: The rvm directory "<<rvm<<" does not exist"<<endl;
	}
}

void rvm_destroy(rvm_t rvm, const char *segname)
{
	segment_hash_iter= segment_hash.find(segname); 
	string seg_file_path = rvm + "/" + segname;
	string log_file_path = rvm + "/" + segname + ".log";

	if(segment_hash_iter!=segment_hash.end())
	{
		segment_t* segment_entry = segment_hash_iter->second;
		if(segment_entry->is_mapped==1)
		{
			cout<<"ERROR: Attempting to destroy a mapped segment"<<endl;
			return;
		}
		segment_hash.erase(segname);
		delete segment_entry;
	}
	//Deleting the external data segment specified by segname and its corresponding log file.
	remove(log_file_path.c_str());
	remove(seg_file_path.c_str());
}
