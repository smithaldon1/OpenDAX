/* Modbus (tm) Communications Module for OpenDAX
 * Copyright (C) 2006 Phil Birkelbach
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __OPTIONS_H
#define __OPTIONS_H

#include <modbus.h>

/* All this silliness is because the different distributions have the libraries
 and header files for lua in different places with different names.
 There has got to be a better way. */
#if defined(HAVE_LUA5_1_LUA_H)
 #include <lua5.1/lua.h>
#elif defined(HAVE_LUA51_LUA_H)
 #include <lua51/lua.h>
#elif defined(HAVE_LUA_LUA_H)
 #include <lua/lua.h>
#elif defined(HAVE_LUA_H)
 #include <lua.h>
#else
 #error Missing lua.h
#endif

#if defined(HAVE_LUA51_LAUXLIB_H)
 #include <lua51/lauxlib.h>
#elif defined(HAVE_LUA5_1_LAUXLIB_H)
 #include <lua5.1/lauxlib.h>
#elif defined(HAVE_LUA_LAUXLIB_H)
 #include <lua/lauxlib.h>
#elif defined(HAVE_LAUXLIB_H)
 #include <lauxlib.h>
#else
 #error Missing lauxlib.h
#endif

#if defined(HAVE_LUA51_LUALIB_H)
 #include <lua51/lualib.h>
#elif defined(HAVE_LUA5_1_LUALIB_H)
 #include <lua5.1/lualib.h>
#elif defined(HAVE_LUA_LUALIB_H)
 #include <lua/lualib.h>
#elif defined(HAVE_LUALIB_H)
 #include <lualib.h>
#else
 #error Missing lualib.h
#endif 


#ifndef DEFAULT_PID
 #define DEFAULT_PID "/var/run/modbus.pid"
#endif

#define DEFAULT_DEVICE "/dev/serial"
#define DEFAULT_PORT 7777
#define DEFAULT_TABLE_SIZE 100
#define DEFAULT_TAGNAME "modbus"

/* Default number of ports to allocate if none is specified */
#ifndef MAX_PORTS
  #define MAX_PORTS 16
#endif

struct Config {
    char *pidfile;
    char *configfile;
    char *tagname;
    int verbosity;
    u_int8_t daemonize;
    unsigned int tablesize;
    int portcount;
    int maxports;
//    struct mb_port ports[MAX_PORTS]; /* Pointer to an array of ports */
    struct mb_port *ports; /* Pointer to an array of ports */
};


int modbus_configure(int, const char **);
int getbaudrate(int);

#endif
