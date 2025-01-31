/*
 | LuaRT - A Windows programming framework for Lua
 | Luart.org, Copyright (c) Tine Samir 2022.
 | See Copyright Notice in LICENSE.TXT
 |-------------------------------------------------
 | luaRT.h | LuaRT API header
*/

#pragma once

#include "..\lua\lua.h"
#include "..\lua\lauxlib.h"
#include "..\lua\lualib.h"
#include <windows.h>

//--------------------------------------------------| LuaRT _VERSION
#define xstr(s) str(s)
#define str(s) #s
#undef LUA_VERSION_MAJOR
#undef LUA_VERSION_MINOR
#undef LUA_VERSION_RELEASE
#define LUA_VERSION_MAJOR	xstr(LUART_MAJOR)
#define LUA_VERSION_MINOR	xstr(LUART_MINOR)
#define LUA_VERSION_RELEASE	xstr(LUART_RELEASE)

#undef LUA_VERSION
#define LUA_VERSION	"LuaRT " LUA_VERSION_MAJOR "." LUA_VERSION_MINOR "." LUA_VERSION_RELEASE

//--------------------------------------------------| Object declaration macros

//--- Define object method
#define LUA_METHOD(t, n) int t##_##n(lua_State *L)

//--- Define object constructor
#define LUA_CONSTRUCTOR(t) int t##_constructor(lua_State *L)

//--- Define object get/set properties
#define LUA_PROPERTY_GET(t, n) int t##_get##n(lua_State *L)
#define LUA_PROPERTY_SET(t, n) int t##_set##n(lua_State *L)

//--------------------------------------------------| Internal Object management

//--- Internal object type id
typedef int luart_type;
#define LUART_OBJECTS "LuaRT Objects"

//--------------------------------------------------| Object registration

//--- Register object function
int lua_registerobject(lua_State *L, luart_type *type, const char *typename, lua_CFunction constructor, const luaL_Reg *methods, const luaL_Reg *mt);

//--- Object registration macros
#define lua_regobjectmt(L, typename) lua_registerobject(L, &T##typename, #typename, typename##_constructor, typename##_methods, typename##_metafields)
#define lua_regobject(L, typename) lua_registerobject(L, &T##typename, #typename, typename##_constructor, typename##_methods, NULL)

//--------------------------------------------------| Lua object manipulation

//--- Push a new instance from a Lua object at index idx on stack
int lua_createinstance(lua_State *L, int idx);

//--- Push a new Lua object instance of a 'objectname' object
void lua_checkinstance(lua_State *L, int idx, const char *objectname);

//--- Check if value at index idx is a Lua object, setting its name to objectname
int lua_isinstance(lua_State *L, int idx, const char **objectname);

//--------------------------------------------------| C object manipulation

//--- Initialize a new C object instance, to be called only in the constructor
void lua_createcinstance(lua_State *L, void *t, luart_type type);
#define lua_newinstance(L, t, _type) lua_createcinstance(L, t, T##_type)

//--- Returns any instance at index position or error with type t expected
void *lua_toself(lua_State *L, int idx, luart_type t);

//--- Returns object at specified index, and gets its type
void *lua_tocinstance(lua_State *L, int idx, luart_type *t);

//--- Returns instance of specified type at index or NULL
void *lua_iscinstance(lua_State *L, int idx, luart_type t);

//--- Returns object of specified type at index or throws an error
void *lua_checkcinstance(lua_State *L, int idx, luart_type t);
#define luaL_checkcinstance(L, idx, type) ((type *)lua_checkcinstance(L, idx, T##type))

//--- Gets object at specified index without checking its type
#define lua_self(L, idx, type) ((type*)lua_toself(L, idx, T##type))

//--- Pushes a new instance from object name with the lasts narg arguments on stack as constructor arguments
LUA_API void *lua_pushnewinstancebyname(lua_State *L, luart_type type, int narg);
LUA_API void *lua_pushnewinstance(lua_State *L, const char *typename, int narg);
#define lua_pushinstance(L, type, narg) lua_pushnewinstance(L, #type, narg)

//--- Gets value type name at the specified index (supporting objects type names)
LUA_API const char *lua_objectname(lua_State *L, int idx);

//--- Uses lua_objectname for luaL_typename implementation
#undef luaL_typename
#define luaL_typename(L, i) lua_objectname(L, i)

//--- Returns the inherited Object
LUA_API int lua_super(lua_State *L);

//--------------------------------------------------| Module registration

//--- Registers module and pushes it on the stack
LUA_API void lua_registermodule(lua_State *L, const char *name, const luaL_Reg *functions, const luaL_Reg *properties, lua_CFunction finalizer);

//--- Module registration macros with or without a finalizer (called upon module garbage collection)
#define lua_regmodule(L, m) lua_registermodule(L, #m, m##lib, m##_properties, NULL)
#define lua_regmodulefinalize(L, m) lua_registermodule(L, #m, m##lib, m##_properties, m##_finalize)

//--------------------------------------------------| Wide string functions

//--- Checks for a string at the specified index and get it as a wide string
LUA_API wchar_t *lua_tolwstring(lua_State *L, int idx, int *len);

//--- Pushes a wide string onto the stack
LUA_API void lua_pushlwstring(lua_State *L, const wchar_t *str, int len);

//--- Wide strings macros
#define lua_towstring(L, i)		lua_tolwstring(L, i, NULL)
#define lua_pushwstring(L, s)	lua_pushlwstring(L, s, -1)

//--------------------------------------------------| Various utility functions

//--- Returns index of string at specfied index, in a an array of options strings, or the default specified index
LUA_API int lua_optstring(lua_State *L, int idx, const char *options[], int def);

//--- Returns true if the specified executable has embedded content, and loads global "embed" module to access it 
LUALIB_API int luaL_embedopen(lua_State *L, const wchar_t *exename);

//--- Closes embedded content previously opened with luaL_embedopen()
LUALIB_API int luaL_embedclose(lua_State *L);

//--- luaL_setfuncs() alternative with lua_rawset() and without upvalues
LUALIB_API void luaL_setrawfuncs(lua_State *L, const luaL_Reg *l);

//--- luaL_require() alternative with luaL_requiref()
LUALIB_API void luaL_require(lua_State *L, const char *modname);
#include <commctrl.h>

//--------------------------------------------------| Widget object definition

typedef int WidgetType;

typedef struct WidgetItem {
	WidgetType	itemtype;
	LRESULT		iconstyle;
	union {
		TVITEMW				*treeitem;
		LVITEMW 			*listitem;
		TCITEMW 			*tabitem;
		COMBOBOXEXITEMW 	*cbitem;
		MENUITEMINFOW		*mi;
	};
} WidgetItem;

typedef struct Widget {
	luart_type	type;
	void		*handle;
	WidgetType	wtype;
	union {
		HANDLE		tooltip; //------ For standard Widgets, or Parent for Window:showmodal()
		HANDLE		parent;	 //------ For Menu
		int			menu;    //------ For Window
	};
	HANDLE		status;
	int			ref;
	HIMAGELIST	imglist;
	int 		index;
	WidgetItem	item;
	HFONT		font;
	HICON		icon;
	ACCEL		accel;
	HACCEL		accel_table;
	int			cursor;
	HCURSOR  	hcursor;
	BOOL 		autosize;
	UINT		events;
	HBRUSH		brush;
	COLORREF	color;
} Widget;

//--------------------------------------------------| GUI Events
typedef enum{
    onHide 			= 0,
    onShow 			= 1, 
    onMove 			= 2, 
    onResize		= 3,
    onHover 		= 4,
    onLeave			= 5,
    onClose 		= 6,
    onClick 		= 7,
    onDoubleClick 	= 8,
    onContext 		= 9,
    onCreate		= 10,
	onCaret 		= 11,
    onChange 		= 12,
    onSelect 		= 13,
    onTrayClick		= 14,
    onTrayDoubleClick=15,
    onTrayContext 	= 16,
    onTrayHover 	= 17,
	onMenu			= 18
} WidgetEvent;

#define	WM_LUAMIN			(WM_USER+2)
#define WM_LUAHIDE 			(WM_LUAMIN + onHide)
#define WM_LUASHOW 			(WM_LUAMIN + onShow)
#define WM_LUAMOVE 			(WM_LUAMIN + onMove)
#define WM_LUARESIZE 		(WM_LUAMIN + onResize)
#define WM_LUAHOVER 		(WM_LUAMIN + onHover)
#define WM_LUALEAVE 		(WM_LUAMIN + onLeave)
#define WM_LUACLOSE 		(WM_LUAMIN + onClose)
#define WM_LUACLICK 		(WM_LUAMIN + onClick)
#define WM_LUADBLCLICK		(WM_LUAMIN + onDoubleClick)
#define WM_LUACREATE	 	(WM_LUAMIN + onCreate)
#define WM_LUACONTEXT 		(WM_LUAMIN + onContext)
#define WM_LUACHANGE 		(WM_LUAMIN + onChange)
#define WM_LUASELECT 		(WM_LUAMIN + onSelect)
#define WM_LUATRAYCLICK 	(WM_LUAMIN + onTrayClick)
#define WM_LUATRAYDBLCLICK	(WM_LUAMIN + onTrayDoubleClick)
#define WM_LUATRAYCONTEXT 	(WM_LUAMIN + onTrayContext)
#define WM_LUATRAYHOVER 	(WM_LUAMIN + onTrayHover)
#define WM_LUAMENU 			(WM_LUAMIN + onMenu)
#define WM_LUAMAX 			(WM_LUAMENU+1)

//---- call event e associated with widget w
#define lua_callevent(w, e) PostMessage(w->handle, WM_LUAMIN+e, 0, 0)
//---- call event e associated with widget w with parameters p and pp
#define lua_paramevent(w, e, p, pp) PostMessage(w->handle, WM_LUAMIN+e, (WPARAM)p, (LPARAM)pp)
//---- call event e associated with widget w with an index value i
#define lua_indexevent(w, e, i) PostMessage(w->handle, WM_LUAMIN+e, (WPARAM)(i), 0)
//---- call close event a associated with window w
#define lua_closeevent(w, e) PostMessage(w->handle, WM_LUACLOSE, 0, 0)
//--- call menu event references in LUA_REGISTRYINDEX with i 
#define lua_menuevent(i, idx) PostMessage(NULL, WM_LUAMENU, (WPARAM)i, (LPARAM)idx) 

//--- Register a widget type
void lua_registerwidget(lua_State *L, int *type, char *typename, lua_CFunction constructor, const luaL_Reg *methods, const luaL_Reg *mt, BOOL has_text, BOOL has_font, BOOL has_cursor, BOOL has_icon, BOOL has_tooltip);
#define lua_regwidget(L, typename, methods, mt, has_text, has_font, has_cursor, has_icon, has_tooltip) lua_registerwidget(L, &T##typename, #typename, typename##_constructor, methods, mt, has_text, has_font, has_cursor, has_icon, has_tooltip)

//--- Widget initializators functions to be called inside widget constructor
typedef void *(*WIDGET_INIT)(lua_State *L, Widget **wp);
typedef Widget *(*WIDGET_FINALIZE)(lua_State *L, HWND h, WidgetType type, Widget *wp, SUBCLASSPROC proc);

extern WIDGET_INIT 		lua_widgetinitialize;
extern WIDGET_FINALIZE	lua_widgetfinalize;
extern luaL_Reg 		*WIDGET_METHODS;
extern luart_type 		TWidget;