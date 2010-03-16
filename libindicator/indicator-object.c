/*
An object to represent loadable indicator modules to make loading
them easy and objectified.

Copyright 2009 Canonical Ltd.

Authors:
    Ted Gould <ted@canonical.com>

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
version 3.0 as published by the Free Software Foundation.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License version 3.0 for more details.

You should have received a copy of the GNU General Public
License along with this library. If not, see
<http://www.gnu.org/licenses/>.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "indicator.h"
#include "indicator-object.h"
#include "indicator-object-marshal.h"
#include "indicator-object-enum-types.h"

/**
	IndicatorObjectPrivate:
	@module: The loaded module representing the object.  Note to
		subclasses: This will not be set when you're initalized.
	@entry: A default entry for objects that don't need all the
		fancy stuff.  This works with #get_entries_default.
	@gotten_entries: A check to see if the @entry has been
		populated intelligently yet.

	Structure to define the memory for the private area
	of the object instance.
*/
struct _IndicatorObjectPrivate {
	GModule * module;

	/* For get_entries_default */
	IndicatorObjectEntry entry;
	gboolean gotten_entries;
};

#define INDICATOR_OBJECT_GET_PRIVATE(o) (INDICATOR_OBJECT(o)->priv)

/* Signals Stuff */
enum {
	ENTRY_ADDED,
	ENTRY_REMOVED,
	ENTRY_MOVED,
        SCROLL,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

/* GObject stuff */
static void indicator_object_class_init (IndicatorObjectClass *klass);
static void indicator_object_init       (IndicatorObject *self);
static void indicator_object_dispose    (GObject *object);
static void indicator_object_finalize   (GObject *object);

static GList * get_entries_default (IndicatorObject * io);

G_DEFINE_TYPE (IndicatorObject, indicator_object, G_TYPE_OBJECT);

/* Setup the class and put the functions into the
   class structure */
static void
indicator_object_class_init (IndicatorObjectClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (IndicatorObjectPrivate));

	object_class->dispose = indicator_object_dispose;
	object_class->finalize = indicator_object_finalize;

	klass->get_label =  NULL;
	klass->get_menu =   NULL;
	klass->get_image =  NULL;

	klass->get_entries = get_entries_default;
	klass->get_location = NULL;

	/**
		IndicatorObject::entry-added:
		@arg0: The #IndicatorObject object
		@arg1: A pointer to the #IndicatorObjectEntry that
			is being added.

		Signaled when a new entry is added and should
		be shown by the person using this object.
	*/
	signals[ENTRY_ADDED] = g_signal_new (INDICATOR_OBJECT_SIGNAL_ENTRY_ADDED,
	                                     G_TYPE_FROM_CLASS(klass),
	                                     G_SIGNAL_RUN_LAST,
	                                     G_STRUCT_OFFSET (IndicatorObjectClass, entry_added),
	                                     NULL, NULL,
	                                     g_cclosure_marshal_VOID__POINTER,
	                                     G_TYPE_NONE, 1, G_TYPE_POINTER, G_TYPE_NONE);

	/**
		IndicatorObject::entry-removed:
		@arg0: The #IndicatorObject object
		@arg1: A pointer to the #IndicatorObjectEntry that
			is being removed.

		Signaled when an entry is removed and should
		be removed by the person using this object.
	*/
	signals[ENTRY_REMOVED] = g_signal_new (INDICATOR_OBJECT_SIGNAL_ENTRY_REMOVED,
	                                       G_TYPE_FROM_CLASS(klass),
	                                       G_SIGNAL_RUN_LAST,
	                                       G_STRUCT_OFFSET (IndicatorObjectClass, entry_removed),
	                                       NULL, NULL,
	                                       g_cclosure_marshal_VOID__POINTER,
	                                       G_TYPE_NONE, 1, G_TYPE_POINTER, G_TYPE_NONE);
	/**
		IndicatorObject::entry-moved:
		@arg0: The #IndicatorObject object
		@arg1: A pointer to the #IndicatorObjectEntry that
			is being moved.
		@arg2: The old location of the entry
		@arg3: The new location of the entry

		When the order of the entries change, then this signal
		is sent to tell the new location.
	*/
	signals[ENTRY_MOVED] = g_signal_new (INDICATOR_OBJECT_SIGNAL_ENTRY_MOVED,
	                                     G_TYPE_FROM_CLASS(klass),
	                                     G_SIGNAL_RUN_LAST,
	                                     G_STRUCT_OFFSET (IndicatorObjectClass, entry_moved),
	                                     NULL, NULL,
	                                     _indicator_object_marshal_VOID__POINTER_UINT_UINT,
	                                     G_TYPE_NONE, 3, G_TYPE_POINTER, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_NONE);


        /**
                IndicatorObject::scroll:
                @arg0: The #IndicatorObject object
                @arg1: The delta of the scroll event
                @arg2: The orientation of the scroll event. %TRUE indicates up, %FALSE down.

                When the indicator receives a mouse scroll wheel event
                from the user, this signal is emitted.
         */
        signals[SCROLL] = g_signal_new (INDICATOR_OBJECT_SIGNAL_SCROLL,
                                        G_TYPE_FROM_CLASS(klass),
                                        G_SIGNAL_RUN_LAST,
                                        G_STRUCT_OFFSET (IndicatorObjectClass, scroll),
                                        NULL, NULL,
                                        _indicator_object_marshal_VOID__UINT_ENUM,
                                        G_TYPE_NONE, 2, G_TYPE_UINT, INDICATOR_OBJECT_TYPE_SCROLL_DIRECTION);


	return;
}

/* Initialize an instance */
static void
indicator_object_init (IndicatorObject *self)
{
	self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, INDICATOR_OBJECT_TYPE, IndicatorObjectPrivate);

	self->priv->module = NULL;

	self->priv->entry.menu = NULL;
	self->priv->entry.label = NULL;
	self->priv->entry.image = NULL;

	self->priv->gotten_entries = FALSE;

	return;
}

/* Unref the objects that we're holding on to. */
static void
indicator_object_dispose (GObject *object)
{

	G_OBJECT_CLASS (indicator_object_parent_class)->dispose (object);
	return;
}

/* A small helper function that closes a module but
   in the function prototype of a GSourceFunc. */
static gboolean
module_unref (gpointer data)
{
	if (!g_module_close((GModule *)data)) {
		/* All we can do is warn. */
		g_warning("Unable to close module!");
	}
	return FALSE;
}

/* Free memory */
static void
indicator_object_finalize (GObject *object)
{
	IndicatorObjectPrivate * priv = INDICATOR_OBJECT_GET_PRIVATE(object);

	if (priv->module != NULL) {
		/* Wow, this is convoluted.  So basically we want to unref
		   the module which will cause the code it included to be
		   removed.  But, since it's finalize function is the function
		   that called this one, we can't really remove it before
		   it finishes being executed.  So we're putting the job into
		   the main loop to remove it the next time it gets a chance.
		   Slightly non-deterministic, but should work. */
		g_idle_add(module_unref, priv->module);
		priv->module = NULL;
	}

	G_OBJECT_CLASS (indicator_object_parent_class)->finalize (object);
	return;
}

/**
	indicator_object_new_from_file:
	@file: Filename containing a loadable module

	This function builds an #IndicatorObject using the symbols
	that are found in @file.  The module is loaded and the
	references are all kept by the object.  To unload the
	module the object must be destroyed.

	Return value: A valid #IndicatorObject or #NULL if error.
*/
IndicatorObject *
indicator_object_new_from_file (const gchar * file)
{
	GObject * object = NULL;
	GModule * module = NULL;

	/* Check to make sure the name exists and that the
	   file itself exists */
	if (file == NULL) {
		g_warning("Invalid filename.");
		return NULL;
	}

	if (!g_file_test(file, G_FILE_TEST_EXISTS)) {
		g_warning("File '%s' does not exist.", file);
		return NULL;
	}

	/* Grab the g_module reference, pull it in but let's
	   keep the symbols local to avoid conflicts. */
	module = g_module_open(file,
                           G_MODULE_BIND_LAZY | G_MODULE_BIND_LOCAL);
	if (module == NULL) {
		g_warning("Unable to load module: %s", file);
		return NULL;
	}

	/* Look for the version function, error if not found. */
	get_version_t lget_version = NULL;
	if (!g_module_symbol(module, INDICATOR_GET_VERSION_S, (gpointer *)(&lget_version))) {
		g_warning("Unable to get the symbol for getting the version.");
		return NULL;
	}

	/* Check the version with the macro and make sure we're
	   all talking the same language. */
	if (!INDICATOR_VERSION_CHECK(lget_version())) {
		g_warning("Indicator using API version '%s' we're expecting '%s'", lget_version(), INDICATOR_VERSION);
		return NULL;
	}

	/* The function for grabbing a label from the module
	   execute it, and make sure everything is a-okay */
	get_type_t lget_type = NULL;
	if (!g_module_symbol(module, INDICATOR_GET_TYPE_S, (gpointer *)(&lget_type))) {
		g_warning("Unable to get '" INDICATOR_GET_TYPE_S "' symbol from module: %s", file);
		goto unrefandout;
	}
	if (lget_type == NULL) {
		g_warning("Symbol '" INDICATOR_GET_TYPE_S "' is (null) in module: %s", file);
		goto unrefandout;
	}

	/* A this point we allocate the object, any code beyond
	   here needs to deallocate it if we're returning in an
	   error'd state. */
	object = g_object_new(lget_type(), NULL);
	if (object == NULL) {
		g_warning("Unable to build an object if type '%d' in module: %s", (gint)lget_type(), file);
		goto unrefandout;
	}
	if (!INDICATOR_IS_OBJECT(object)) {
		g_warning("Type '%d' in file %s is not a subclass of IndicatorObject.", (gint)lget_type(), file);
		goto unrefandout;
	}

	IndicatorObjectPrivate * priv = INDICATOR_OBJECT_GET_PRIVATE(object);
	/* Now we can track the module */
	priv->module = module;

	return INDICATOR_OBJECT(object);

	/* Error, let's drop the object and return NULL.  Sad when
	   this happens. */
unrefandout:
	if (object != NULL) {
		g_object_unref(object);
	}
	if (module != NULL) {
		g_object_unref(module);
	}
	g_warning("Error building IndicatorObject from file: %s", file);
	return NULL;
}

/* The default get entries function uses the other single
   entries in the class to create an entry structure and
   put it into a list.  This makes it simple for simple objects
   to create the list.  Small changes from the way they
   previously were. */
static GList *
get_entries_default (IndicatorObject * io)
{
	IndicatorObjectPrivate * priv = INDICATOR_OBJECT_GET_PRIVATE(io);

	if (!priv->gotten_entries) {
		IndicatorObjectClass * class = INDICATOR_OBJECT_GET_CLASS(io);

		if (class->get_label) {
			priv->entry.label = class->get_label(io);
		}

		if (class->get_image) {
			priv->entry.image = class->get_image(io);
		}

		if (priv->entry.image == NULL && priv->entry.label == NULL) {
			g_warning("IndicatorObject class does not create an image or a label.  We need one of those.");
			return NULL;
		}

		if (class->get_menu) {
			priv->entry.menu = class->get_menu(io);
		}

		if (priv->entry.menu == NULL) {
			g_warning("IndicatorObject class does not create a menu.  We need one of those.");
			return NULL;
		}

		priv->gotten_entries = TRUE;
	}

	return g_list_append(NULL, &(priv->entry));
}

/**
	indicator_object_get_entires:
	@io: #IndicatorObject to query

	This function looks on the class for the object and calls
	it's #IndicatorObjectClass::get_entries function.  The
	list should be owned by the caller, but the individual
	enteries should not be.

	Return value: A list if #IndicatorObjectEntry structures or
		NULL if there is an error.
*/
GList *
indicator_object_get_entries (IndicatorObject * io)
{
	g_return_val_if_fail(INDICATOR_IS_OBJECT(io), NULL);
	IndicatorObjectClass * class = INDICATOR_OBJECT_GET_CLASS(io);

	if (class->get_entries) {
		return class->get_entries(io);
	}

	g_error("No get_entries function on object.  It must have been deleted?!?!");
	return NULL;
}

/**
	indicator_object_get_location:
	@io: #IndicatorObject to query
	@entry: The #IndicatorObjectEntry to look for.

	This function looks on the class for the object and calls
	it's #IndicatorObjectClass::get_location function.  If the
	function doesn't exist it returns zero.

	Return value: Location of the @entry in the display or
		zero if no location is specified.
*/
guint
indicator_object_get_location (IndicatorObject * io, IndicatorObjectEntry * entry)
{
	g_return_val_if_fail(INDICATOR_IS_OBJECT(io), 0);
	IndicatorObjectClass * class = INDICATOR_OBJECT_GET_CLASS(io);

	if (class->get_location) {
		return class->get_location(io, entry);
	}

	return 0;
}
