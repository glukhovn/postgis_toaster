/*-------------------------------------------------------------------------
 *
 * postgis_toaster.c
 *		PostGIS toaster.
 *
 * Portions Copyright (c) 2016-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1990-1993, Regents of the University of California
 *
 * IDENTIFICATION
 *	  contrib/postgis_toaster/postgis_toaster.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "access/heapam.h"
#include "access/heaptoast.h"
#include "access/table.h"
#include "access/tableam.h"
#include "access/toasterapi.h"
#include "access/toast_helper.h"
#include "access/toast_internals.h"
#include "catalog/toasting.h"
#include "fmgr.h"
#include "utils/builtins.h"
#include "utils/memutils.h"
#include "utils/varlena.h"

PG_MODULE_MAGIC;

#define POSTGIS_HEADER_SIZE (VARHDRSZ + \
							 4 + /* srid (3) + flags (1) */ \
							 8 + /* extended header */ \
							 2 * 4 * sizeof(float))	/* bounding box (up to 4D) */

typedef struct PostGISToastData
{
	varatt_external ptr;
	int32		inline_size;
	char	   *inline_data; /* [FLEXIBLE_ARRAY_MEMBER] */
} PostGISToastData;

#define VARATT_CUSTOM_POSTGIS_HDRSZ \
	offsetof(PostGISToastData, inline_data)

#define VARATT_CUSTOM_POSTGIS_SIZE(inline_size) \
	VARATT_CUSTOM_SIZE(VARATT_CUSTOM_POSTGIS_HDRSZ + (inline_size))

#define VARATT_CUSTOM_GET_POSTGIS_DATA(attr, data) \
do { \
	varattrib_1b_e *attrc = (varattrib_1b_e *)(attr); \
	Assert(VARATT_IS_CUSTOM(attrc)); \
	Assert(VARSIZE_CUSTOM(attrc) >= VARATT_CUSTOM_POSTGIS_SIZE(0)); \
	memcpy(&(data), VARATT_CUSTOM_GET_DATA(attrc), VARATT_CUSTOM_POSTGIS_HDRSZ); \
	(data).inline_data = VARATT_CUSTOM_GET_DATA(attrc) + VARATT_CUSTOM_POSTGIS_HDRSZ; \
} while (0)

static void
postgis_toaster_init(Relation rel, Datum reloptions, LOCKMODE lockmode,
					 bool check, Oid OIDOldToast)
{
	(void) create_toast_table(rel, InvalidOid, InvalidOid, reloptions,
							  lockmode, check, OIDOldToast);
}

static bool
postgis_toaster_validate(Oid typeoid, char storage, char compression,
						 Oid amoid, bool false_ok)

{
	return true; /* typeoid == GEOMETRYOID || typeoid == GEOGRAPHYOID */
}

static struct varlena *
postgis_toaster_make_pointer(Oid toasterid, struct varatt_external *ptr,
							 Size inline_size, char *data)
{
	Size		size = VARATT_CUSTOM_POSTGIS_SIZE(inline_size);
	struct varlena *result = palloc(size);
	PostGISToastData result_data;

	SET_VARTAG_EXTERNAL(result, VARTAG_CUSTOM);

	VARATT_CUSTOM_SET_TOASTERID(result, toasterid);
	VARATT_CUSTOM_SET_DATA_RAW_SIZE(result, ptr->va_rawsize);
	VARATT_CUSTOM_SET_DATA_SIZE(result, VARATT_CUSTOM_POSTGIS_HDRSZ + inline_size);

	result_data.ptr = *ptr;
	result_data.inline_size = inline_size;

	memcpy(VARATT_CUSTOM_GET_DATA(result), &result_data, VARATT_CUSTOM_POSTGIS_HDRSZ);
	memcpy(VARATT_CUSTOM_GET_DATA(result) + VARATT_CUSTOM_POSTGIS_HDRSZ, data, inline_size);

	return result;
}

static void
postgis_toaster_delete_toast(Datum old_val, bool is_speculative)
{
	if (VARATT_IS_CUSTOM(old_val))
	{
		PostGISToastData old_data;
		char		ptr[TOAST_POINTER_SIZE];

		VARATT_CUSTOM_GET_POSTGIS_DATA(old_val, old_data);

		SET_VARTAG_EXTERNAL(ptr, VARTAG_ONDISK);
		memcpy(VARDATA_EXTERNAL(ptr), &old_data.ptr, sizeof(old_data.ptr));

		toast_delete_datum(PointerGetDatum(ptr), is_speculative);
	}
}

static struct varlena *
postgis_toaster_copy(Relation rel, Oid toasterid, Datum newval,
					 int inline_size, int options)
{
	struct varatt_external toast_ptr;
	struct varlena *res;
	void	   *detoasted = detoast_attr((void *) DatumGetPointer(newval));
	void	   *toasted = DatumGetPointer(
		toast_save_datum(rel, PointerGetDatum(detoasted), NULL, options));

	Assert(VARATT_IS_EXTERNAL_ONDISK(toasted));
	VARATT_EXTERNAL_GET_POINTER(toast_ptr, toasted);

	res = postgis_toaster_make_pointer(toasterid, &toast_ptr, inline_size,
									   VARDATA_ANY(detoasted));

	if (detoasted != (void *) DatumGetPointer(newval))
		pfree(detoasted);

	pfree(toasted);

	return res;
}

static struct varlena *
postgis_toaster_toast(Relation rel, Oid toasterid,
					  Datum newval, Datum oldval,
					  int max_inline_size, int options)
{
	int			inline_size =
		max_inline_size >= VARATT_CUSTOM_POSTGIS_SIZE(POSTGIS_HEADER_SIZE) ? POSTGIS_HEADER_SIZE : 0;

	if (VARATT_IS_CUSTOM(newval) &&
		VARATT_CUSTOM_GET_TOASTERID(newval) == toasterid)
	{
		PostGISToastData data;

		VARATT_CUSTOM_GET_POSTGIS_DATA(newval, data);

		return postgis_toaster_make_pointer(toasterid, &data.ptr,
											Min(data.inline_size, inline_size),
											data.inline_data);
	}

	return postgis_toaster_copy(rel, toasterid, newval, inline_size, options);
}

static struct varlena *
postgis_toaster_update_toast(Relation rel, Oid toasterid,
							 Datum newval, Datum oldval, int options)
{
	PostGISToastData old_data;
	PostGISToastData new_data;
	Oid			toastrelid = rel->rd_rel->reltoastrelid;
	bool		is_speculative = false; /* (options & HEAP_INSERT_SPECULATIVE) != 0 XXX */

	Assert(VARATT_IS_CUSTOM(newval) && VARATT_CUSTOM_GET_TOASTERID(newval) == toasterid &&
		   VARATT_IS_CUSTOM(oldval) && VARATT_CUSTOM_GET_TOASTERID(oldval) == toasterid);

	VARATT_CUSTOM_GET_POSTGIS_DATA(oldval, old_data);
	VARATT_CUSTOM_GET_POSTGIS_DATA(newval, new_data);

	if (new_data.ptr.va_toastrelid == toastrelid &&
		!memcmp(&old_data.ptr, &new_data.ptr, sizeof(old_data.ptr)))
		return NULL;

	postgis_toaster_delete_toast(oldval, is_speculative);

	return postgis_toaster_copy(rel, toasterid, newval, POSTGIS_HEADER_SIZE, options);
}

static struct varlena *
postgis_toaster_copy_toast(Relation rel, Oid toasterid,
						   Datum newval, int options)
{
	return postgis_toaster_copy(rel, toasterid, newval, POSTGIS_HEADER_SIZE, options);
}

static struct varlena *
postgis_toaster_detoast(Datum toastptr, int offset, int length)
{
	PostGISToastData data;
	int32		attr_size;
	char		ptr[TOAST_POINTER_SIZE];

	Assert(VARATT_IS_CUSTOM(toastptr));
	VARATT_CUSTOM_GET_POSTGIS_DATA(toastptr, data);

	attr_size = VARATT_EXTERNAL_GET_EXTSIZE(data.ptr);

	if (length >= 0 &&
		offset + length <= data.inline_size)
	{
		struct varlena *result = (struct varlena *) palloc(length + VARHDRSZ);
		SET_VARSIZE(result, length + VARHDRSZ);
		memcpy(VARDATA(result), data.inline_data + offset, length);
		return result;
	}

	SET_VARTAG_EXTERNAL(ptr, VARTAG_ONDISK);
	memcpy(VARDATA_EXTERNAL(ptr), &data.ptr, sizeof(data.ptr));

	if (offset == 0 && (length < 0 || length >= attr_size))
		return toast_fetch_datum((struct varlena *) ptr);
	else
		return toast_fetch_datum_slice((struct varlena *) ptr, offset, length);
}

PG_FUNCTION_INFO_V1(postgis_toaster_handler);
Datum
postgis_toaster_handler(PG_FUNCTION_ARGS)
{
	TsrRoutine *tsr = makeNode(TsrRoutine);

	tsr->init = postgis_toaster_init;
	tsr->toast = postgis_toaster_toast;
	tsr->deltoast = postgis_toaster_delete_toast;
	tsr->copy_toast = postgis_toaster_copy_toast;
	tsr->update_toast = postgis_toaster_update_toast;
	tsr->detoast = postgis_toaster_detoast;
	tsr->toastervalidate = postgis_toaster_validate;
	tsr->get_vtable = NULL;

	PG_RETURN_POINTER(tsr);
}
