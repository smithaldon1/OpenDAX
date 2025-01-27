/*  OpenDAX - An open source data acquisition and control system
 *  Copyright (c) 2021 Phil Birkelbach
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
 *  Tests the basic function of the tag retention functions
 */

#include <common.h>
#include <opendax.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "libtest_common.h"


int
test_one(int argc, char *argv[])
{
    dax_state *ds;
    int result = 0;
    tag_handle h;
    dax_dint temp;

    ds = dax_init("test");
    dax_init_config(ds, "test");

    dax_configure(ds, argc, argv, CFG_CMDLINE);
    result = dax_connect(ds);
    if(result) {
        return -1;
    }
    result = 0;
    result += dax_tag_add(ds, &h, "TEST1", DAX_DINT, 1, TAG_ATTR_RETAIN);
    result += dax_tag_add(ds, &h, "TEST2", DAX_DINT, 1, TAG_ATTR_RETAIN);
    temp = 0xAABBCCDD;
    result = dax_write_tag(ds, h, &temp);

    return result;
}

int
test_two(int argc, char *argv[])
{
    dax_state *ds;
    int result = 0;
    tag_handle h;
    dax_dint temp;

    ds = dax_init("test");
    dax_init_config(ds, "test");

    dax_configure(ds, argc, argv, CFG_CMDLINE);
    result = dax_connect(ds);
    if(result) {
        return -1;
    }
    result = 0;
    result += dax_tag_add(ds, &h, "TEST1", DAX_DINT, 1, TAG_ATTR_RETAIN);
    result += dax_tag_add(ds, &h, "TEST2", DAX_DINT, 1, TAG_ATTR_RETAIN);
    result = dax_read_tag(ds, h, &temp);
    if(temp != 0xAABBCCDD) {
        printf("ERROR: Tag value does not match\n");
        return -1;
    }

    return result;
}


int
main(int argc, char *argv[])
{
    if(run_test(test_one, argc, argv, NO_UNLINK_RETAIN)) {
        exit(-1);
    } else {
        if(run_test(test_two, argc, argv, 0)) {
            exit(-1);
        } else {
            exit(0);
        }
    }
}
