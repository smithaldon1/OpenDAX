/*  OpenDAX - An open source data acquisition and control system
 *  Copyright (c) 1997 Phil Birkelbach
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

 * This is the source file for the event notification handling routines
 */

#include <common.h>
#include "tagbase.h"
#include "func.h"
#include <ctype.h>
#include <assert.h>

pthread_mutex_t _event_lock;

extern _dax_tag_db *_db;

/* Private function definitions */

static int
_send_event(tag_index idx, _dax_event *event)
{
    int result;
    char buff[DAX_MSGMAX];
    uint32_t msgsize;

    if(event->options & EVENT_OPT_SEND_DATA) {
        msgsize = event->size + 16; /* Calculate the total size of this message */
        *(uint32_t *)(&buff[0])  = htonl(event->size + 8); /* The size that we send */
    } else {
        msgsize = 16; /* Calculate the total size of this message */
        *(uint32_t *)(&buff[0])  = htonl(8); /* The size that we send */
    }
    if(msgsize > DAX_MSGMAX) return ERR_2BIG;
    *(uint32_t *)(&buff[4])  = htonl(MSG_EVENT | event->eventtype);
    *(uint32_t *)(&buff[8])  = htonl(idx);
    *(uint32_t *)(&buff[12])  = htonl(event->id);
    if(event->options & EVENT_OPT_SEND_DATA) {
        memcpy(&buff[16], &_db[idx].data[event->byte], event->size);
    }
    xlog(LOG_MSG, "Sending %d event to module %d",
         event->eventtype, event->notify->fd);
    result = xwrite(event->notify->fd, buff, msgsize);
    if(result < 0) {
        xerror("_send_event: %s", strerror(errno));
        return ERR_MSG_SEND;
    }
    return 0;
}

static inline int
_event_change(_dax_event *event, tag_index idx, int offset, int size) {
    int bit, n, i, len, result;
    uint8_t *this, *that;
    uint8_t mask;
    result = 0;


    if(event->datatype == DAX_BOOL) {
        /* TODO: Only check the bits that have changed.  For now we are just
         * looping through each bit in the event to see if anything has
         * changed.  This can be made much more efficient. */
        this = (uint8_t *)event->test;
        that = (uint8_t *)&(_db[idx].data[event->byte]);
        bit = event->bit;
        i = 0;
        for(n = 0; n < event->count; n++) {
            mask = (0x01 << bit);
            if((this[i] & mask) != (that[i] & mask)) {
                this[i] = that[i];
                result = 1;
            }
            bit++;
            if(bit == 8) {
                bit = 0;
                i++;
            }
        }

    } else {
        this = (uint8_t *)event->test + MAX(0, offset - event->byte);
        that = (uint8_t *)&(_db[idx].data[MAX(offset, event->byte)]);
        len = MIN(event->byte + event->size, offset + size) - MAX(offset, event->byte);

        for(n = 0; n < len; n++) {
            if(this[n] != that[n]) {
                result = 1;
                this[n] = that[n];
            }
        }
    }
    return result;
}

/* Checks to see if the bit is set and whether or not the event has been sent */
static inline int
_event_set(_dax_event *event, tag_index idx, int offset, int size) {
    int bit, n, i, result;
    uint8_t *this, *that;
    uint8_t mask;
    result = 0;

    /* TODO: Only check the bits that have changed.  For now we are just
     * looping through each bit in the event to see if anything has
     * changed.  This can be made much more efficient. */
    this = (uint8_t *)event->test;
    that = (uint8_t *)&(_db[idx].data[event->byte]);
    bit = event->bit;
    i = 0;
    for(n = 0; n < event->count; n++) {
        mask = (0x01 << bit);
        if(that[i] & mask) {
            if(!(this[i] & mask)) {
                result = 1;
                this[i] |= mask; /* Set flag so we don't do this next time */
            }
        } else {
            this[i] &= ~mask; /* Unset flag so event will fire next time */
        }
        bit++;
        if(bit == 8) {
            bit = 0;
            i++;
        }
    }
    return result;
}

static inline int
_event_reset(_dax_event *event, tag_index idx, int offset, int size) {
    int bit, n, i, result;
    uint8_t *this, *that;
    uint8_t mask;
    result = 0;

    /* TODO: Only check the bits that have changed.  For now we are just
     * looping through each bit in the event to see if anything has
     * changed.  This can be made much more efficient. */
    this = (uint8_t *)event->test;
    that = (uint8_t *)&(_db[idx].data[event->byte]);
    bit = event->bit;
    i = 0;
    for(n = 0; n < event->count; n++) {
        mask = (0x01 << bit);
        if(!(that[i] & mask)) {
            if(!(this[i] & mask)) {
                result = 1;
                this[i] |= mask; /* Set flag so we don't do this next time */
            }
        } else {
            this[i] &= ~mask; /* Unset flag so event will fire next time */
        }
        bit++;
        if(bit == 8) {
            bit = 0;
            i++;
        }
    }
    return result;
}

static inline int
_generic_compare(tag_type datatype, void *data1, void *data2) {
    dax_type_union u1, u2;

    switch(datatype) {
        case DAX_BYTE:
            u1.dax_byte = *(dax_byte *)data1;
            u2.dax_byte = *(dax_byte *)data2;
            if(u1.dax_byte < u2.dax_byte) return -1;
            else if(u1.dax_byte > u2.dax_byte) return 1;
            else return 0;
        case DAX_SINT:
        case DAX_CHAR:
            u1.dax_sint = *(dax_sint *)data1;
            u2.dax_sint = *(dax_sint *)data2;
            if(u1.dax_sint < u2.dax_sint) return -1;
            else if(u1.dax_sint > u2.dax_sint) return 1;
            else return 0;
        case DAX_UINT:
        case DAX_WORD:
            u1.dax_uint = *(dax_uint *)data1;
            u2.dax_uint = *(dax_uint *)data2;
            if(u1.dax_uint < u2.dax_uint) return -1;
            else if(u1.dax_uint > u2.dax_uint) return 1;
            else return 0;
        case DAX_INT:
            u1.dax_int = *(dax_int *)data1;
            u2.dax_int = *(dax_int *)data2;
            if(u1.dax_int < u2.dax_int) return -1;
            else if(u1.dax_int > u2.dax_int) return 1;
            else return 0;
        case DAX_UDINT:
        case DAX_DWORD:
        case DAX_TIME:
            u1.dax_udint = *(dax_udint *)data1;
            u2.dax_udint = *(dax_udint *)data2;
            if(u1.dax_udint < u2.dax_udint) return -1;
            else if(u1.dax_udint > u2.dax_udint) return 1;
            else return 0;
        case DAX_DINT:
            u1.dax_dint = *(dax_dint *)data1;
            u2.dax_dint = *(dax_dint *)data2;
            if(u1.dax_dint < u2.dax_dint) return -1;
            else if(u1.dax_dint > u2.dax_dint) return 1;
            else return 0;
        case DAX_ULINT:
        case DAX_LWORD:
            u1.dax_ulint = *(dax_ulint *)data1;
            u2.dax_ulint = *(dax_ulint *)data2;
            if(u1.dax_ulint < u2.dax_ulint) return -1;
            else if(u1.dax_ulint > u2.dax_ulint) return 1;
            else return 0;
        case DAX_LINT:
            u1.dax_lint = *(dax_lint *)data1;
            u2.dax_lint = *(dax_lint *)data2;
            if(u1.dax_lint < u2.dax_lint) return -1;
            else if(u1.dax_lint > u2.dax_lint) return 1;
            else return 0;
        case DAX_REAL:
            u1.dax_real = *(dax_real *)data1;
            u2.dax_real = *(dax_real *)data2;
            if(u1.dax_real < u2.dax_real) return -1;
            else if(u1.dax_real > u2.dax_real) return 1;
            else return 0;
        case DAX_LREAL:
            u1.dax_lreal = *(dax_lreal *)data1;
            u2.dax_lreal = *(dax_lreal *)data2;
            if(u1.dax_lreal < u2.dax_lreal) return -1;
            else if(u1.dax_lreal > u2.dax_lreal) return 1;
            else return 0;
    }
    assert(0); /* Something is seriously wrong if we get here */
    return -2;
}

static int
_event_compare(_dax_event *event, tag_index idx, int offset, int size, int compare) {
    int n, inc, bit, len, result;
    uint8_t *this, *that;
    uint8_t mask;
    result = 0;

    inc = TYPESIZE(event->datatype) / 8;
    /* For these 'this' is a bit field */
    bit = MAX(0, (offset - event->byte) / inc);
    this = (uint8_t *)event->test;
    that = (uint8_t *)&(_db[idx].data[MAX(offset, event->byte)]);
    len = MIN(event->byte + event->size, offset + size) - MAX(offset, event->byte);
    for(n = 0; n < len; n += inc) {
        mask = 0x01<<(bit%8);
        if(_generic_compare(event->datatype, event->data, &(that[n])) == compare) {
            if(!(this[bit/8] & mask)) {
                this[bit/8] |= mask;
                result = 1;
            }
        } else {
            this[bit/8] &= ~mask; /* Unset flag so event will fire next time */
        }
        bit++;
    }
    return result;
}

/* This function calculates whether the deadband has been exceeded for each of the
 * different datatypes.  Returns 1 if the deadband has been exceeded and 0 otherwise */
static inline int
_generic_deadband(_dax_event *event, void *data1, void *data2) {
    dax_type_union diff, db;

    switch(event->datatype) {
        case DAX_BYTE:
            db.dax_byte = *(dax_byte *)event->data;
            diff.dax_int = *(dax_byte *)data1 - *(dax_byte *)data2;
            if(ABS(diff.dax_int) >= db.dax_byte) return 1;
            else return 0;
        case DAX_SINT:
        case DAX_CHAR:
            db.dax_sint = *(dax_sint *)event->data;
            diff.dax_sint = *(dax_sint *)data1 - *(dax_sint *)data2;
            if(ABS(diff.dax_sint) >= db.dax_sint) return 1;
            else return 0;
        case DAX_UINT:
        case DAX_WORD:
            db.dax_uint = *(dax_uint *)event->data;
            diff.dax_dint = *(dax_uint *)data1 - *(dax_uint *)data2;
            if(ABS(diff.dax_dint) >= db.dax_uint) return 1;
            else return 0;
        case DAX_INT:
            db.dax_int = *(dax_int *)event->data;
            diff.dax_int = *(dax_int *)data1 - *(dax_int *)data2;
            if(ABS(diff.dax_int) >= db.dax_int) return 1;
            else return 0;
        case DAX_UDINT:
        case DAX_DWORD:
        case DAX_TIME:
            db.dax_udint = *(dax_udint *)event->data;
            diff.dax_lint = *(dax_udint *)data1 - *(dax_udint *)data2;
            if(ABS(diff.dax_lint) >= db.dax_udint) return 1;
            else return 0;
        case DAX_DINT:
            db.dax_dint = *(dax_dint *)event->data;
            diff.dax_dint = *(dax_dint *)data1 - *(dax_dint *)data2;
            if(ABS(diff.dax_dint) >= db.dax_dint) return 1;
            else return 0;
        case DAX_ULINT:
        case DAX_LWORD:
            db.dax_ulint = *(dax_ulint *)event->data;
            diff.dax_lint = *(dax_ulint *)data1 - *(dax_ulint *)data2;
            if(ABS(diff.dax_lint) >= db.dax_ulint) return 1;
            else return 0;
        case DAX_LINT:
            db.dax_lint = *(dax_lint *)event->data;
            diff.dax_lint = *(dax_lint *)data1 - *(dax_lint *)data2;
            if(ABS(diff.dax_lint) >= db.dax_lint) return 1;
            else return 0;
        case DAX_REAL:
            db.dax_real = *(dax_real *)event->data;
            diff.dax_real = *(dax_real *)data1 - *(dax_real *)data2;
            if(ABS(diff.dax_real) >= db.dax_real) return 1;
            else return 0;
        case DAX_LREAL:
            db.dax_lreal = *(dax_lreal *)event->data;
            diff.dax_lreal = *(dax_lreal *)data1 - *(dax_lreal *)data2;
            if(ABS(diff.dax_lreal) >= db.dax_lreal) return 1;
            else return 0;
    }
    assert(0); /* Something is seriously wrong if we get here */
    return -2;
}


static inline int
_event_deadband(_dax_event *event, tag_index idx, int offset, int size) {
    int n, inc, len, result;
    uint8_t *this, *that;
    result = 0;

    inc = TYPESIZE(event->datatype) / 8;
    this = (uint8_t *)event->test + MAX(0, offset - event->byte);
    that = (uint8_t *)&(_db[idx].data[MAX(offset, event->byte)]);
    len = MIN(event->byte + event->size, offset + size) - MAX(offset, event->byte);

    for(n = 0; n < len; n += inc) {
        if(_generic_deadband(event, &this[n], &that[n])) {
            result = 1;
            memcpy(&this[n], &that[n], inc);
        }
    }
    return result;
}



/* This function is called when the area of data is affected by the write
 * and it determines whether the event should fire or not.  Return 1 if the
 * event hits and 0 otherwise.  There are no errors */
static int
_event_hit(_dax_event *event, tag_index idx, int offset, int size) {
    switch(event->eventtype) {
        case EVENT_WRITE:
            return 1;
        case EVENT_CHANGE:
            return _event_change(event, idx, offset, size);
        case EVENT_SET:
            return _event_set(event, idx, offset, size);
        case EVENT_RESET:
            return _event_reset(event, idx, offset, size);
        case EVENT_EQUAL:
            return _event_compare(event, idx, offset, size, 0);
        case EVENT_GREATER:
            return _event_compare(event, idx, offset, size, -1);
        case EVENT_LESS:
            return _event_compare(event, idx, offset, size, 1);
        case EVENT_DEADBAND:
            return _event_deadband(event, idx, offset, size);
    }
    return 0;
}

/* This function checks to see if an event has occurred.  It should be
 * called from the tag_write() function or the tag_mask_write() function.
 * If it decides that there is an event match to the data area given then
 * it will call the send_event() function to send the event message to
 * the proper module. This function assumes that the events that are stored
 * with events that make sense so it does no checking.  There is no return type
 * because there are no possible errors, and no information to pass back. */
/* TODO: The event list should be sorted in increasing order of the bottom of
 * the range so that this function would not have to run through all of the
 * events.  For now we keep it simple. */
void
event_check(tag_index idx, int offset, int size) {
    _dax_event *this;

    this = _db[idx].events;

    while(this != NULL) {
        /* This is to check whether the the data rages intersect.  If this
         * test passes then we have manipulated the data associated with
         * this event. */
        if(offset <= (this->byte + this->size - 1) && (offset + size -1 ) >= this->byte) {
            if(_event_hit(this, idx, offset, size)) {
                _send_event(idx, this);
            }
        }
        this = this->next;
    }
    return;
}

/* This function checks to see if the tag has a deleted event.  This
 * should only be called from the tag_delete() function */
void
event_del_check(tag_index idx) {
    _dax_event *this;

    this = _db[idx].events;

    while(this != NULL) {
        /* Look for the tag delete event. */
        if(this->eventtype == EVENT_DELETED) {
            _send_event(idx, this);
        }
        this = this->next;
    }
    return;
}

static inline int
_verify_event_type(tag_type ttype, int etype)
{
    /* All datatypes can use Write or Change */
    if(etype == EVENT_WRITE || etype == EVENT_CHANGE) {
        return 0;
    }
    /* Only BOOL can use Set and Reset */
    if(etype == EVENT_SET || etype == EVENT_RESET) {
        if(ttype == DAX_BOOL) {
            return 0;
        } else {
            xlog(LOG_ERROR, "Only BOOL tags are allowed to use SET or RESET events");
            return -1;
        }
    }
    /* Booleans, Reals and Custom Datatypes Can't use Equal */
    if(etype == EVENT_EQUAL) {
        if(ttype == DAX_REAL || ttype == DAX_LREAL ||
           ttype == DAX_BOOL || ttype >= DAX_CUSTOM) {
            xlog(LOG_ERROR, "EQUAL event not allowed for BOOL, REAL, LREAL or Custom types");
            return -1;
        } else {
            return 0;
        }
    }
    /* At this point the only ones left are < > and deadband.  All
     * except Booleans and Custom datatypes can use these */
    if(ttype == DAX_BOOL || ttype >= DAX_CUSTOM) {
        xlog(LOG_ERROR, "GREATER, LESS and DEADBAND events not allowed for BOOL and Custom types");
        return -1;
    } else {
        return 0;
    }
    /* If we get here then we were given an unknown event type */
    xlog(LOG_ERROR, "Unknown datatype in new event");
    return -1;
}

/* This function figures out how big the *data and *test memory blocks need
 * to be in the event and assigns the proper data to them.  The *data area
 * is to store the data from the user that is associated with the event, such
 * as the number that the variables are supposed to be less than.  The *test
 * area is to store state data such as a bitmap to show the last values of
 * the tag to determine if a set event should be issued. It is assumed that
 * the events make sense at this point. Returns 0 on success and an error
 * code otherwise. */
int
_set_event_data(_dax_event *event, tag_index index, void *data) {
    int datasize = 0, testsize = 0, size;

    /* Figure out how much memory to allocate */
    switch(event->eventtype) {
        case EVENT_WRITE: /* We don't need any of this to detect write events */
            datasize = 0;
            testsize = 0;
            break;
        case EVENT_CHANGE:
            datasize = 0;
            if(event->datatype == DAX_BOOL) {
                testsize = (event->count - 1)/8 + 1;
            } else {
                testsize = type_size(event->datatype) * event->count;
            }
            break;
        case EVENT_DEADBAND:
            size = type_size(event->datatype);
            datasize = size;
            testsize = size * event->count;
            break;
        case EVENT_SET:
        case EVENT_RESET:
            datasize = 0;
            testsize = (event->count - 1)/8 + 1;
            break;
        case EVENT_EQUAL:
        case EVENT_GREATER:
        case EVENT_LESS:
            datasize = type_size(event->datatype);
            testsize = (event->count - 1)/8 + 1;
            break;
    }
    /* Allocate the memory that we need */
    if(datasize > 0) {
        event->data = malloc(datasize);
        if(event->data == NULL) {
            xerror("event_add() - Unable to allocate memory for event data");
            return ERR_ALLOC;
        }
    } else {
        event->data = NULL;
    }

    if(testsize > 0) {
        event->test = malloc(testsize);
        if(event->test == NULL) {
            if(event->data != NULL) free(event->data);
            xerror("event_add() - Unable to allocate memory for event test data");
            return ERR_ALLOC;
        }
    } else {
        event->test = NULL;
    }

    switch(event->eventtype) {
        case EVENT_WRITE:
           break;
        case EVENT_DEADBAND:
            /* Copy the existing tag data into the *data area */
            memcpy(event->data, data, datasize);
            memcpy(event->test, &_db[index].data[event->byte], testsize);
            break;
        case EVENT_CHANGE:
            memcpy(event->test, &_db[index].data[event->byte], testsize);
            break;
        case EVENT_SET:
        case EVENT_RESET:
            bzero(event->test, testsize);
            break;
        case EVENT_EQUAL:
        case EVENT_GREATER:
        case EVENT_LESS:
            memcpy(event->data, data, datasize);
            bzero(event->test, testsize);
            break;
    }


    return 0;
}

/* Add the event defined.  Return the event id. 'h' is a handle to the tag
 * data that the event is tied too.  'event_type' is the type of event (see
 * opendax.h for #defines.  'data' is any data that may need to be
 * passed such as values for deadband.*/
int
event_add(tag_handle h, int event_type, void *data, dax_module *module)
{
    _dax_event *head, *new;
    int result;

    /* Bounds check handle */
    if(h.index < 0 || h.index >= get_tagindex()) {
        xlog(LOG_ERROR, "Tag index %d for new event is out of bounds", h.index);
        return ERR_ARG;
    }
    /* No events can be assigned to virtual tags unless it's a queue */
    if(is_tag_virtual(h.index) && ! is_tag_queue(h.index)) {
        return ERR_ILLEGAL;
    }
    /* If the tag is a queue then we can only do write events for
     * the entire tag not a partial handle. */
    if(is_tag_queue(h.index)) {
        if(h.byte != 0 || event_type != EVENT_WRITE ||
           h.size != (tag_get_size(_db[h.index].type) * _db[h.index].count)) {
            return ERR_ILLEGAL;
        }
    }
    /* Bounds check size */
    if( (h.byte + h.size) > tag_get_size(h.index)) {
        xlog(LOG_ERROR, "Size of the affected data in the new event is too large");
        return ERR_2BIG;
    }
    if(_verify_event_type(h.type, event_type)) {
        /* error log handled in verify_event_type() function */
        return ERR_ARG;
    }
    /* If everything is okay then allocate the new event. */
    new = xmalloc(sizeof(_dax_event));
    if(new == NULL) {
        xerror("event_add() - Unable to allocate memory for new event");
        return ERR_ALLOC;
    }
    new->id = _db[h.index].nextevent++;
    new->byte = h.byte;
    new->bit = h.bit;
    new->size = h.size;
    new->count = h.count;
    new->options = 0x0000;
    new->datatype = h.type;
    new->eventtype = event_type;
    new->notify = module;
    result = _set_event_data(new, h.index, data);
    if(result) {
        free(new);
        return result;
    }

    head = _db[h.index].events;
    /* If the list is empty put it on top */
    if(head == NULL) {
        new->next = NULL;
        _db[h.index].events = new;
    } else {
        /* For now we are not going to sort these events.  The right optimization
         * would be to sort by the bottom of the range.  This way the
         * _event_check() function could stop once the top of the updated
         * range is greater than the bottom of the range of the event. */
        new->next = _db[h.index].events;
        _db[h.index].events = new;
    }
    module->event_count++; /* Increment the Module's event reference counter */
    return new->id;
}

/* Frees the memory associated with an event.  Pass a NULL pointer
 * and bad things will happen. */
static void
_free_event(_dax_event *event) {
    if(event->data != NULL) free(event->data);
    if(event->test != NULL) free(event->test);
    free(event);
}

int
_find_event(_dax_event **event, int index, int id) {
	_dax_event *this;

    this = _db[index].events;
    if(this == NULL) return ERR_NOTFOUND;
    while(this->id != id) {
    	if(this->next == NULL) return ERR_NOTFOUND;
    	this = this->next;
    }
    *event = this;
	return 0;
}

int
event_del(int index, int id, dax_module *module)
{
    _dax_event *this, *last;
    int result = ERR_NOTFOUND;

    if(index >= get_tagindex() || index < 0) {
        xerror("event_del() - index %d is out of range\n", index);
    }
    this = _db[index].events;
    if(this == NULL) return ERR_NOTFOUND;
    if(this->id == id) {
        if(this->notify != module) {
            xlog(LOG_ERROR | LOG_VERBOSE, "Module cannot delete another module's event");
            return ERR_AUTH;
        }
        _db[index].events = this->next;
        _free_event(this);
    }
    last = this;
    this = this->next;
    while(this != NULL) {
        if(this->id == id) {
            if(this->notify != module) {
                xlog(LOG_ERROR | LOG_VERBOSE, "Module cannot delete another module's event");
                return ERR_AUTH;
            }
            last->next = this->next;
            _free_event(this);
            result = 0;
            break; /* Uggg */
        }
        last = this;
        this = this->next;
    }
    module->event_count--;
    return result;
}

/* Traverse the linked list of events and delete them all */
int
events_del_all(_dax_event *head) {
    _dax_event *this, *next;
    this = head;
    while(this != NULL) {
        next = this->next;
        _free_event(this);
        this = next;
    }
    return 0;
}

int
event_opt(int index, int id, uint32_t options, dax_module *module) {
    _dax_event *event;
    int result;

    if(index >= get_tagindex() || index < 0) {
        xerror("event_opt() - index %d is out of range\n", index);
    }

    result = _find_event(&event, index, id);
    if(result) xerror("event_opt() - event not found\n");
    if(event->notify != module) {
    	xlog(LOG_ERROR | LOG_VERBOSE, "Module cannot modify another module's event");
        return ERR_AUTH;
    }

    event->options = options;
    return 0;
}


/* Removes all of the events that are owned by the given module */
int
events_cleanup(dax_module *module) {
    int n, count;
    _dax_event *this;

    count = get_tagindex();
    /* We start our scan at the bottom and work our way up.  It's probably
     * more likely that our modules events are associated with tags at the
     * bottom of the list.  This should prove more efficient */
    for(n = count-1; n >= 0 && module->event_count > 0; n--) {
        if(_db[n].events != NULL) {
            this = _db[n].events;
            while(this != NULL) {
                if(this->notify == module) {
                    event_del(n, this->id, module);
                }
                this = this->next;
            }
        }
    }
    return 0;
}
