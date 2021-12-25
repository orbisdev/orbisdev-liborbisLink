/*
#  ____   ____    ____         ___ ____   ____ _     _
# |    |  ____>   ____>  |    |        | <____  \   /
# |____| |    \   ____>  | ___|    ____| <____   \_/   ORBISDEV Open Source Project.
#------------------------------------------------------------------------------------
# Copyright 2010-2020, orbisdev - http://orbisdev.github.io
# Licenced under MIT license
# Review README & LICENSE files for further details.
*/
#ifndef _ORBISLINK_H_
#define _ORBISLINK_H_

#include <orbisdev.h>
#include <debugnet.h>
#include <orbisNfs.h>
#include <orbisPad.h>
#include <orbisAudio.h>
#include <orbisKeyboard.h>

//to deprecate 
typedef struct OrbisGlobalConf
{
	void *conf; //deprecated
	OrbisPadConfig *confPad;
	OrbisAudioConfig *confAudio;
	OrbisKeyboardConfig *confKeyboard;
	void *confLink; //deprecated
	int orbisLinkFlag;
	debugNetConfiguration *confDebug;
 	OrbisNfsConfig *confNfs;
}OrbisGlobalConf;

typedef void module_patch_cb_t(void *arg, uint8_t *base, uint64_t size);

#define ORBISLINK_OK													   0
#define ORBISLINK_ERROR_LOADING_VANILLA_MODULE_INTERNAL_SYSTEM_SERVICE	-100
#define ORBISLINK_ERROR_LOADING_VANILLA_MODULE_INTERNAL_NET				-101
#define ORBISLINK_ERROR_LOADING_VANILLA_MODULE_INTERNAL_USER_SERVICE	-102

#define ORBISLINK_ERROR_CREATING_SELF_DIRECTORIES	-1
#define ORBISLINK_ERROR_POPULATING_CONFIG			-2
#define ORBISLINK_ERROR_LOADING_DEBUGNET			-3
#define ORBISLINK_ERROR_LOADING_ORBISNFS			-4
#define ORBISLINK_ERROR_UPLOADING_PIGLET			-5
#define ORBISLINK_ERROR_UPLOADING_SHCOMP			-6
#define ORBISLINK_ERROR_UPLOADING_SELF				-7
#define ORBISLINK_ERROR_LOADING_PIGLET 				-8
#define ORBISLINK_ERROR_LOADING_SHCOMP 				-9
#define ORBISLINK_ERROR_PATCHING_PIGLET 			-10
#define ORBISLINK_ERROR_GET_SANDBOXWORD 			-11
#define ORBISLINK_ERROR_LOADING_PIGLET_RETAIL 		-12
#define ORBISLINK_ERROR_SQLITE_DB 					-13
#define ORBISLINK_SHADERS_SQLITE_DB 				-14
#define ORBISLINK_ERROR_SQLITE_DB_SHADERS 			-15

#define ORBISLINK_CONFIG_DB_PATH_APP "/app0/media/orbislink_config.db"
#define ORBISLINK_CONFIG_DB_PATH_HDD "/data/orbislink/orbislink_config.db"

#define ORBISLINK_CONFIG_DB_DROP_TABLE  "DROP TABLE IF EXISTS orbislink_config"
#define ORBISLINK_CONFIG_DB_CREATE_TABLE    "CREATE TABLE orbislink_config( name TEXT, debugnetIp TEXT, debugnetPort INT,debugnetLogLevel INT,nfsUrl TEXT)"
#define ORBISLINK_CONFIG_DB_SELECT "SELECT * FROM orbislink_config where rowid=?"

#define ORBISLINK_SHADERS_DB_SELECT "SELECT * FROM orbislink_shaders where rowid=?"

#define PIGLET_MODULE_NAME "libScePigletv2VSH.sprx"
#define SHCOMP_MODULE_NAME "libSceShaccVSH.sprx"
#define FSELF_NAME "homebrew.self"

#define MODULE_PATH_PREFIX "/data/self/system/common/lib"
#define DB_PATH_PREFIX "/data/orbislink"

#ifdef __cplusplus
extern "C" {
#endif

//for apps only with vanilla modules(SYSTEM_SERVICE,NET,USER_SERVICES) loaded. Up to developer do the rest
int initOrbisLinkAppVanilla(void);
//for apps only with vanilla modules(SYSTEM_SERVICE,NET,USER_SERVICES) debugnet and piglet retail
int initOrbisLinkAppVanillaGl(void);
//for apps only with vanilla modules(SYSTEM_SERVICE,NET,USER_SERVICES) debugnet and piglet and shader compiler from devkit
int initOrbisLinkAppVanillaGlWithShaderCompiler(void);
//for apps with vanilla modules loaded(SYSTEM_SERVICE,NET,USER_SERVICES), debugnet and nfs
int initOrbisLinkApp(void);
//for orbislink loader 
int initOrbisLinkAppInternal(char *config);
//for orbislink loader if you have devkit piglet and shader compiler modules to upload from nfs to your console internal hdd at /data/self/system/common/lib
int initOrbisLinkAppInternalWithShaderCompiler(void);

int orbisLinkUploadSelf(const char *path);
int orbisLinkLoadModule(int moduleId);
int orbisLinkLoadModulesGl(int compilerFlag, int level);


OrbisGlobalConf *orbisLinkGetGlobalConf(void);
void finishOrbisLinkApp(void);

bool patch_module(const char *name, module_patch_cb_t *cb, void *arg, int level);

int orbisLinkLoadModulesVanilla(void);

#ifdef __cplusplus
}
#endif

#endif

