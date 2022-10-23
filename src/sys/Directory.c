/*
 | LuaRT - A Windows programming framework for Lua
 | Luart.org, Copyright (c) Tine Samir 2022.
 | See Copyright Notice in LICENSE.TXT
 |-------------------------------------------------
 | Directory.c | LuaRT Directory object implementation
*/

#include <Directory.h>
#include <File.h>
#include <Buffer.h>

#include "lrtapi.h"
#include <luart.h>
#include <stdlib.h>
#include <windows.h>
#include <io.h>
#include <limits.h>
#include <fcntl.h>
#include <shlwapi.h>

luart_type TDirectory;

//-------------------------------------[ Directory Constructor ]
LUA_CONSTRUCTOR(Directory) {
	DWORD dwAttrib;
	Directory *dir = calloc(1, sizeof(Directory));
	dir->type = TDirectory;
	init_fullpath(L, dir);
	lua_newinstance(L, dir, Directory);

	dwAttrib = GetFileAttributesW(dir->fullpath);
	dir->type = TDirectory;
	if (dwAttrib != INVALID_FILE_ATTRIBUTES)
		if ((dir->exists = dwAttrib & FILE_ATTRIBUTE_DIRECTORY) == FALSE)
			luaL_error(L, "'%s' is not a valid directory path", dir->fullpath);
	return 1;
}

//-------------------------------------[ Directory.make ]
static wchar_t *normalize(wchar_t* folder) {
	wchar_t *result = wcsdup(folder);
	int i = -1;
	while(folder[++i])
		if (folder[i] == L'/')
			result[i] = L'\\';
	return result;
}

BOOL make_path(wchar_t *folder) {
	wchar_t *end;
	int start = 0;
	wchar_t *path = normalize(folder);
	BOOL result = TRUE;
	
	while((end = wcschr(path+start, L'\\')))
	{
		*end = L'\0';
		if (*(end-1) != L':') {
			if(!CreateDirectoryW(path, NULL) && (GetLastError() != ERROR_ALREADY_EXISTS)) {
				result = FALSE;
				break;
			}
		}
		*end = L'\\';
		start = end-path+1;
	}
	free(path);
	return result;

}

//------- Append path utility
static wchar_t *append_path(wchar_t *str, wchar_t* path) {
	size_t len = wcslen(str) + wcslen(path)+2;
	wchar_t *result = calloc(sizeof(wchar_t)*len, 1);
	_snwprintf(result, len, L"%s/%s", str, path);
	return result;
}

LUA_METHOD(Directory, make) {
	Directory *dir = lua_self(L, 1, Directory);
	wchar_t *trailing = append_path(dir->fullpath, L"");;

	if (GetFileAttributesW(dir->fullpath) == INVALID_FILE_ATTRIBUTES) {
		dir->exists = make_path(trailing);
		lua_pushboolean(L, dir->exists);
	}
	else {
		SetLastError(ERROR_ALREADY_EXISTS);
		lua_pushboolean(L, FALSE);
	}
	free(trailing);
	return 1;
}

//-------------------------------------[ Directory.exists ]
LUA_METHOD(Directory, getexists) {
	lua_pushboolean(L, lua_self(L, 1, Directory)->exists);
	return 1;
}

//-------------------------------------[ Directory.list ]
static int Directory_iter(lua_State *L) {
	Directory *dir = lua_self(L, lua_upvalueindex(1), Directory);
	BOOL first_time = lua_isstring(L, lua_upvalueindex(2));
	HANDLE hdir = first_time ? NULL : lua_touserdata(L, lua_upvalueindex(2));
	WIN32_FIND_DATAW data;
	size_t len;
	wchar_t *buff;

	if ( first_time ) {
		if ((hdir = FindFirstFileW((wchar_t*)lua_tostring(L, lua_upvalueindex(2)), &data))== INVALID_HANDLE_VALUE)
			return 0;
		goto found;
	}
next:
	if (FindNextFileW(hdir, &data)) {
found:
		if ( data.cFileName[0] == L'.')
			goto next;
		else {
			len = wcslen(dir->fullpath)+MAX_PATH+1;
			buff = (wchar_t*)calloc(sizeof(wchar_t)*len, 1);
			_snwprintf(buff, len, L"%s\\%s", dir->fullpath, data.cFileName);
			lua_pushlwstring(L, buff, len);
			if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				lua_pushinstance(L, Directory, 1);
			else
				lua_pushinstance(L, File, 1);
			free(buff);
		}
	}
	else {
		FindClose(hdir);
		return 0;
	}
	lua_pushlightuserdata(L, hdir);
	lua_replace(L, lua_upvalueindex(2));
	return 1;
}

static int dir_create_iterator(lua_State *L, wchar_t *filter, size_t len) {
	Directory *dir = lua_self(L, 1, Directory);
	wchar_t *buff;

	len += wcslen(dir->fullpath)+2;
	buff = (wchar_t*)malloc(sizeof(wchar_t)*len);
	_snwprintf(buff, len, L"%s\\%s\0", dir->fullpath, filter);
	lua_pushvalue(L, 1);
	lua_pushlstring(L, (const char*)buff, len*sizeof(wchar_t));
	free(buff);
	lua_pushcclosure(L, Directory_iter, 2);
	return 1;
}

LUA_METHOD(Directory, list) {
	int len;
	wchar_t *filter = lua_tolwstring(L, 2, &len);

	dir_create_iterator(L, filter, len);
	free(filter);
	return 1;
}

LUA_METHOD(Directory, removeall) {
	Directory *d = lua_self(L, 1, Directory);
	size_t size = wcslen(d->fullpath)+2;
	wchar_t *path = calloc(sizeof(wchar_t), size);
	SHFILEOPSTRUCTW shFileStruct = {0};

	wcsncpy(path, d->fullpath, size-2);
    shFileStruct.wFunc= FO_DELETE;
    shFileStruct.pFrom= path;
    shFileStruct.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
	lua_pushboolean(L, SHFileOperationW(&shFileStruct) == 0);
	free(path);
    return 1;
}

LUA_METHOD(Directory, __iterate) {
	return dir_create_iterator(L, L"*.*", 3);
}

LUA_METHOD(Directory, __len) {
	Directory *dir = lua_self(L, 1, Directory);
	wchar_t *buff;
	size_t len, count = 0;
	HANDLE hdir;
	WIN32_FIND_DATAW data;

	len = wcslen(dir->fullpath)+5;
	buff = (wchar_t*)malloc(sizeof(wchar_t)*len);
	_snwprintf(buff, len, L"%s\\*.*\0", dir->fullpath);
	if ((hdir = FindFirstFileW(buff, &data)) != INVALID_HANDLE_VALUE) {
		while(FindNextFileW(hdir, &data))
			count++;
		count--;
	}
	free(buff);
	lua_pushinteger(L, count);
	return 1;
}

const luaL_Reg Directory_metafields[] = {
	{"__gc", File_gc},
	{"__iterate", Directory___iterate},
	{"__len", Directory___len},
	{NULL, NULL}
};

const luaL_Reg Directory_methods[] = {
	{"make",			Directory_make},
	{"remove",			File_remove},
	{"removeall",		Directory_removeall},
	{"move",			File_move},
	{"list",			Directory_list},
	{"get_name",		File_getfilename},
	{"get_parent",		File_getparent},
	{"get_path",		File_getpath},
	{"get_fullpath",	File_getfullpath},
	{"get_exists",		Directory_getexists},
	{"get_hidden",		File_gethidden},
	{"set_hidden",		File_sethidden},
	{"get_created",		File_getcreated},
	{"get_modified",	File_getmodified},
	{"get_accessed",	File_getaccessed},
	{"set_created",		File_setcreated},
	{"set_modified",	File_setmodified},
	{"set_accessed",	File_setaccessed},
	{NULL, NULL}
};