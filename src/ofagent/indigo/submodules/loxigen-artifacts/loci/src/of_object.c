/* Copyright (c) 2008 The Board of Trustees of The Leland Stanford Junior University */
/* Copyright (c) 2011, 2012 Open Networking Foundation */
/* Copyright (c) 2012, 2013 Big Switch Networks, Inc. */
/* See the file LICENSE.loci which should have been included in the source distribution */

/****************************************************************
 *
 * of_object.c
 *
 * These are the low level object constructor/destructor operators.
 *
 ****************************************************************/

#include "loci_log.h"
#include <loci/loci.h>
#include <loci/loci_validator.h>

/**
 * Create a generic new object and possibly underlying wire buffer
 * @param bytes The number of bytes to allocate in the underlying buffer
 *
 * If bytes <= 0, do not allocate a wire buffer.
 *
 * Note that this is an internal function.  The class specific
 * new functions should be called to properly initialize and track an
 * OF object.
 */

of_object_t *
of_object_new(int bytes)
{
    of_object_t *obj;

    if ((obj = (of_object_t *)MALLOC(sizeof(*obj))) == NULL) {
        return NULL;
    }
    MEMSET(obj, 0, sizeof(*obj));

    if (bytes > 0) {
        if ((obj->wire_object.wbuf = of_wire_buffer_new(bytes)) == NULL) {
            FREE(obj);
            return NULL;
        }
        obj->wire_object.owned = 1;
    }

    return obj;
}

/**
 * The delete function for LOCI objects
 *
 * @param obj Pointer to the object to be deleted
 *
 * This can be called on any LOCI object; it should not need to be
 * overridden.
 */

void
of_object_delete(of_object_t *obj)
{
    if (obj == NULL) {
        return;
    }

    /*
     * Make callback if present
     */
    if (obj->track_info.delete_cb != NULL) {
        obj->track_info.delete_cb(obj);
    }

    if (obj->wire_object.owned) {
        of_wire_buffer_free(obj->wire_object.wbuf);
    }

    FREE(obj);
}

/**
 * Duplicate an object
 * @param src The object to be duplicated
 * @returns Pointer to the duplicate or NULL on error.  Caller is responsible
 * for freeing the returned object.
 */

of_object_t *
of_object_dup(of_object_t *src)
{
    of_object_t *dst;
    of_object_init_f init_fn;

    if ((dst = (of_object_t *)MALLOC(sizeof(*dst))) == NULL) {
        return NULL;
    }

    MEMSET(dst, 0, sizeof(*dst));

    /* Allocate a minimal wire buffer assuming we will not write to it. */
    if ((dst->wire_object.wbuf = of_wire_buffer_new(src->length)) == NULL) {
        FREE(dst);
        return NULL;
    }

    dst->wire_object.owned = 1;

    init_fn = of_object_init_map[src->object_id];
    init_fn(dst, src->version, src->length, 0);

    MEMCPY(OF_OBJECT_BUFFER_INDEX(dst, 0),
           OF_OBJECT_BUFFER_INDEX(src, 0),
           src->length);

    return dst;
}

/**
 * Generic new from message call
 */

of_object_t *
of_object_new_from_message(of_message_t msg, int len)
{
    of_object_id_t object_id;
    of_object_t *obj;
    of_version_t version;

    version = of_message_version_get(msg);
    if (!OF_VERSION_OKAY(version)) {
        return NULL;
    }

    if (of_validate_message(msg, len) != 0) {
        LOCI_LOG_ERROR("message validation failed\n");
        return NULL;
    }

    if ((obj = of_object_new(-1)) == NULL) {
        return NULL;
    }

    if (of_object_buffer_bind(obj, OF_MESSAGE_TO_BUFFER(msg), len, 
                              OF_MESSAGE_FREE_FUNCTION) < 0) {
        FREE(obj);
        return NULL;
    }
    obj->version = version;

    of_header_wire_object_id_get(obj, &object_id);
    of_object_init_map[object_id](obj, version, len, 0);

    return obj;
}

/**
 * Parse a message without allocating memory
 *
 * @param storage Pointer to an uninitialized of_object_storage_t
 * @param buf Pointer to the buffer
 * @param length Length of buf
 * @returns Pointer to an initialized of_object_t
 *
 * The lifetime of the returned object is the minimum of the lifetimes of
 * 'buf' and 'storage'.
 */

of_object_t *
of_object_new_from_message_preallocated(of_object_storage_t *storage,
                                        uint8_t *buf, int len)
{
    of_object_t *obj = &storage->obj;
    of_wire_buffer_t *wbuf = &storage->wbuf;
    of_message_t msg = buf;
    of_version_t version;
    of_object_id_t object_id;

    memset(storage, 0, sizeof(*storage));

    version = of_message_version_get(msg);
    if (!OF_VERSION_OKAY(version)) {
        return NULL;
    }

    if (of_validate_message(msg, len) != 0) {
        LOCI_LOG_ERROR("message validation failed\n");
        return NULL;
    }

    obj->version = version;
    obj->wire_object.wbuf = wbuf;
    wbuf->buf = msg;
    wbuf->alloc_bytes = len;
    wbuf->current_bytes = len;

    of_header_wire_object_id_get(obj, &object_id);
    of_object_init_map[object_id](obj, version, len, 0);

    return obj;
}

/**
 * Bind an existing buffer to an LOCI object
 *
 * @param obj Pointer to the object to be updated
 * @param buf Pointer to the buffer to bind to obj
 * @param bytes Length of buf
 * @param buf_free An optional free function to be applied to
 * buf on deallocation
 *
 * This can be called on any LOCI object; it should not need to be
 * overridden.
 */

int
of_object_buffer_bind(of_object_t *obj, uint8_t *buf, int bytes, 
                      of_buffer_free_f buf_free)
{
    of_wire_object_t *wobj;
    of_wire_buffer_t *wbuf;

    LOCI_ASSERT(buf != NULL);
    LOCI_ASSERT(bytes > 0);
    // LOCI_ASSERT(wobj is not bound);

    wobj = &obj->wire_object;
    MEMSET(wobj, 0, sizeof(*wobj));

    wbuf = of_wire_buffer_new_bind(buf, bytes, buf_free);
    if (wbuf == NULL) {
        return OF_ERROR_RESOURCE;
    }

    wobj->wbuf = wbuf;
    wobj->owned = 1;
    obj->length = bytes;

    return OF_ERROR_NONE;
}

/**
 * Connect a child to a parent at the wire buffer level
 *
 * @param parent The top level object to bind to
 * @param child The sub-object connecting to the parent
 * @param offset The offset at which to attach the child RELATIVE 
 * TO THE PARENT in the buffer
 * @param bytes The amount of the buffer dedicated to the child; see below
 * @param inc_ref_count Should the ref count of the parent be incremented
 * 
 * This is used for 'get' accessors for composite types as well as
 * iterator functions for lists, both read (first/next) and write
 * (append_init, append_advance).
 *
 * Connect a child object to a parent by setting up the child's
 * wire_object to point to the parent's underlying buffer.  The value
 * of the parameter bytes is important in determining how the child
 * is initialized:
 * @li If bytes <= 0, the length and type of the child are not modified;
 * no additional space is added to the buffer.
 * @li If bytes > 0, the current wire buffer is grown to 
 * accomodate this many bytes.  This is to support append operations.
 *
 * If an error is returned, future references to the child object
 * (until it is reinitialized) are undefined.
 */
static void
object_child_attach(of_object_t *parent, of_object_t *child, 
                       int offset, int bytes)
{
    of_wire_object_t *c_wobj; /* Pointer to child's wire object */
    of_wire_buffer_t *wbuf; /* Pointer to common wire buffer manager */

    child->parent = parent;
    wbuf = parent->wire_object.wbuf;

    /* Set up the child's wire buf to point to same as parent */
    c_wobj = &child->wire_object;
    c_wobj->wbuf = wbuf;
    c_wobj->obj_offset = parent->wire_object.obj_offset + offset;
    c_wobj->owned = 0;

    /*
     * bytes determines if this is a read or write setup.
     * If > 0, grow the buffer to accomodate the space
     * Otherwise do nothing
     */
    if (bytes > 0) { /* Set internal length, request buffer space */
        int tot_bytes; /* Total bytes to request for buffer if updated */

        /* Set up space for the child in the parent's buffer */
        tot_bytes = parent->wire_object.obj_offset + offset + bytes;

        of_wire_buffer_grow(wbuf, tot_bytes);
        child->length = bytes;
    }
    /* if bytes == 0 don't do anything */
}

/**
 * Check for room in an object's wire buffer.
 * @param obj The object being checked
 * @param new_len The desired length
 * @return Boolean
 */

int
of_object_can_grow(of_object_t *obj, int new_len)
{
    return OF_OBJECT_ABSOLUTE_OFFSET(obj, new_len) <=
        WBUF_ALLOC_BYTES(obj->wire_object.wbuf);
}

/**
 * Set the xid of a message object
 * @param obj The object being accessed
 * @param xid The xid value to store in the wire buffer
 * @return OF_ERROR_
 * Since the XID is common across all versions, this is used
 * for all XID accessors.
 */

int
of_object_xid_set(of_object_t *obj, uint32_t xid)
{
    of_wire_buffer_t *wbuf;

    if ((wbuf = OF_OBJECT_TO_WBUF(obj)) == NULL) {
        return OF_ERROR_PARAM;
    }
    of_wire_buffer_u32_set(wbuf, 
        OF_OBJECT_ABSOLUTE_OFFSET(obj, OF_MESSAGE_XID_OFFSET), xid);
    return OF_ERROR_NONE;
}

/**
 * Get the xid of a message object
 * @param obj The object being accessed
 * @param xid Pointer to where to store the xid value
 * @return OF_ERROR_
 * Since the XID is common across all versions, this is used
 * for all XID accessors.
 */

int
of_object_xid_get(of_object_t *obj, uint32_t *xid)
{
    of_wire_buffer_t *wbuf;

    if ((wbuf = OF_OBJECT_TO_WBUF(obj)) == NULL) {
        return OF_ERROR_PARAM;
    }
    of_wire_buffer_u32_get(wbuf, 
        OF_OBJECT_ABSOLUTE_OFFSET(obj, OF_MESSAGE_XID_OFFSET), xid);
    return OF_ERROR_NONE;
}

/****************************************************************
 *
 * Generic list operation implementations
 *
 ****************************************************************/

/**
 * Set up a child for appending to a parent list
 * @param parent The parent; must be a list object
 * @param child The child object; must be of type list element
 * @return OF_ERROR_
 *
 * Attaches the wire buffer of the parent to the child by pointing
 * the child to the end of the parent.
 * 
 * Set the wire length and type from the child.
 * Update the parent length adding the current child length
 *
 * After calling this function, the child object may be updated
 * resulting in changes to the parent's wire buffer
 * 
 */ 

int
of_list_append_bind(of_object_t *parent, of_object_t *child)
{
    if (parent == NULL || child == NULL ||
           parent->wire_object.wbuf == NULL) {
        return OF_ERROR_PARAM;
    }

    if (!of_object_can_grow(parent, parent->length + child->length)) {
        return OF_ERROR_RESOURCE;
    }

    object_child_attach(parent, child, parent->length, 
                        child->length);

    /* Update the wire length and type if needed */
    if (child->wire_length_set) {
        child->wire_length_set(child, child->length);
    }

    if (child->wire_type_set) {
        child->wire_type_set(child);
    }

    /* Update the parent's length */
    of_object_parent_length_update(parent, child->length);

    OF_LENGTH_CHECK_ASSERT(parent);

    return OF_ERROR_NONE;
}

/**
 * Generic atomic list append operation
 * @param list The list to which an item is being appended
 * @param item THe item to append to the list
 *
 * The contents of the item are copied to the end of the list.
 * Currently assumes the list is at the end of its parent.
 */
int
of_list_append(of_object_t *list, of_object_t *item)
{
    int new_len;

    new_len = list->length + item->length;

    if (!of_object_can_grow(list, new_len)) {
        return OF_ERROR_RESOURCE;
    }

    of_wire_buffer_grow(list->wire_object.wbuf,
                        OF_OBJECT_ABSOLUTE_OFFSET(list, new_len));

    MEMCPY(OF_OBJECT_BUFFER_INDEX(list, list->length),
           OF_OBJECT_BUFFER_INDEX(item, 0), item->length);

    /* Update the list's length */
    of_object_parent_length_update(list, item->length);

    OF_LENGTH_CHECK_ASSERT(list);

    return OF_ERROR_NONE;
}

/**
 * Generic list first function
 * @param parent The parent; must be a list object
 * @param child The child object; must be of type list element
 * @return OF_ERROR_RANGE if list is empty
 * @return OF_ERROR_
 *
 * Sets up the child to point to the first element in the list
 *
 * Child init must be called before this is called.
 *
 * @note TREAT AS PRIVATE
 * Does not fully initialized object
 */
int
of_list_first(of_object_t *parent, of_object_t *child)
{
    if (parent->length == 0) { /* Empty list */
        return OF_ERROR_RANGE;
    }

    object_child_attach(parent, child, 0, 0);

    return OF_ERROR_NONE;
}

/**
 * Return boolean indicating if child is pointing to last entry in parent
 * @param parent The parent; must be a list object
 * @param child The child object; must be of type list element
 * @return OF_ERROR_RANGE if list is empty
 * @return OF_ERROR_
 *
 */
static int
of_list_is_last(of_object_t *parent, of_object_t *child)
{
    if (child->wire_object.obj_offset + child->length >= 
        parent->wire_object.obj_offset + parent->length) {
        return 1;
    }

    return 0;
}

/**
 * Generic list next function
 * @param parent The parent; must be a list object
 * @param child The child object; must be of type list element
 * @return OF_ERROR_RANGE if at end of list
 * @return OF_ERROR_
 *
 * Advances the child to point to the subsequent element in the list.
 * The wire buffer object must not have been modified since the 
 * previous call to _first or _next.
 *
 * @note TREAT AS PRIVATE
 * Does not fully initialized object
 */ 
int
of_list_next(of_object_t *parent, of_object_t *child)
{
    int offset;

    LOCI_ASSERT(child->length > 0);

    /* Get offset of parent */
    if (of_list_is_last(parent, child)) {
        return OF_ERROR_RANGE; /* We were on the last object */
    }

    /* Offset is relative to parent start */
    offset = (child->wire_object.obj_offset - parent->wire_object.obj_offset) +
        child->length;
    object_child_attach(parent, child, offset, 0);

    return OF_ERROR_NONE;
}

void
of_object_wire_buffer_steal(of_object_t *obj, uint8_t **buffer)
{
    LOCI_ASSERT(obj != NULL);
    of_wire_buffer_steal(obj->wire_object.wbuf, buffer);
    obj->wire_object.wbuf = NULL;
}

#define _MAX_PARENT_ITERATIONS 4
/**
 * Iteratively update parent lengths thru hierarchy
 * @param obj The object whose length is being updated
 * @param delta The difference between the current and new lengths
 *
 * Note that this includes updating the object itself.  It will
 * iterate thru parents.
 *
 * Assumes delta > 0.
 */
void
of_object_parent_length_update(of_object_t *obj, int delta)
{
#ifndef NDEBUG
    int count = 0;
    of_wire_buffer_t *wbuf;  /* For debug asserts only */
#endif

    while (obj != NULL) {
        LOCI_ASSERT(count++ < _MAX_PARENT_ITERATIONS);
        obj->length += delta;
        if (obj->wire_length_set != NULL) {
            obj->wire_length_set(obj, obj->length);
        }
#ifndef NDEBUG
        wbuf = obj->wire_object.wbuf;
#endif

        /* Asserts for wire length checking */
        LOCI_ASSERT(obj->length + obj->wire_object.obj_offset <=
               WBUF_CURRENT_BYTES(wbuf));
        if (obj->parent == NULL) {
            LOCI_ASSERT(obj->length + obj->wire_object.obj_offset ==
                   WBUF_CURRENT_BYTES(wbuf));
        }

        obj = obj->parent;
    }
}

/**
 * Use the type/length from the wire buffer and init the object
 * @param obj The object being initialized
 * @param base_object_id If > 0, this indicates the base object
 * @param max_len If > 0, the max length to expect for the obj
 * type for inheritance checking
 * @return OF_ERROR_
 *
 * Used for inheritance type objects such as actions and OXMs
 * The type is checked and if valid, the object is initialized.
 * Then the length is taken from the buffer.
 *
 * Note that the object version must already be properly set.
 */
int
of_object_wire_init(of_object_t *obj, of_object_id_t base_object_id,
                    int max_len)
{
    if (obj->wire_type_get != NULL) {
        of_object_id_t id;
        obj->wire_type_get(obj, &id);
        if (!of_wire_id_valid(id, base_object_id)) {
            return OF_ERROR_PARSE;
        }
        obj->object_id = id;
        /* Call the init function for this object type; do not push to wire */
        of_object_init_map[id]((of_object_t *)(obj), obj->version, -1, 0);
    }
    if (obj->wire_length_get != NULL) {
        int length;
        obj->wire_length_get(obj, &length);
        if (length < 0 || (max_len > 0 && length > max_len)) {
            return OF_ERROR_PARSE;
        }
        obj->length = length;
    } else {
        /* @fixme Does this cover everything else? */
        obj->length = of_object_fixed_len[obj->version][base_object_id];
    }

    return OF_ERROR_NONE;
}

/*
 * Set member:
 *    get_wbuf_extent
 *    find offset of start of member
 *    if offset is at wbuf_extent (append new data)
 *        copy data at extent
 *        update parent length
 *    else
 *        find length of current entry
 *        move from end of current to extent to create (or remove) space
 *        copy data to offset
 *        update my length -- NEED LOCAL INFO TO DO THIS for some cases
 */

/* Also need: get offset of member for all combinations */
/* Also need: get length of member for all combinations */
#if 0
/**
 * Append the wire buffer data from src to the end of dst's wire buffer
 */
int
of_object_append_buffer(of_object_t *dst, of_object_t *src)
{
    of_wire_buffer_t *s_wbuf, *d_wbuf;
    int orig_len, dst_offset, src_offset;

    d_wbuf = OF_OBJECT_TO_WBUF(dst);
    s_wbuf = OF_OBJECT_TO_WBUF(src);
    dst_offset = dst->wire_object.obj_offset + dst_length;
    src_offset = src->wire_object.obj_offset;
    OF_WIRE_BUFFER_INIT_CHECK(d_wbuf, dst_offset + src->length);
    MEMCPY(OF_WBUF_BUFFER_POINTER(d_wbuf, dst_offset),
           OF_WBUF_BUFFER_POINTER(s_wbuf, 0), src->length);

    orig_len = dst->length;
    dst->length += src->length;

    return OF_ERROR_NONE;
}

/**
 * Set the length of the actions object in a packet_in object
 */

int
of_packet_out_actions_length_set(of_packet_t *obj, int len)
{
    if (obj == NULL || obj->object_id != OF_PACKET_IN ||
        obj->wire_object.wbuf == NULL) {
        return OF_ERROR_PARAM;
    }

    obj->actions_len_set(obj, len);
}

int
_packet_out_data_offset_get(of_packet_t *obj)
{
    if (obj == NULL || obj->object_id != OF_PACKET_IN ||
        obj->wire_object.wbuf == NULL) {
        return -1;
    }

    return OF_PACKET_OUT_FIXED_LENGTH + _packet_out_actions_length_get(obj);
}


/**
 * Simple length derivation function
 *
 * Most variable length fields are alone at the end of a structure.
 * Their length is a simple calculation, just the total length of
 * the parent minus the length of the non-variable part of the 
 * parent's class type.
 *
 * @param parent The parent object
 * @param length (out) Where to store the length of the final 
 * variable length member
 */
int
of_object_simple_length_derive(of_object_t *obj, int *length)
{
    
}
#endif
