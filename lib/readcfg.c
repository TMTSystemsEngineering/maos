/*
  Copyright 2009-2022 Lianqi Wang <lianqiw-at-tmt-dot-org>
  
  This file is part of Multithreaded Adaptive Optics Simulator (MAOS).

  MAOS is free software: you can redistribute it and/or modify it under the
  terms of the GNU General Public License as published by the Free Software
  Foundation, either version 3 of the License, or (at your option) any later
  version.

  MAOS is distributed in the hope that it will be useful, but WITHOUT ANY
  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
  A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

  You should have received a copy of the GNU General Public License along with
  MAOS.  If not, see <http://www.gnu.org/licenses/>.
*/


/*
  read the config file, parse the tokens and store into a binary tree.
  then user can query tokens using read_int, read_real, or read_array
*/

#include <search.h>
#include <ctype.h>
#include <limits.h>
#include "readcfg.h"
/**
   Routines to read .conf type config files. Each entry is composed of a key and
   a value. The entries are maintained in a tree. Each entry can be
   retrieved from the key.
*/
/**
   Compatibility mode: old keys are automatically renamed to new keys.
*/
#define COMPATIBILITY 1
DEF_ENV_FLAG(PERMISSIVE, 0)
#if COMPATIBILITY == 1
#define RENAME(old,new)	if(!strcmp(var,#old)){warning("Deprecated: change %s to %s.\n",#old,#new);var=#new; }
#define IGNORE(old) if(!strcmp(var,#old)){ssline[0]='\0';continue; }
#else
#define RENAME(old,new)
#define IGNORE(old)
#endif
static void* MROOT=NULL;
static char* default_config=NULL;
typedef struct STORE_T{
	char* key;    //Name of the entry
	char* data;   //Value of the entry
	long priority;//Priority of the entry
	long used;    //Whether the entry has been used
}STORE_T;
static int key_cmp(const void* a, const void* b){
	return strcmp(((STORE_T*)a)->key, ((STORE_T*)b)->key);
}

/*Keep record of all the strings so that we can check whether they have all been used. */
static long nstore=0;/*number of total records */
static long nused=0;/*number of read records */

#define STRICT 1

/**
   trim the spaces before and after string.*/
static void strtrim(char** str){
	if(!*str) return;
	int iend;
	/*turn non-printable characters, coma, and semicolumn, to space */
	for(char* tmp=*str; !is_end(*tmp); tmp++){
		if(!isgraph((int)*tmp)||isspace(*tmp)){
			*tmp=' ';
		}
	}
	/*remove leading spaces. */
	while(!is_end(**str)&&isspace((*str)[0])) (*str)++;
	iend=strlen(*str)-1;
	/*remove tailing spaces. */
	while((isspace((*str)[iend])||(*str)[iend]==';')&&iend>=0){
		(*str)[iend]='\0';
		iend--;
	}
	/*remove continuous spaces */
	char* tmp2=*str;
	int issp=0;
	for(char* tmp=*str; !is_end(*tmp); tmp++){
		if(*tmp=='['){
			issp=2; //skip space after [
		} else if(*tmp==' '){
			if(issp){//skip multiple space
				continue;
			} else{
				issp=1;
			}
		} else if(*tmp==']'){
			if(issp==1){//remove space before ]
				tmp2--;
			} else{
				issp=2;//skip space after ]
			}
		} else{
			issp=0;
		}
		/*copy the string. */
		*tmp2=*tmp;
		tmp2++;
	}
	*tmp2='\0';/*properly end the string */
	if(is_end(**str)) *str=NULL;
}
/**
   remove "" or '' around a string from config file. Accept whole string if
quotes are not found.  */
static char* strextract(const char* data){
	const char *start=data, *end=0;
	trim_string(&start, &end);
	return mystrndup(start, end-start);
	/*
	size_t slen=strlen(data);
	while(slen>0&&isspace(data[0])){
		data++;
		slen--;
	}
	while(slen>0&&isspace(data[slen-1])){
		slen--;
	}
	if(!slen) return NULL;
	if(slen>1 && (data[0]=='"'||data[0]=='\'')){
		if(data[slen-1]!=data[0]){
			warning("Record is {%s}, quotes should come in pair\n", data);
			return strdup(data);
		} else{
			char* res=strdup(data+1);
			res[slen-2]='\0';
			if(is_end(res[0])){
				free(res);
				res=NULL;
			}
			return res;
		}
	} else{
		return strdup(data);
	}*/
}
/**
   Remove comment (after #), leading spaces, and trailing line break and spaces from a
   string.
 */
static char* squeeze(char* line){
	char* sline, * comment;
	if(!line){
		sline=NULL;
	} else{
	/*Remove comment*/
		comment=strchr(line, '#');
		if(comment){
			comment[0]='\0';
		}
		/*Remove trailing linebreak, semi-colon, and spaces*/
		int nread=strlen(line)-1;
		while(nread>=0&&(isspace(line[nread])||is_end(line[nread]))){
			line[nread]='\0';
			nread--;
		}
		/*Remove leading spaces*/
		sline=line;
		while(isspace(sline[0])) sline++;
		if(is_end(sline[0]))  sline=NULL;
	}
	return sline;
}
static FILE* fpout=NULL;
static void print_key(const void* key, VISIT which, int level){
	const STORE_T* store=*((const STORE_T**)key);
	(void)level;
	if(which==leaf||which==postorder){
		if(fpout){
			if(!store->priority){
				fprintf(fpout, "#");
			}
			fprintf(fpout, "%s=", store->key);
			if(store->data&&strcmp(store->data, "ignore")){
				fprintf(fpout, "%s\n", store->data);
			} else{
				fprintf(fpout, "\n");
			}
		}
		if(store->used!=1&&(!store->data||strcmp(store->data, "ignore"))){
			if(store->used==0){
				if(PERMISSIVE){
					warning("key \"%s\" is not recognized, value is %s\n", store->key, store->data);
				} else{
					error("key \"%s\" is not recognized, value is %s\n", store->key, store->data);
				}
			} else if(store->used!=1){
				if(PERMISSIVE){
					warning("Key %s is used %ld times\n", store->key, store->used);
				} else{
					error("Key %s is used %ld times\n", store->key, store->used);
				}
			}
		}
	}
}
static void delete_leaf(const void* key, VISIT which, int level){
	(void)level;
	if(which==leaf){
		STORE_T* store=*((STORE_T**)key);
		tdelete(store, &MROOT, key_cmp);
		free(store->key);
		free(store->data);
		free(store);
	}
}
void erase_config(){
	while(MROOT){
		twalk(MROOT, delete_leaf);
	}
	nused=0;
	nstore=0;
	free(default_config); default_config=NULL;
}
/**
   Save all configs to file and check for unused config options.
 */
void close_config(const char* format, ...){
	format2fn;
	const char* fnout=format?fn:NULL;
	if(MROOT){
		info("Used %ld of %ld supplied keys\n", nused, nstore);
		if(fnout&&strlen(fnout)>0&&!disable_save) fpout=fopen(fnout, "w");
		if(fpout && default_config) fprintf(fpout, "include=%s\n", default_config);
		twalk(MROOT, print_key);
		if(fpout){ fclose(fpout); fpout=NULL; }
	}
	erase_config();
}
/**
   Start the read config process by opening .conf files and fill the entries in
   a hash table. A key has a priority or 0 or higher. A new key with same or
   higher priority can override previous entry. priority is 0 for default configurations
 */
void open_config(const char* config_in, /**<[in]The .conf file to read*/
	const char* prefix,    /**<[in]if not NULL, prefix the key with this.*/
	int priority     /**<[in]Priorities of keys.*/
){
	if(!config_in) return;
	FILE* fd=NULL;
	char* config_file=NULL;
	if(check_suffix(config_in, ".conf")){
		config_file=find_file(config_in);
		if(!config_file||!(fd=fopen(config_file, "r"))){
			error("Cannot open file %s for reading.\n", config_file);
		}
		if(!priority && !default_config){//used in close_config
			default_config=strdup(config_in);
		}
	} else{
		config_file=strdup(config_in);
		parse_argopt(config_file, NULL);
		char* end;
		//Remove trailing space
		for(end=config_file+strlen(config_file); end>=config_file; end--){
			if(isspace((int)*end)) *end='\0';
			else break;
		}
		if(end<config_file){
			warning("config %s is empty\n", config_in);
			free(config_file);
			return;
		}
	}

	char* sline=NULL;
	int countnew=0;
	int countold=0;
	int countskip=0;
#define MAXLN 10000
	char ssline[MAXLN];
	ssline[0]='\0';/*stores the available line. */
	char line[MAXLN];//line concatenates to ssline if ends with /
	char* config_line=config_file;//contains config lines
	while(1){
		if(fd){/*read from file*/
			if(!fgets(line, MAXLN, fd)) break;
		} else{/*read from string*/
			if(!config_line) break;
			char* p0=strchr(config_line, '\n');
			const int len=p0?(p0-config_line):(MAXLN-1);
			if(len+1>MAXLN){
				error("Input line is too long. Please make MAXLN larger to accomodate.\n");break;
			}else{
				strncpy(line, config_line, len); line[len]='\0';
			}
			if(p0){
				config_line=p0+1;//start of next line
			} else{
				config_line=NULL;//no more
			}
		}
		sline=squeeze(line);
		if(!sline||is_end(sline[0]))/*skip empty lines. */
			continue;
		if(sline){
			if((strlen(sline)+strlen(ssline))>=MAXLN){
				error("Input line is too long. Please make MAXLN larger to accomodate.\n");
			}
			strcat(ssline, sline);
		}
		/*lines ended with \ will continue on next line */
		if(ssline[strlen(ssline)-1]=='\\'){
			ssline[strlen(ssline)-1]='\0';
			continue;
		}
		char* eql=strchr(ssline, '=');
		if(!eql){//no equal sign
			if(check_suffix(ssline, ".conf")){
				char* embeded=strextract(ssline);
				open_config(embeded, prefix, priority);
				free(embeded);
			} else if(!strcmp(ssline, "__reset__")){
				if(nstore>0){
					info("Replacing all existing input\n");
					erase_config();
				}
				countnew=0;
				countold=0;
				countskip=0;
			} else if(!strcmp(ssline, "__default__")){
				priority=0;//this file contains default setup.
			} else{
				error("Input (%s) is not valid\n", ssline);
			}
			ssline[0]='\0';
			continue;
		}
		int append=0;
		if(eql[-1]=='+'){
			append=1;//append to key
			eql[-1]='\0';
		}
		eql[0]='\0';
		char* var0=ssline;
		strtrim(&var0);
		const char* var=var0;//so we can assign a const string to it in RENAME.

		char* value=eql+1;
		strtrim(&value);

		if(!var||strlen(var)==0){
			error("Line '%s' is invalid\n", line);
		} else if(!strcmp(var, "path")||!strcmp(var, "PATH")){
			char* val2=strextract(value);
			addpath2(val2, 99);
			info("addpath %s\n", val2);
			free(val2);
		} else if(!strncmp(var, "MAOS_", 5)){
			//This is environment variable.
			if(!value){
				unsetenv(var);//not effective for read_sys_env()
			}else{
				setenv(var, value, 1);
				read_sys_env();
			}
		} else if(!strcmp(var, "include")){
			/*dbg("Opening embeded config file %s\n",value); */
			char* embeded=strextract(value);
			if(embeded){
				open_config(embeded, prefix, priority);
				free(embeded);
			}
		} else{
#if COMPATIBILITY == 1	    
			/*
			Compatibility mode: rename old key names to new key names. Will
			remove in the future.
			*/
			/*RENAME(atm.fractal, atm.method);*/
			RENAME(atm.zadeg, sim.zadeg);
			/*Added on 2011-04-28 */
			/*RENAME(dbg.noatm, sim.noatm);
			RENAME(dbg.fitonly, sim.fitonly);
			RENAME(dbg.evlol, sim.evlol);
			RENAME(sim.fitonly, sim.idealfit);
			IGNORE(sim.servotype_hi);
			IGNORE(sim.servotype_lo);
			IGNORE(sim.epfocus);
			IGNORE(dbg.dxonedge);
			IGNORE(sim.ahst_ttr);
			RENAME(sim.gtypeII_lo, sim.eplo);
			RENAME(sim.epngs, sim.eplo);
			RENAME(sim.apngs, sim.aplo);
			RENAME(dbg.splitlrt, tomo.splitlrt);
			RENAME(tomo.split_wt, tomo.ahst_wt);
			RENAME(tomo.split, recon.split);
			RENAME(evl.opdcov, evl.cov);
			RENAME(evl.psfpttr, evl.pttr);*/
			/*Added 2017-11-27*/
			RENAME(sim.apdm, sim.aphi);
			RENAME(sim.epdm, sim.ephi);
			RENAME(sim.aldm, sim.alhi);
			RENAME(powfs.neaspeckle, powfs.neaextra);
			RENAME(tomo.cxx, tomo.cxxalg);
			RENAME(sim.ahstfocus, tomo.ahst_focus);
			RENAME(powfs.phytype, powfs.phytype_recon);
			RENAME(powfs.phytypesim, powfs.phytype_sim);
			RENAME(powfs.phytypesim2, powfs.phytype_sim2);
			RENAME(dbg.ncpa_nouncorr, dbg.ncpa_rmsci);
			RENAME(powfs0_llt.colsimdtrat, powfs0_llt.coldtrat);
			RENAME(recon.psddtrat, recon.psddtrat_hi);
			RENAME(powfs.ncomp, powfs.notf);
			IGNORE(powfs.radrot);
#endif
			STORE_T* store=mycalloc(1, STORE_T);
			if(prefix){
				store->key=stradd(prefix, var, NULL);
			} else{
				store->key=strdup(var);
			}

			if(value&&strlen(value)>0){
				store->data=strdup(value);
			} else{
				store->data=NULL;
			}
			store->used=0;
			store->priority=priority;
			void* entryfind=tfind(store, &MROOT, key_cmp);
			if(entryfind){
			/*same key found */
				STORE_T* oldstore=*(STORE_T**)entryfind;
				int diffval=(((oldstore->data==NULL||store->data==NULL)
					&&(oldstore->data!=store->data))||
					((oldstore->data!=NULL&&store->data!=NULL)
						&&strcmp(oldstore->data, store->data)));
				if(append){
					/*concatenate new value with old value for arrays. both have to start/end with [/]*/
					const char* olddata=oldstore->data;
					const char* newdata=store->data;
					const int nolddata=strlen(olddata);
					const int nnewdata=strlen(newdata);
					if(olddata[0]!='['||olddata[nolddata-1]!=']'){
						error("olddata='%s' should be encapsulated by bare [].\n", olddata);
					} else if(newdata[0]!='['||newdata[nnewdata-1]!=']'){
						error("newdata='%s' should be encapsulated by bare [].\n", newdata);
					} else{
						oldstore->data=myrealloc(oldstore->data, (nolddata+nnewdata), char);
						oldstore->data[nolddata-1]=oldstore->data[nolddata-2]=='['?0:' ';
						strncat(oldstore->data, newdata+1, nnewdata-1);
						oldstore->priority=priority;
					}
				} else if(diffval){
					if(oldstore->priority>priority){
						countskip++;
						info("Not overriding %-20s\t%s by %s\n", store->key, oldstore->data, store->data);
						//Skip the entry.
					} else{
						info("Overriding %-20s\t%s --> %s\n", store->key, oldstore->data, store->data);
						/*free old value */
						free(oldstore->data);
						/*move pointer of new value. */
						oldstore->data=store->data; store->data=NULL;
						oldstore->priority=priority;
						oldstore->used=store->used;
					}
				}
				countold++;
				free(store->data);
				free(store->key);
				free(store);
			} else{
			/*new key */
				if(!tsearch(store, &MROOT, key_cmp)){
					error("Error inserting to tree\n");
				}
				countnew++;
				nstore++;
			}
		}
		ssline[0]='\0';
	}
	info("loaded %3d (%3d new) records from '%s'\n", countnew+countold, countnew, fd?config_file:"command line");
	if(fd){
		fclose(fd);
	}
	free(config_file);
#undef MAXLN
}
/**
   Get the record number of a key.

   Value of mark:
   1: mark the record as read.
   0: do not mark or check record existance
   -1:do not mark but error if record is not found.
 */
static const STORE_T* getrecord(char* key, int mark){
	STORE_T store;
	void* found=NULL;
	strtrim(&key);
	store.key=key;
	if((found=tfind(&store, &MROOT, key_cmp))){
		if(mark>0){
			if((*(STORE_T**)found)->used){
				error("This record %s is already read\n", key);
			}
			(*(STORE_T**)found)->used++;
			nused++;
		}
		const char* data=(*(STORE_T**)found)->data;
		if(!mystrcmp(data, "ref:")){
			//This key references another key's value
			char* key2=mystrdup(data+4);
			const STORE_T* refed=getrecord(key2, -1);
			free(key2);
			return refed;
		}
	} else if(mark){
		print_file("change.log");
		error("Record %s not found\n", key);
	}
	return found?(*(STORE_T**)found):0;
}
/**
   Check whether a have a record of a key.
 */
int readcfg_peek(const char* format, ...){
	/*Check whether key exists */
	format2key;
	return getrecord(key, 0)?1:0;
}
/**
   Check the size of an array input
*/
int readcfg_peek_n(const char* format, ...){
	format2key;
	const STORE_T* store=getrecord(key, 0);
	if(!store) return 0;
	const char* sdata=store->data;
	const char* startptr=strchr(sdata, '[');
	const char* endptr=strchr(sdata, ']');
	if(!startptr) startptr=sdata;
	if(!endptr) endptr=startptr+strlen(sdata)-1;
	const char* quote=strchr(startptr, '"');
	int count=0;
	if(quote&&quote<endptr){/*this is string array */
		char** ret=NULL;
		count=readstr_strarr(&ret, 0, 0, sdata);
		for(int i=0; i<count; i++){
			free(ret[i]);
		}
		free(ret);
	} else{/*this is numerical array */
		void* ret;
		count=readstr_numarr(&ret, NULL, NULL, 0, 0, M_REAL, sdata);
		free(ret);
	}
	return count;
}
/**
 * Ignore an entry
 * */
void readcfg_ignore(const char *format, ...){
	format2key;
	getrecord(key, 1);
}
/**
   Check whether the record is overriden by user supplied conf files.
*/
int readcfg_peek_override(const char* format, ...){
	/*Check whether key exists */
	format2key;
	const STORE_T* store=getrecord(key, 0);
	if(!store){
		return 0;
	} else{
		return store->priority;
	}
}
/**
   Obtain a string value from the key.
 */
char* readcfg_str(const char* format, ...){
	format2key;
	char* data=NULL;
	const STORE_T* store=getrecord(key, 1);
	const char* sdata=store->data;
	if(sdata&&strlen(sdata)>0){
		data=strextract(sdata);
	}
	return data;
}

/**
   Read as an lmat.
 */
lmat *readcfg_lmat(int n, int relax, const char *format, ...){
	format2key;
	long* val=NULL;
	int nx, ny;
	readstr_numarr((void**)&val, &nx, &ny, n, relax, M_LONG, getrecord(key, 1)->data);
	lmat* res=NULL;
	if(nx && ny){
		res=lnew_do(nx, ny, val, mem_new(val));
	}
	return res;
}
/**
   Read as a dmat. It can be a file name or an array.
 */
dmat* readstr_dmat(int n, ///[in]Number of elements requested
				int relax, ///[in]1: allow fewer values and fill the rest
				const char *str///[in]input
				){
	if(!str){
		return NULL;
	}
	dmat* res=NULL;
	char* fn=strextract(str);
	if(check_suffix(fn, ".bin.gz")||check_suffix(fn, ".fits.gz")||
		check_suffix(fn, ".bin")||check_suffix(fn, ".fits")){
		if(!(res=dread("%s", fn))){
			error("Read %s failed\n", fn);
		}
		if(n>0 && PN(res)!=n){
			if((relax==2&& n>0) || (relax==1 && n==1)){//resize and fill remaining values
				long n2=PN(res);
				dresize(res, n, 1);
				for(long i=n2; i<n; i++){
					P(res,i)=P(res,n2-1);
				}
			}else{
				error("{%s}: need %d values, got %ld\n", fn, n, PN(res));
			}
		}
	} else{
		int nx, ny;
		real* val=NULL;
		real** pval=&val;
		readstr_numarr((void**)pval, &nx, &ny, n, relax, M_REAL, fn);
		if(nx&&ny){
			res=dnew_do(nx, ny, val, mem_new(val));
		}
	}
	free(fn);
	return res;
}

/**
   Read as a dmat. It can be a file name or an array.
 */
dmat* readcfg_dmat(int n, int relax, const char* format, ...){
	format2key;
	return readstr_dmat(n, relax, getrecord(key, 1)->data);
}

/**
   Read string array of len elements
*/
int readcfg_strarr(char*** ret, int len, int relax, const char* format, ...){
	format2key;
	return readstr_strarr((char ***)ret, len, relax, getrecord(key, 1)->data);
}

/**
   Read integer array of len elements
*/
int readcfg_intarr(int** ret, int len, int relax, const char* format, ...){
	format2key;
	return readstr_numarr((void**)ret,  NULL, NULL, len, relax,  M_INT, getrecord(key, 1)->data);

}
/**
   Read integer array of len elements
*/
int readcfg_dblarr(real **ret, int len, int relax, const char *format, ...){
	format2key;
	return readstr_numarr((void **)ret, NULL, NULL, len, relax, M_REAL, getrecord(key, 1)->data);
}
/**
   Read integer
*/
int readcfg_int(const char* format, ...){
	format2key;
	char* val;
	char* endstr;
	real ans=0;
	if(!(val=getrecord(key, 1)->data)||isnan(ans=readstr_num(val, &endstr))||endstr[0]!='\0'||fabs(ans-(int)ans)>EPS){
		error("Invalid data: %s=%s\n", key, val);
	}
	return (int)ans;
}
/**
   Read real
*/
real readcfg_dbl(const char* format, ...){
	format2key;
	char *val; 
	char *endstr;
	real ans=0;
	if(!(val=getrecord(key, 1)->data)||isnan(ans=readstr_num(val, &endstr))||endstr[0]!='\0'){
		error("Invalid data: %s=%s\n", key, val);
	}
	return ans;
}

/**
   Read dcell
*/
dcell* readcfg_dcell(const char* format, ...){
	format2key;
	const char* str=getrecord(key, 1)->data;
	if(str){
		return dcellread("%s", str);
	} else{
		return NULL;
	}
}
