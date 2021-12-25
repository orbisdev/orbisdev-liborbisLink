/*
#  ____   ____    ____         ___ ____   ____ _     _
# |    |  ____>   ____>  |    |        | <____  \   /
# |____| |    \   ____>  | ___|    ____| <____   \_/   ORBISDEV Open Source Project.
#------------------------------------------------------------------------------------
# Copyright 2010-2020, orbisdev - http://orbisdev.github.io
# Licenced under MIT license
# Review README & LICENSE files for further details.
*/
/*
	patching piglet code from flatz https://github.com/flatz/ps4_gl_test
*/
#include <stdio.h>
#include <user_mem.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <orbisdev.h>
#include "orbislink.h"
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <unistd.h>
#include <sqlite3.h>


static int s_piglet_module = -1;
static int s_shcomp_module = -1;

const char* sandboxWord=NULL;
char orbislink_insert_config[256];
OrbisGlobalConf globalConf;


char debugnetIp[16];
int debugnetPort;
int debugnetLogLevel;
char nfsUrl[256];
int shaderCompilerEnabled=0;
int configPopulated=0;

char *default_vertex_shader;
int default_vertex_shader_length;
char *default_fragment_shader;
int default_fragment_shader_length;

int initialDbConfigDone=0;


bool get_module_base(const char* name, uint64_t* base, uint64_t* size, int level)
{
	SceKernelModuleInfo moduleInfo;
	int ret;

	ret=sceKernelGetModuleInfoByName(name,&moduleInfo);
	if(ret)
	{
		debugNetPrintf(level,"[ORBISLINK][%s][%d] sceKernelGetModuleInfoByName(%s) failed: 0x%08X\n",__FUNCTION__,__LINE__,name,ret);
		goto err;
	}
	if(base)
	{
		*base=(uint64_t)moduleInfo.segmentInfo[0].address;
	}
	if(size)
	{
		*size=moduleInfo.segmentInfo[0].size;
	}
	return true;
	err:
	return false;
}

bool patch_module(const char* name, module_patch_cb_t* cb, void* arg,int level)
{
	uint64_t base, size;
	int ret;
	if(!get_module_base(name, &base, &size,level))
	{
		debugNetPrintf(level,"[ORBISLINK][%s][%d] get_module_base return error\n",__FUNCTION__,__LINE__);
		goto err;
	}
	debugNetPrintf(level,"[ORBISLINK][%s][%d] module base=0x%08X size=%ld\n",__FUNCTION__,__LINE__,base,size);
	ret=sceKernelMprotect((void*)base, size, PROT_READ | PROT_WRITE | PROT_EXEC);
	if(ret)
	{
		debugNetPrintf(level,"[ORBISLINK][%s][%d] sceKernelMprotect(%s) failed: 0x%08X\n",__FUNCTION__,__LINE__,name,ret);
		goto err;
	}
	debugNetPrintf(level,"[ORBISLINK][%s][%d] patching module\n",__FUNCTION__,__LINE__);
	if(cb)
	{
		(*cb)(arg,(uint8_t*)base,size);
	}
	return true;
	err:
	return false;
}

void orbisLinkUnloadPigletModules(int level)
{

	if(shaderCompilerEnabled && s_shcomp_module>0)
	{
		sceKernelStopUnloadModule(s_shcomp_module, 0, NULL, 0, NULL, NULL);
		s_shcomp_module=-1;
	}
	if(s_piglet_module>0)
	{
		sceKernelStopUnloadModule(s_piglet_module, 0, NULL, 0, NULL, NULL);
		s_piglet_module=-1;
	}

}

int orbisLinkLoadPigletModules(int flagPiglet,int level)
{
	int ret;
	if(flagPiglet>0)
	{
		ret=sceKernelLoadStartModule(MODULE_PATH_PREFIX "/" PIGLET_MODULE_NAME,0,NULL,0,NULL,NULL);
		if(ret<0)
		{
			debugNetPrintf(level,"[ORBISLINK][%s][%d] sceKernelLoadStartModule(%s) failed: 0x%08X\n",__FUNCTION__,__LINE__,PIGLET_MODULE_NAME,ret);
			return ORBISLINK_ERROR_LOADING_PIGLET;
		}
		s_piglet_module=ret;
		if(flagPiglet>=2)
		{
			ret=sceKernelLoadStartModule(MODULE_PATH_PREFIX "/" SHCOMP_MODULE_NAME,0,NULL,0,NULL,NULL);
			if(ret<0)
			{
				debugNetPrintf(level,"[ORBISLINK][%s][%d] sceKernelLoadStartModule(%s) failed: 0x%08X\n",__FUNCTION__,__LINE__,SHCOMP_MODULE_NAME,ret);
				sceKernelStopUnloadModule(s_piglet_module, 0, NULL, 0, NULL, NULL);
				return ORBISLINK_ERROR_LOADING_SHCOMP;
			}
			shaderCompilerEnabled=1;
			s_shcomp_module=ret;
		}
		ret=ORBISLINK_OK;
	}
	else
	{
		char filePath[256];
		sandboxWord=sceKernelGetFsSandboxRandomWord();
		if(!sandboxWord)
		{
			debugNetPrintf(level,"[ORBISLINK][%s][%d] sceKernelGetFsSandboxRandomWord failed: \n",__FUNCTION__,__LINE__);
			return ORBISLINK_ERROR_GET_SANDBOXWORD;
		}
		snprintf(filePath, sizeof(filePath), "/%s/common/lib/%s", sandboxWord, PIGLET_MODULE_NAME);
		ret=sceKernelLoadStartModule(filePath,0,NULL,0,NULL,NULL);
		if(ret<0)
		{
			debugNetPrintf(level,"[ORBISLINK][%s][%d] sceKernelLoadStartModule(%s) failed: 0x%08X\n",__FUNCTION__,__LINE__,PIGLET_MODULE_NAME,ret);
			return ORBISLINK_ERROR_LOADING_PIGLET_RETAIL;
		}
		shaderCompilerEnabled=0;
		s_piglet_module=ret;
		ret=ORBISLINK_OK;
	}
	return ret;
}

/* XXX: patches below are given for Piglet module from 4.74 Devkit PUP */
static void pgl_patches_cb(void* arg, uint8_t* base, uint64_t size)
{
	/* Patch runtime compiler check */
	const uint8_t p_set_eax_to_1[] = { 0x31, 0xC0, 0xFF, 0xC0, 0x90,};
	memcpy(base + 0x5451F, p_set_eax_to_1, sizeof(p_set_eax_to_1));

	/* Tell that runtime compiler exists */
	*(uint8_t*)(base + 0xB2DEC) = 0;
	*(uint8_t*)(base + 0xB2DED) = 0;
	*(uint8_t*)(base + 0xB2DEE) = 1;
	*(uint8_t*)(base + 0xB2E21) = 1;

	/* Inform Piglet that we have shader compiler module loaded */
	*(int32_t*)(base + 0xB2E24) = s_shcomp_module;
}

static bool do_patches(int level)
{
	if(!patch_module(PIGLET_MODULE_NAME, &pgl_patches_cb, NULL,level))
	{
		debugNetPrintf(level,"[ORBISLINK][%s][%d] %s Unable to patch PGL module.\n",__FUNCTION__,__LINE__);
		return false;
	}
	return true;
}

static void cleanup(void)
{
	orbisLinkUnloadPigletModules(DEBUGNET_DEBUG);
	
	sceSysmoduleUnloadModuleInternal(SCE_SYSMODULE_INTERNAL_SYSTEM_SERVICE);

}
int orbisLinkCopyModulesFromNfs(const char *name)
{
	int ret;
	int fd;
	struct stat sb;
	int size;
	char path[256];
	unsigned char *mod_buffer=NULL;
	sprintf(path,"%s/%s",MODULE_PATH_PREFIX,name);

	

	fd=orbisNfsOpen(name,O_RDONLY,0777);
	if(fd<0)
	{
		debugNetPrintf(DEBUGNET_ERROR,"[ORBISLINK][%s][%d] error opening %s module from your nfs server\n",__FUNCTION__,__LINE__,name);
		return -1;
	}
	size=orbisNfsLseek(fd,0,SEEK_END);
	orbisNfsLseek(fd,0,SEEK_SET);
	if(size<0)
	{
		debugNetPrintf(DEBUGNET_ERROR,"[ORBISLINK][%s][%d] Failed to read size of file %s\n",__FUNCTION__,__LINE__,name);
		orbisNfsClose(fd);
		return -1;
	}
	mod_buffer=malloc(sizeof(unsigned char)*size);
	if(mod_buffer==NULL)
	{
		debugNetPrintf(DEBUGNET_ERROR,"[ORBISLINK][%s][%d] Failed to allocate %d bytes\n",__FUNCTION__,__LINE__,size);
		orbisNfsClose(fd);
		return -1;
	}
	if(size>=1024*1024)
	{
		int numread=size/(1024*1024);
		int lastread=size%(1024*1024);
		int i,j;
		for(j=0;j<numread;j++)
		{
			if(j<numread-1)
			{
				i=orbisNfsRead(fd,mod_buffer+j*(1024*1024),(1024*1024));
			}
			else
			{
				i=orbisNfsRead(fd,mod_buffer+j*(1024*1024),(1024*1024)+lastread);
			}
			if(i<0)
			{
				debugNetPrintf(DEBUGNET_ERROR,"[ORBISLINK][%s][%d] nfs_read, data read error\n",__FUNCTION__,__LINE__);
				orbisNfsClose(fd);
				free(mod_buffer);
				return -1;
			}
			debugNetPrintf(DEBUGNET_DEBUG,"[ORBISLINK][%s][%d] orbisNfsRead: chunk %d  read %d\n",__FUNCTION__,__LINE__,j,i);
		}
	}
	else
	{
		ret=orbisNfsRead(fd,mod_buffer,size);
		if(ret!=size)
		{
			debugNetPrintf(DEBUGNET_ERROR,"[ORBISLINK][%s][%d] Failed to read content of file %s requested %d returned ret\n",__FUNCTION__,name,size,ret);
			orbisNfsClose(fd);
			free(mod_buffer);
			return -1;
		}  
	}
	orbisNfsClose(fd);
	fd=-1;
	fd=sceKernelOpen(path,O_WRONLY|O_CREAT|O_TRUNC,0777);
	if(fd<0)
	{
		debugNetPrintf(DEBUGNET_ERROR,"[ORBISLINK][%s][%d] error sceKernelOpen err 0x%08X opening %s\n",__FUNCTION__,__LINE__,fd,path);
		free(mod_buffer);
		return -1;
	}
	ret=sceKernelWrite(fd,mod_buffer,size);
	if(ret!=size)
	{
		debugNetPrintf(DEBUGNET_ERROR,"[ORBISLINK][%s][%d] sceKernelWrite err 0x%08X to write content of file %s\n",__FUNCTION__,__LINE__,ret,name);
		orbisNfsClose(fd);
		free(mod_buffer);
		return -1;	
	}
	sceKernelClose(fd);
	sceKernelSync();
	free(mod_buffer);
	
	return ORBISLINK_OK;
}

int orbisLinkUploadPigletModules()
{
	
	int ret;
	struct stat sb;
	char path[256];
	sprintf(path,"%s/%s",MODULE_PATH_PREFIX,PIGLET_MODULE_NAME);

	ret=sceKernelStat(path,&sb);
	if(ret!=0)
	{
		ret=orbisLinkCopyModulesFromNfs(PIGLET_MODULE_NAME);
		if(ret==-1)
		{
			shaderCompilerEnabled=0;
			debugNetPrintf(DEBUGNET_ERROR,"[ORBISLINK][%s][%d] %s no valid module we will use default piglet without shader compiler\n",__FUNCTION__,__LINE__,PIGLET_MODULE_NAME);
			return ORBISLINK_ERROR_UPLOADING_PIGLET;
		}
		debugNetPrintf(DEBUGNET_DEBUG,"[ORBISLINK][%s][%d] %s module already on PlayStation file system\n",__FUNCTION__,__LINE__,PIGLET_MODULE_NAME);
	}
	char path1[256];
	sprintf(path1,"%s/%s",MODULE_PATH_PREFIX,PIGLET_MODULE_NAME);
	ret=sceKernelStat(path1,&sb);
	if(ret!=0)
	{
		ret=orbisLinkCopyModulesFromNfs(SHCOMP_MODULE_NAME);
		if(ret==-1)
		{
			shaderCompilerEnabled=0;
			debugNetPrintf(DEBUGNET_ERROR,"[ORBISLINK][%s][%d] %s no valid module for shader compiler\n",__FUNCTION__,__LINE__,SHCOMP_MODULE_NAME);
			return ORBISLINK_ERROR_UPLOADING_SHCOMP;
		}
		debugNetPrintf(DEBUGNET_DEBUG,"[ORBISLINK][%s][%d] %s module already on PlayStation file system\n",__FUNCTION__,__LINE__,SHCOMP_MODULE_NAME);
		shaderCompilerEnabled=1;
	}
	return ORBISLINK_OK;
}
int orbisLinkUploadSelf(const char *path)
{
	int ret;
	ret=sceKernelChmod("/data/self/system/common/lib/homebrew.self", 0000777);
	ret=orbisLinkCopyModulesFromNfs(path);
	if(ret==-1)
	{
		debugNetPrintf(DEBUGNET_ERROR,"[ORBISLINK][%s][%d] %s not uploaded\n",__FUNCTION__,__LINE__,FSELF_NAME);
		return ORBISLINK_ERROR_UPLOADING_SELF;
	}
	ret=sceKernelChmod("/data/self/system/common/lib/homebrew.self", 0000777);
	debugNetPrintf(DEBUGNET_DEBUG,"[ORBISLINK][%s][%d]sceKernelChmod return 0x%08X %s  already on PlayStation file system\n",__FUNCTION__,__LINE__,ret,FSELF_NAME);
	return ORBISLINK_OK;
}

int orbisLinkLoadModule(int moduleId)
{
	int ret=sceSysmoduleLoadModuleInternal(moduleId);
	if(ret!=0)
	{	
		printf("[ORBIS][ERROR][ORBISLINK][%s][%d] sceSysmoduleLoadModuleInternal  failed: 0x%08X\n",__FUNCTION__,__LINE__,ret);//,orbisLinkGetModuleName(moduleId));
		return -1;
	}
	return ORBISLINK_OK;
}

int orbisLinkLoadModulesGl(int compilerFlag, int level)
{
	int ret;
	
	ret=orbisLinkLoadPigletModules(compilerFlag,level);
	if(ret<0)
	{
		debugNetPrintf(DEBUGNET_ERROR,"[ORBISLINK][%s][%d] Error loading piglet module %d\n",__FUNCTION__,__LINE__,ret);
		return ret;
	}
	else
	{
		debugNetPrintf(DEBUGNET_DEBUG,"[ORBISLINK][%s][%d] piglet module loaded %d\n",__FUNCTION__,__LINE__,ret);
	}
		
	if(compilerFlag==2)
	{
		ret=do_patches(level);
		if(ret<0)
		{
			debugNetPrintf(DEBUGNET_ERROR,"[ORBISLINK][%s][%d] Error loading patching piglet module %d\n",__FUNCTION__,__LINE__,ret);
			return ORBISLINK_ERROR_PATCHING_PIGLET;
		}
	}
	return ORBISLINK_OK;
}

int orbisLinkLoadModulesVanilla(void)
{
	int ret=orbisLinkLoadModule(SCE_SYSMODULE_INTERNAL_SYSTEM_SERVICE);
	if(ret!=0)
	{
		return ORBISLINK_ERROR_LOADING_VANILLA_MODULE_INTERNAL_SYSTEM_SERVICE;
	}
	ret=orbisLinkLoadModule(SCE_SYSMODULE_INTERNAL_NET);
	if(ret!=0)
	{
		return ORBISLINK_ERROR_LOADING_VANILLA_MODULE_INTERNAL_NET;
	}
	ret=orbisLinkLoadModule(SCE_SYSMODULE_INTERNAL_USER_SERVICE);
	if(ret!=0)
	{
		return ORBISLINK_ERROR_LOADING_VANILLA_MODULE_INTERNAL_USER_SERVICE;
	}
	return ORBISLINK_OK;
}

int orbisLinkCreateSelfDirectories(void)
{
	struct stat sb;
	int ret;
	//check if directory structure for loading our self is already there
	ret=sceKernelStat("/data/self/system/common/lib",&sb);
	if(ret!=0)
	{
		//Sony sceKernelMkdir need to go dir by dir in path creation, /data must be there already
		ret=sceKernelMkdir("/data/self",0777);
		//if error(!=0) and if it not 0x80020011(aka already exist)
		if(ret!=0 && ret!=0x80020011)
		{
			printf("[ORBIS][ERROR][ORBISLINK][%s][%d] sceKernelMkdir(%s) failed: 0x%08X\n",__FUNCTION__,__LINE__,"/data/self",ret);
			return ORBISLINK_ERROR_CREATING_SELF_DIRECTORIES;
		}
		ret=sceKernelMkdir("/data/self/system",0777);
		//if error(!=0) and if it not 0x80020011(aka already exist)
		if(ret!=0 && ret!=0x80020011)
		{
			printf("[ORBIS][ERROR][ORBISLINK][%s][%d] sceKernelMkdir(%s) failed: 0x%08X\n",__FUNCTION__,__LINE__,"/data/self/system",ret);
			return ORBISLINK_ERROR_CREATING_SELF_DIRECTORIES;
		}
		ret=sceKernelMkdir("/data/self/system/commom",0777);
		//if error(!=0) and if it not 0x80020011(aka already exist)
		if(ret!=0 && ret!=0x80020011)
		{
			printf("[ORBIS][ERROR][ORBISLINK][%s][%d] sceKernelMkdir(%s) failed: 0x%08X\n",__FUNCTION__,__LINE__,"/data/self/system/common",ret);
			return ORBISLINK_ERROR_CREATING_SELF_DIRECTORIES;
		}
		ret=sceKernelMkdir("/data/self/system/common/lib",0777);
		if(ret!=0 && ret!=0x80020011)
		{
			printf("[ORBIS][ERROR][ORBISLINK][%s][%d] sceKernelMkdir(%s) failed: 0x%08X\n",__FUNCTION__,__LINE__,"/data/self/system/common/lib",ret);
			return ORBISLINK_ERROR_CREATING_SELF_DIRECTORIES;
		}
	}
	return ORBISLINK_OK;
}
int orbisLinkCheckConfig()
{
	int ret;
	struct stat sb;
	int fd;
	unsigned char *db_buffer;
	int db_size;
	//check if we have db populated
	
	ret=sceKernelStat(ORBISLINK_CONFIG_DB_PATH_HDD,&sb);
	if(ret!=0)
	{
		fd=sceKernelOpen(ORBISLINK_CONFIG_DB_PATH_APP,O_RDONLY,0);
		if(fd<0)
		{
			printf("[ORBIS][ERROR][ORBISLINK][%s][%d] error sceKernelOpen err 0x%08X opening %s\n",__FUNCTION__,__LINE__,fd,ORBISLINK_CONFIG_DB_PATH_APP);
			return -1;
		}
		db_size=sceKernelLseek(fd,0,SEEK_END);
		sceKernelLseek(fd,0,SEEK_SET);
		if(db_size<0)
		{
			printf("[ORBIS][ERROR][ORBISLINK][%s][%d] Failed to read size of file %s\n",__FUNCTION__,__LINE__,ORBISLINK_CONFIG_DB_PATH_APP);
			sceKernelClose(fd);
			return -1;
		}
		db_buffer=malloc(sizeof(unsigned char)*db_size);
		if(db_buffer==NULL)
		{
			printf("[ORBIS][ERROR][ORBISLINK][%s][%d] Failed to allocate %d bytes\n",__FUNCTION__,__LINE__,db_size);
			sceKernelClose(fd);
			return -1;
		}
		ret=sceKernelRead(fd,db_buffer,db_size);
		if(ret!=db_size)
		{
			printf("[ORBIS][ERROR][ORBISLINK][%s][%d] sceKernelRead err 0x%08X to read content of file %s\n",__FUNCTION__,__LINE__,ret,ORBISLINK_CONFIG_DB_PATH_HDD);
			sceKernelClose(fd);
			free(db_buffer);
			return -1;	
		}
		sceKernelClose(fd);
		fd=-1;
		fd=sceKernelOpen(ORBISLINK_CONFIG_DB_PATH_HDD,O_WRONLY|O_CREAT|O_TRUNC,0777);
		if(fd<0)
		{
			printf("[ORBIS][ERROR][ORBISLINK][%s][%d] error sceKernelOpen err 0x%08X opening %s\n",__FUNCTION__,__LINE__,fd,ORBISLINK_CONFIG_DB_PATH_HDD);
			free(db_buffer);
			return -1;
		}
		ret=sceKernelWrite(fd,db_buffer,db_size);
		if(ret!=db_size)
		{
			printf("[ORBIS][ERROR][ORBISLINK][%s][%d] sceKernelWrite err 0x%08X to write content of file %s\n",__FUNCTION__,__LINE__,ret,ORBISLINK_CONFIG_DB_PATH_HDD);
			sceKernelClose(fd);
			free(db_buffer);
			return -1;	
		}
		sceKernelClose(fd);
		sceKernelSync();
		free(db_buffer);
		sceKernelChmod(ORBISLINK_CONFIG_DB_PATH_HDD, 0000777);
	}
	else
	{
		initialDbConfigDone=1;
	}
	return ORBISLINK_OK;

}
int orbisLinkCreateConfigDirectories()
{
	struct stat sb;
	int ret;
	
	//check if directory structure for loading our self is already there
	ret=sceKernelStat(DB_PATH_PREFIX,&sb);
	if(ret!=0)
	{
		//Sony sceKernelMkdir need to go dir by dir in path creation, /data must be there already
		ret=sceKernelMkdir(DB_PATH_PREFIX,0777);
		if(ret!=0 && ret!=0x80020011)
		{
			printf("[ORBIS][ERROR][ORBISLINK][%s][%d] sceKernelMkdir(%s) failed: 0x%08X\n",__FUNCTION__,__LINE__,DB_PATH_PREFIX,ret);
			return -1;
		}
	}
	
	return ORBISLINK_OK;
}
void orbisLinkSQLiteCloseDb(sqlite3 *db)
{
	if(db!=NULL)
	{
		sqlite3_close(db);
	}
}
sqlite3 * orbisLinkSQLiteOpenDb(char *path,int flags)
{
	int ret;
	struct stat sb;
	sqlite3 *db=NULL;

	//check if our sqlite db is already present in our path
	ret=sceKernelStat(path,&sb);
	if(ret!=0)
	{
		printf("[ORBIS][ERROR][ORBISLINK][%s][%d] sceKernelStat(%s) failed: 0x%08X\n",__FUNCTION__,__LINE__,path,ret);
		goto sqlite_error_out;
	}
	ret=sqlite3_open_v2(path,&db,flags,NULL);
	sqlite_error_out:
	//return error is something went wrong errmsg on klogs
	if(ret!=SQLITE_OK)
	{
		printf("[ORBIS][ERROR][ORBISLINK][%s][%d] ERROR 0x%08X: %s\n",__FUNCTION__,__LINE__,ret,sqlite3_errmsg(db));
		return NULL;
	}
	else
	{
		return db;
	}
}
int orbisLinkSQLiteSetConfig(sqlite3 *db)
{
	int ret;
	char *errmsg=NULL;
	if(db==NULL)
	{
		return ORBISLINK_ERROR_SQLITE_DB;
	}
	ret=sqlite3_exec(db,ORBISLINK_CONFIG_DB_DROP_TABLE,NULL,NULL,&errmsg);
	if(ret!=SQLITE_OK)
	{
		printf("[ORBIS][ERROR][ORBISLINK][%s][%d] sqlite3_exec with sql %s failed: 0x%08X\n",__FUNCTION__,__LINE__,ORBISLINK_CONFIG_DB_DROP_TABLE,ret);
		goto sqlite_error_out;
	}
	ret=sqlite3_exec(db,ORBISLINK_CONFIG_DB_CREATE_TABLE,NULL,NULL,&errmsg);
	if(ret!=SQLITE_OK)
	{
		printf("[ORBIS][ERROR][ORBISLINK][%s][%d] sqlite3_exec with sql %s failed: 0x%08X\n",__FUNCTION__,__LINE__,ORBISLINK_CONFIG_DB_CREATE_TABLE,ret);
		goto sqlite_error_out;
	}
	ret=sqlite3_exec(db,orbislink_insert_config,NULL,NULL,&errmsg);
	if(ret!=SQLITE_OK)
	{
		printf("[ORBIS][ERROR][ORBISLINK][%s][%d] sqlite3_exec with sql %s failed: 0x%08X\n",__FUNCTION__,__LINE__,orbislink_insert_config,ret);
		goto sqlite_error_out;
	}
	sqlite_error_out:
	//return error is something went wrong errmsg on klogs
	if(ret!=SQLITE_OK)
	{
		printf("[ORBIS][ERROR][ORBISLINK][%s][%d] ERROR 0x%08X: %s\n",__FUNCTION__,__LINE__,ret,sqlite3_errmsg(db));
		ret=ORBISLINK_ERROR_POPULATING_CONFIG;
	}
	else
	{
		ret=ORBISLINK_OK;
	}
	sqlite3_free(errmsg);

	return ret;


}
int orbisLinkSQLiteGetConfig(sqlite3 *db)
{
	int ret;
	sqlite3_stmt *stmt =NULL;

	if(db==NULL)
	{
		return ORBISLINK_ERROR_SQLITE_DB;
	}
	//prepare select statement
	ret=sqlite3_prepare_v2(db,ORBISLINK_CONFIG_DB_SELECT,-1,&stmt,NULL);
	if (ret==SQLITE_OK)
	{
		//bind to first rowid
		sqlite3_bind_int(stmt,1,1);
	}
	else
	{
		printf("[ORBIS][ERROR][ORBISLINK][%s][%d] sqlite3_prepare_v2 with sql %s failed: 0x%08X\n",__FUNCTION__,__LINE__,ORBISLINK_CONFIG_DB_SELECT,ret);
		goto sqlite_error_out;
	}
	//retrieve data and populate
	ret=sqlite3_step(stmt);
	if(ret==SQLITE_ROW)
	{
		snprintf(debugnetIp,strlen(( char*)(sqlite3_column_text(stmt,1)))+1,"%s",( char*)(sqlite3_column_text(stmt,1)));
		debugnetPort=sqlite3_column_int(stmt,2);
		debugnetLogLevel=sqlite3_column_int(stmt,3);
		snprintf(nfsUrl,strlen(( char*)(sqlite3_column_text(stmt,4)))+1,"%s",( char*)(sqlite3_column_text(stmt,4)));
		configPopulated=1;
		printf("[ORBIS][ERROR][ORBISLINK][%s][%d] sqlite3_bind_int  %s %d %d %s\n",__FUNCTION__,__LINE__,debugnetIp,debugnetPort,debugnetLogLevel,nfsUrl);
	}
	//finalize statement
	ret=sqlite3_finalize(stmt);
	
	sqlite_error_out:
	//return error is something went wrong errmsg on klogs
	if(ret!=SQLITE_OK)
	{
		printf("[ORBIS][ERROR][ORBISLINK][%s][%d] ERROR 0x%08X: %s\n",__FUNCTION__,__LINE__,ret,sqlite3_errmsg(db));
		ret=ORBISLINK_ERROR_POPULATING_CONFIG;
	}
	else
	{
		ret=ORBISLINK_OK;
	}
	return ret;

}
int orbisLinkSQLiteGetShader(sqlite3 *db,int rowid,char **shader, int *shader_size)
{
	int ret;
	sqlite3_stmt *stmt =NULL;
	
	
	if(db==NULL)
	{
		return ORBISLINK_ERROR_SQLITE_DB;
	}
	//prepare select statement
	ret=sqlite3_prepare_v2(db,ORBISLINK_SHADERS_DB_SELECT,-1,&stmt,NULL);
	if(ret==SQLITE_OK)
	{
		sqlite3_bind_int(stmt,1,rowid);
	}
	else
	{
		printf("[ORBIS][ERROR][ORBISLINK][%s][%d] sqlite3_prepare_v2 with sql %s failed: 0x%08X\n",__FUNCTION__,__LINE__,ORBISLINK_SHADERS_DB_SELECT,ret);
		goto sqlite_error_out;
	}
	//retrieve data and populate
	ret=sqlite3_step(stmt);
	if(ret==SQLITE_ROW)
	{
		//snprintf(shader_name,strlen(( char*)(sqlite3_column_text(stmt,0))),"%s",( char*)(sqlite3_column_text(stmt,0)));
		*shader_size=sqlite3_column_bytes(stmt,1);
		printf("[ORBIS][DEBUG][ORBISLINK][%s][%d] shader %s with length: %d\n",__FUNCTION__,__LINE__,sqlite3_column_text(stmt,0),*shader_size);
		*shader=malloc(*shader_size);
		memcpy(*shader,sqlite3_column_blob(stmt,1),sqlite3_column_bytes(stmt, 1));

	}
	//finalize statement
	ret=sqlite3_finalize(stmt);

	sqlite_error_out:
	//return error is something went wrong errmsg on klogs
	if(ret!=SQLITE_OK)
	{
		printf("[ORBIS][ERROR][ORBISLINK][%s][%d] ERROR 0x%08X: %s\n",__FUNCTION__,__LINE__,ret,sqlite3_errmsg(db));
		ret=ORBISLINK_ERROR_POPULATING_CONFIG;
	}
	else
	{
		ret=ORBISLINK_OK;
	}
	return ret;
}
int orbisLinkSQLiteGetDefaultShaders(sqlite3 *db)
{
	int ret;
	if(db==NULL)
	{
		return ORBISLINK_ERROR_SQLITE_DB;
	}
	ret=orbisLinkSQLiteGetShader(db,1,&default_vertex_shader,&default_vertex_shader_length);
	if(ret<0)
	{
		return ORBISLINK_ERROR_SQLITE_DB_SHADERS;
	}
	ret=orbisLinkSQLiteGetShader(db,2,&default_fragment_shader,&default_fragment_shader_length);
	if(ret<0)
	{
		return ORBISLINK_ERROR_SQLITE_DB_SHADERS;
	}
	return ORBISLINK_OK;
}
int orbisLinkPopulateConfig(void)
{
	int ret;
	sqlite3 *db=NULL;

	ret=orbisLinkCheckConfig();
	if(ret<0)
	{
		return ORBISLINK_ERROR_POPULATING_CONFIG;
	}

	if(initialDbConfigDone==0)
	{
		db=orbisLinkSQLiteOpenDb(ORBISLINK_CONFIG_DB_PATH_HDD,SQLITE_OPEN_READWRITE);
	}
	else
	{
		db=orbisLinkSQLiteOpenDb(ORBISLINK_CONFIG_DB_PATH_HDD,SQLITE_OPEN_READONLY);
	}
	if(db==NULL)
	{
		return ORBISLINK_ERROR_POPULATING_CONFIG;
	}
	if(initialDbConfigDone==0)
	{
		ret=orbisLinkSQLiteSetConfig(db);
		//return error is something went wrong errmsg on klogs
		if(ret!=ORBISLINK_OK)
		{
			return ORBISLINK_ERROR_POPULATING_CONFIG;
		}
	}
	ret=orbisLinkSQLiteGetConfig(db);
	//return error is something went wrong errmsg on klogs
	if(ret!=ORBISLINK_OK)
	{
		return ORBISLINK_ERROR_POPULATING_CONFIG;
	}
	ret=orbisLinkSQLiteGetDefaultShaders(db);
	if(ret!=ORBISLINK_OK)
	{
		return ORBISLINK_ERROR_POPULATING_CONFIG;
	}
	orbisLinkSQLiteCloseDb(db);
	return ORBISLINK_OK;
}

int initOrbisLinkAppInternal(char *config)
{
	int ret;
	//loading vanilla base modules
	ret=orbisLinkLoadModulesVanilla();
	if(ret<0)
	{
		return ret;
	}
	//create self path needed to load our self and custom sprx
	ret=orbisLinkCreateSelfDirectories();
	if(ret<0)
	{
		return ret;
	}
	
	//create our orbislink configuration directory
	ret=orbisLinkCreateConfigDirectories();
	if(ret!=0)
	{
		return ORBISLINK_ERROR_POPULATING_CONFIG;
	}

	//populate config
	strncpy(orbislink_insert_config,config,strlen(config));
	ret=orbisLinkPopulateConfig();
	if(ret<0 || configPopulated==0)
	{
		return ORBISLINK_ERROR_POPULATING_CONFIG;
	}
	
	//init debugnet
	ret=debugNetInit(debugnetIp,debugnetPort,debugnetLogLevel);
	if(ret!=1)
	{	
    	return ORBISLINK_ERROR_LOADING_DEBUGNET;
	}
	globalConf.orbisLinkFlag=0;
    globalConf.confDebug=debugNetGetConf();
    
    //init orbisNfs
    ret=orbisNfsInit(nfsUrl);
    if(ret!=1)
    {
    	return ORBISLINK_ERROR_LOADING_ORBISNFS;
    }
    globalConf.confNfs=orbisNfsGetConf();
    return ORBISLINK_OK;

}

int initOrbisLinkAppInternalWithShaderCompiler(void)
{
	int ret;
	//loading vanilla base modules
	ret=orbisLinkLoadModulesVanilla();
	if(ret<0)
	{
		return ret;
	}
	//create self path needed to load our self and custom sprx
	ret=orbisLinkCreateSelfDirectories();
	if(ret<0)
	{
		return ret;
	}
	
	//create our orbislink configuration directory
	//ret=orbisLinkCreateConfigDirectories("/data/orbislink");
	//if(ret!=0)
	//{
	//	return -1;
	//}

	//populate config
	ret=orbisLinkPopulateConfig();
	if(ret<0 || configPopulated==0)
	{
		return ORBISLINK_ERROR_POPULATING_CONFIG;
	}
	
	//init debugnet
	ret=debugNetInit(debugnetIp,debugnetPort,debugnetLogLevel);
	if(ret!=1)
	{	
    	return ORBISLINK_ERROR_LOADING_DEBUGNET;
	}
	globalConf.orbisLinkFlag=0;
    globalConf.confDebug=debugNetGetConf();
    
    //init orbisNfs
    ret=orbisNfsInit(nfsUrl);
    if(ret!=1)
    {
    	return ORBISLINK_ERROR_LOADING_ORBISNFS;
    }
    globalConf.confNfs=orbisNfsGetConf();
    if(shaderCompilerEnabled==1)
    {
    	ret=orbisLinkUploadPigletModules();
    	if(ret<0)
    	{
			debugNetPrintf(DEBUGNET_ERROR,"[ORBISLINK][%s][%d] error uploading piglet modules so no shader compiler available\n",__FUNCTION__,__LINE__);
    		return ret;
    	}
    }
    return ORBISLINK_OK;

}

int initOrbisLinkApp(void)
{
	int ret;
	//loading vanilla base modules
	ret=orbisLinkLoadModulesVanilla();
	if(ret<0)
	{
		return ret;
	}
	ret=orbisLinkPopulateConfig();
	if(ret<0 || configPopulated==0)
	{
		return ORBISLINK_ERROR_POPULATING_CONFIG;
	}
	//init debugnet
	ret=debugNetInit(debugnetIp,debugnetPort,debugnetLogLevel);
	if(ret!=1)
	{	
    	return ORBISLINK_ERROR_LOADING_DEBUGNET;
	}
	globalConf.orbisLinkFlag=0;
    globalConf.confDebug=debugNetGetConf();	
    ret=orbisNfsInit(nfsUrl);
    if(ret!=1)
    {
    	return ORBISLINK_ERROR_LOADING_ORBISNFS;
    }
    globalConf.confNfs=orbisNfsGetConf();
	return ORBISLINK_OK;
}

int initOrbisLinkAppVanilla(void)
{
	int ret;
	ret=orbisLinkLoadModulesVanilla();
	if(ret<0)
	{
		return ret;
	}
	return ORBISLINK_OK;
}

int initOrbisLinkAppVanillaGl(void)
{
	int ret;
	//loading vanilla base modules
	ret=orbisLinkLoadModulesVanilla();
	if(ret<0)
	{
		return ret;
	}
	ret=orbisLinkPopulateConfig();
	if(ret<0 || configPopulated==0)
	{
		return ORBISLINK_ERROR_POPULATING_CONFIG;
	}
	debugNetInit(debugnetIp,debugnetPort,debugnetLogLevel);
	globalConf.orbisLinkFlag=0;
	globalConf.confDebug=debugNetGetConf();
	ret=orbisLinkLoadModulesGl(0,DEBUGNET_DEBUG);
	if(ret<0)
	{
		return ret;
	}
	return ORBISLINK_OK;
}

int initOrbisLinkAppVanillaGlWithShaderCompiler(void)
{
	int ret;
	//loading vanilla base modules
	ret=orbisLinkLoadModulesVanilla();
	if(ret<0)
	{
		return ret;
	}
	ret=orbisLinkPopulateConfig();
	if(ret<0 || configPopulated==0)
	{
		return ORBISLINK_ERROR_POPULATING_CONFIG;
	}
	debugNetInit(debugnetIp,debugnetPort,debugnetLogLevel);

	ret=orbisLinkLoadModulesGl(2,DEBUGNET_DEBUG);
	if(ret<0)
	{
		return ret;
	}
	debugNetPrintf(DEBUGNET_DEBUG,"[ORBISLINK][%s][%d] piglet modules patched\n",__FUNCTION__,__LINE__);
	return ORBISLINK_OK;
}

void finishOrbisLinkApp(void)
{
	cleanup();
}
