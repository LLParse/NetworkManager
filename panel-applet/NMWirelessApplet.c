/* NetworkManager Wireless Applet -- Display wireless access points and allow user control
 *
 * Dan Williams <dcbw@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * This applet used the GNOME Wireless Applet as a skeleton to build from.
 *
 * GNOME Wireless Applet Authors:
 *		Eskil Heyn Olsen <eskil@eskil.dk>
 *		Bastien Nocera <hadess@hadess.net> (Gnome2 port)
 *
 * (C) Copyright 2004 Red Hat, Inc.
 * (C) Copyright 2001, 2002 Free Software Foundation
 */

#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <dirent.h>

#include "config.h"

#include <gnome.h>

#include <libgnomeui/libgnomeui.h>
#include <glade/glade.h>
#include <gconf/gconf-client.h>

#include "NMWirelessApplet.h"
#include "NMWirelessAppletDbus.h"
#include "menu-info.h"

#define CFG_UPDATE_INTERVAL 1
#define NM_GCONF_WIRELESS_NETWORKS_PATH		"/system/networking/wireless/networks"

static char *glade_file;

static GtkWidget *	nmwa_populate_menu	(NMWirelessApplet *applet);
static void		nmwa_dispose_menu_items (NMWirelessApplet *applet);
static gboolean	do_not_eat_button_press (GtkWidget *widget, GdkEventButton *event);
static GObject * nmwa_constructor (GType type, guint n_props, GObjectConstructParam *construct_props);
static void   setup_stock (void);
static void nmwa_icons_init (NMWirelessApplet *applet);
static gboolean nmwa_fill (NMWirelessApplet *applet);


#ifndef BUILD_NOTIFICATION_ICON
static const BonoboUIVerb nmwa_context_menu_verbs [] =
{
	BONOBO_UI_UNSAFE_VERB ("NMWirelessAbout", nmwa_about_cb),
	BONOBO_UI_VERB_END
};
#endif

G_DEFINE_TYPE(NMWirelessApplet, nmwa, EGG_TYPE_TRAY_ICON)


static void
nmwa_init (NMWirelessApplet *applet)
{
  applet->animation_id = 0;
  applet->animation_step = 0;

  setup_stock ();
  nmwa_icons_init (applet);
  nmwa_fill (applet);
}

static void nmwa_class_init (NMWirelessAppletClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->constructor = nmwa_constructor;
}

static GObject *nmwa_constructor (GType type,
		 		  guint n_props,
				  GObjectConstructParam *construct_props)
{
  GObject *obj;
  NMWirelessApplet *applet;
  NMWirelessAppletClass *klass;

  klass = NM_WIRELESS_APPLET_CLASS (g_type_class_peek (type));
  obj = G_OBJECT_CLASS (nmwa_parent_class)->constructor (type,
							 n_props,
							 construct_props);
  applet =  NM_WIRELESS_APPLET (obj);

  return obj;
}

static gboolean
animation_timeout (NMWirelessApplet *applet)
{
  switch (applet->applet_state)
    {
    case (APPLET_STATE_WIRED_CONNECTING):
      applet->animation_step ++;
      if (applet->animation_step >= NUM_WIRED_CONNECTING_FRAMES)
	applet->animation_step = 0;
      gtk_image_set_from_pixbuf (GTK_IMAGE (applet->pixmap),
				 applet->wired_connecting_icons[applet->animation_step]);
      break;
    case (APPLET_STATE_WIRELESS_CONNECTING):
      applet->animation_step ++;
      if (applet->animation_step >= NUM_WIRELESS_CONNECTING_FRAMES)
	applet->animation_step = 0;
      gtk_image_set_from_pixbuf (GTK_IMAGE (applet->pixmap),
				 applet->wireless_connecting_icons[applet->animation_step]);
      break;
    case (APPLET_STATE_WIRELESS_SCANNING):
      applet->animation_step ++;
      if (applet->animation_step >= NUM_WIRELESS_SCANNING_FRAMES)
	applet->animation_step = 0;
      gtk_image_set_from_pixbuf (GTK_IMAGE (applet->pixmap),
				 applet->wireless_scanning_icons[applet->animation_step]);
      break;
    default:
      break;
    }
  return TRUE;
}

/*
 * nmwa_update_state
 *
 * Figure out what the currently active device is from NetworkManager, its type,
 * and what our icon on the panel should look like for each type.
 *
 */
static void
nmwa_update_state (NMWirelessApplet *applet)
{
  gboolean show_applet = TRUE;
  gboolean need_animation = FALSE;
  GdkPixbuf *pixbuf = NULL;
  gint strength = -1;

  g_mutex_lock (applet->data_mutex);
  if (applet->active_device)
    {
      GSList *list;
      for (list = applet->active_device->networks; list; list = list->next)
	{
	  WirelessNetwork *network;

	  network = (WirelessNetwork *) list->data;
	  if (network->active)
	    strength = CLAMP ((int) network->strength, 0, 100);
	}

      if (strength == -1)
	strength = applet->active_device->strength;

    }

  if (g_slist_length (applet->devices) == 1 &&
      applet->applet_state != APPLET_STATE_NO_NM)
    {
      if (((NetworkDevice *)applet->devices->data)->type == DEVICE_TYPE_WIRED_ETHERNET)
	show_applet = FALSE;
    }
  g_mutex_unlock (applet->data_mutex);

  g_print ("%d\n", applet->applet_state);
  switch (applet->applet_state)
    {
    case (APPLET_STATE_NO_NM):
      pixbuf = applet->no_nm_icon;
      break;
    case (APPLET_STATE_NO_CONNECTION):
      show_applet = FALSE;
      break;
    case (APPLET_STATE_WIRED):
      pixbuf = applet->wired_icon;
      break;
    case (APPLET_STATE_WIRED_CONNECTING):
      applet->animation_step = CLAMP (applet->animation_step, 0, NUM_WIRED_CONNECTING_FRAMES - 1);
      pixbuf = applet->wired_connecting_icons[applet->animation_step];
      need_animation = TRUE;
      break;
    case (APPLET_STATE_WIRELESS):
      if (applet->active_device)
	{
	  if (strength > 75)
	    pixbuf = applet->wireless_100_icon;
	  else if (strength > 50)
	    pixbuf = applet->wireless_75_icon;
	  else if (strength > 25)
	    pixbuf = applet->wireless_50_icon;
	  else if (strength > 0)
	    pixbuf = applet->wireless_25_icon;
	  else
	    pixbuf = applet->wireless_00_icon;
	}
      break;
    case (APPLET_STATE_WIRELESS_CONNECTING):
      applet->animation_step = CLAMP (applet->animation_step, 0, NUM_WIRELESS_CONNECTING_FRAMES - 1);
      pixbuf = applet->wireless_connecting_icons[applet->animation_step];
      need_animation = TRUE;
      break;
    case (APPLET_STATE_WIRELESS_SCANNING):
      applet->animation_step = CLAMP (applet->animation_step, 0, NUM_WIRELESS_SCANNING_FRAMES - 1);
      pixbuf = applet->wireless_scanning_icons[applet->animation_step];
      need_animation = TRUE;
    default:
      break;
    }

  /*determine if we should hide the notification icon*/
  gtk_image_set_from_pixbuf (GTK_IMAGE (applet->pixmap), pixbuf);

  if (show_applet)
    gtk_widget_show (GTK_WIDGET (applet));
  else
    gtk_widget_hide (GTK_WIDGET (applet));

  if (applet->animation_id)
    g_source_remove (applet->animation_id);
  if (need_animation)
    applet->animation_id =
      g_timeout_add (125, (GSourceFunc) (animation_timeout), applet);
}


/*
 * nmwa_redraw_timeout
 *
 * Called regularly to update the applet's state and icon in the panel
 *
 */
static int nmwa_redraw_timeout (NMWirelessApplet *applet)
{
	nmwa_update_state (applet);

  	return (TRUE);
}

static void nmwa_start_redraw_timeout (NMWirelessApplet *applet)
{
	applet->redraw_timeout_id =
	     g_timeout_add (CFG_UPDATE_INTERVAL * 1000, (GtkFunction) nmwa_redraw_timeout, applet);
}



/*
 * show_warning_dialog
 *
 * pop up a warning or error dialog with certain text
 *
 */
static void show_warning_dialog (gboolean error, gchar *mesg, ...)
{
	GtkWidget	*dialog;
	char		*tmp;
	va_list	 ap;

	va_start (ap,mesg);
	tmp = g_strdup_vprintf (mesg,ap);
	dialog = gtk_message_dialog_new (NULL, 0, error ? GTK_MESSAGE_ERROR : GTK_MESSAGE_WARNING,
								GTK_BUTTONS_OK, mesg, NULL);
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
	g_free (tmp);
	va_end (ap);
}



/*
 * nmwa_destroy
 *
 * Destroy the applet and clean up its data
 *
 */
static void nmwa_destroy (NMWirelessApplet *applet, gpointer user_data)
{
	if (applet->menu)
		nmwa_dispose_menu_items (applet);

	if (applet->redraw_timeout_id > 0)
	{
		gtk_timeout_remove (applet->redraw_timeout_id);
		applet->redraw_timeout_id = 0;
	}

	if (applet->gconf_client)
		g_object_unref (G_OBJECT (applet->gconf_client));
}


/*
 * nmwa_update_network_timestamp
 *
 * Update the timestamp of a network in GConf.
 *
 */
static void nmwa_update_network_timestamp (NMWirelessApplet *applet, const WirelessNetwork *network)
{
	char			*key;

	g_return_if_fail (applet != NULL);
	g_return_if_fail (network != NULL);

	/* Update GConf to set timestamp for this network, or add it if
	 * it doesn't already exist.
	 */

	/* Update timestamp on network */
	key = g_strdup_printf ("%s/%s/timestamp", NM_GCONF_WIRELESS_NETWORKS_PATH, network->essid);
	gconf_client_set_int (applet->gconf_client, key, time (NULL), NULL);
	g_free (key);

	/* Force-set the essid too so that we have a semi-complete network entry */
	key = g_strdup_printf ("%s/%s/essid", NM_GCONF_WIRELESS_NETWORKS_PATH, network->essid);
	gconf_client_set_string (applet->gconf_client, key, network->essid, NULL);
	g_free (key);
}


/*
 * nmwa_get_device_network_for_essid
 *
 * Searches the network list for a given network device and returns the
 * Wireless Network structure corresponding to it.
 *
 */
WirelessNetwork *nmwa_get_device_network_for_essid (NMWirelessApplet *applet, NetworkDevice *dev, const char *essid)
{
	WirelessNetwork	*found_network = NULL;
	GSList			*element;

	g_return_val_if_fail (applet != NULL, NULL);
	g_return_val_if_fail (dev != NULL, NULL);
	g_return_val_if_fail (essid != NULL, NULL);
	g_return_val_if_fail (strlen (essid), NULL);

	g_mutex_lock (applet->data_mutex);
	element = dev->networks;
	while (element)
	{
		WirelessNetwork	*network = (WirelessNetwork *)(element->data);
		if (network && (strcmp (network->essid, essid) == 0))
		{
			found_network = network;
			break;
		}
		element = g_slist_next (element);
	}
	g_mutex_unlock (applet->data_mutex);

	return (found_network);
}


/*
 * nmwa_get_device_for_nm_device
 *
 * Searches the device list for a device that matches the
 * NetworkManager ID given.
 *
 */
NetworkDevice *nmwa_get_device_for_nm_device (NMWirelessApplet *applet, const char *nm_dev)
{
	NetworkDevice	*found_dev = NULL;
	GSList		*element;

	g_return_val_if_fail (applet != NULL, NULL);
	g_return_val_if_fail (nm_dev != NULL, NULL);
	g_return_val_if_fail (strlen (nm_dev), NULL);

	g_mutex_lock (applet->data_mutex);
	element = applet->devices;
	while (element)
	{
		NetworkDevice	*dev = (NetworkDevice *)(element->data);
		if (dev && (strcmp (dev->nm_device, nm_dev) == 0))
		{
			found_dev = dev;
			break;
		}
		element = g_slist_next (element);
	}
	g_mutex_unlock (applet->data_mutex);

	return (found_dev);
}


/*
 * nmwa_menu_item_activate
 *
 * Signal function called when user clicks on a menu item
 *
 */
static void nmwa_menu_item_activate (GtkMenuItem *item, gpointer user_data)
{
	NMWirelessApplet	*applet = (NMWirelessApplet *)user_data;
	NetworkDevice		*dev = NULL;
	WirelessNetwork	*net = NULL;
	char				*tag;

	g_return_if_fail (item != NULL);
	g_return_if_fail (applet != NULL);

	if ((tag = g_object_get_data (G_OBJECT (item), "network")))
	{
		char	*item_dev = g_object_get_data (G_OBJECT (item), "nm_device");

		if (item_dev && (dev = nmwa_get_device_for_nm_device (applet, item_dev)))
			if ((net = nmwa_get_device_network_for_essid (applet, dev, tag)))
				nmwa_update_network_timestamp (applet, net);
	}
	else if ((tag = g_object_get_data (G_OBJECT (item), "device")))
		dev = nmwa_get_device_for_nm_device (applet, tag);

	if (dev)
		nmwa_dbus_set_device (applet->connection, dev, net);
}


/*
 * nmwa_toplevel_menu_activate
 *
 * Pop up the wireless networks menu in response to a click on the applet
 *
 */
static void nmwa_toplevel_menu_activate (GtkWidget *menu, NMWirelessApplet *applet)
{
	nmwa_dispose_menu_items (applet);
	nmwa_populate_menu (applet);
	gtk_widget_show (applet->menu);
}


/*
 * nmwa_menu_add_separator_item
 *
 */
static void nmwa_menu_add_separator_item (GtkWidget *menu)
{
	GtkWidget	*menu_item;

	menu_item = gtk_separator_menu_item_new ();
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
	gtk_widget_show (menu_item);
}


/*
 * nmwa_menu_add_text_item
 *
 * Add a non-clickable text item to a menu
 *
 */
static void nmwa_menu_add_text_item (GtkWidget *menu, char *text)
{
	GtkWidget		*menu_item;

	g_return_if_fail (text != NULL);
	g_return_if_fail (menu != NULL);

	menu_item = gtk_menu_item_new_with_label (text);
	gtk_widget_set_sensitive (menu_item, FALSE);

	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
	gtk_widget_show (menu_item);
}


/*
 * nmwa_menu_add_device_item
 *
 * Add a network device to the menu
 *
 */
static void nmwa_menu_add_device_item (GtkWidget *menu, NetworkDevice *device, gboolean current, gint n_devices, NMWirelessApplet *applet)
{
	GtkWidget		*menu_item;

	g_return_if_fail (menu != NULL);

	if (device->type == DEVICE_TYPE_WIRED_ETHERNET)
	{
	     menu_item = nm_menu_wired_new ();
	     nm_menu_wired_update (NM_MENU_WIRED (menu_item), device, n_devices);
	     if (applet->active_device == device)
		gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (menu_item), TRUE);
	}
	else
	{
	     menu_item = nm_menu_network_new ();
	     nm_menu_network_update (NM_MENU_NETWORK (menu_item), device, n_devices);
	}

	g_object_set_data (G_OBJECT (menu_item), "device", g_strdup (device->nm_device));
	g_signal_connect(G_OBJECT (menu_item), "activate", G_CALLBACK(nmwa_menu_item_activate), applet);

	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
	gtk_widget_show (menu_item);
}

static void nmwa_menu_add_custom_essid_item (GtkWidget *menu, NMWirelessApplet *applet)
{
	GtkWidget *menu_item;
	GtkWidget *label;

	menu_item = gtk_menu_item_new ();
	label = gtk_label_new (_("Other Wireless Networks..."));
	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
	gtk_container_add (GTK_CONTAINER (menu_item), label);
	gtk_widget_show_all (menu_item);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
}


/*
 * nmwa_menu_device_add_networks
 *
 */
static void nmwa_menu_device_add_networks (GtkWidget *menu, NetworkDevice *dev, NMWirelessApplet *applet)
{
	GSList *list;
	gboolean has_encrypted = FALSE;

	g_return_if_fail (menu != NULL);
	g_return_if_fail (applet != NULL);
	g_return_if_fail (dev != NULL);

	if (dev->type != DEVICE_TYPE_WIRELESS_ETHERNET)
		return;

	/* Check for any security */
	for (list = dev->networks; list; list = list->next)
	{
		WirelessNetwork *network = list->data;

		if (network->encrypted)
			has_encrypted = TRUE;
	}

	/* Add all networks in our network list to the menu */
	for (list = dev->networks; list; list = list->next)
	{
		GtkWidget *menu_item;
		WirelessNetwork *net;

		net = (WirelessNetwork *) list->data;

		menu_item = nm_menu_wireless_new (applet->encryption_size_group);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
		if (applet->active_device == dev && net->active)
			gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (menu_item), TRUE);
		nm_menu_wireless_update (NM_MENU_WIRELESS (menu_item), net, has_encrypted);

		g_object_set_data (G_OBJECT (menu_item), "network", g_strdup (net->essid));
		g_object_set_data (G_OBJECT (menu_item), "nm_device", g_strdup (dev->nm_device));
		g_signal_connect(G_OBJECT (menu_item), "activate", G_CALLBACK (nmwa_menu_item_activate), applet);

		gtk_widget_show (menu_item);
	}
}

static int
sort_networks_function (gconstpointer a, gconstpointer b)
{
	NetworkDevice *dev_a = (NetworkDevice *) a;
	NetworkDevice *dev_b = (NetworkDevice *) b;
	char *name_a;
	char *name_b;

	if (dev_a->hal_name)
		name_a = dev_a->hal_name;
	else if (dev_a->nm_name)
		name_a = dev_a->nm_name;
	else
		name_a = "";

	if (dev_b->hal_name)
		name_b = dev_b->hal_name;
	else if (dev_b->nm_name)
		name_b = dev_b->nm_name;
	else
		name_b = "";

	if (dev_a->type == dev_b->type)
	{
		return strcmp (name_a, name_b);
	}
	if (dev_a->type == DEVICE_TYPE_WIRED_ETHERNET)
		return -1;
	if (dev_b->type == DEVICE_TYPE_WIRED_ETHERNET)
		return 1;
	if (dev_a->type == DEVICE_TYPE_WIRELESS_ETHERNET)
		return -1;
	if (dev_b->type == DEVICE_TYPE_WIRELESS_ETHERNET)
		return 1;

	/* Unknown device types.  Sort by name only at this point. */
	return strcmp (name_a, name_b);
}

/*
 * nmwa_menu_add_devices
 *
 */
static void nmwa_menu_add_devices (GtkWidget *menu, NMWirelessApplet *applet)
{
	GSList	*element;
	gint n_wireless_interfaces = 0;
	gint n_wired_interfaces = 0;

	g_return_if_fail (menu != NULL);
	g_return_if_fail (applet != NULL);

	g_mutex_lock (applet->data_mutex);
	if (! applet->devices)
	{
		nmwa_menu_add_text_item (menu, _("No network devices have been found"));
		g_mutex_unlock (applet->data_mutex);
		return;
	}

	applet->devices = g_slist_sort (applet->devices, sort_networks_function);

	for (element = applet->devices; element; element = element->next)
	{
		NetworkDevice *dev = (NetworkDevice *)(element->data);

		g_assert (dev);

		switch (dev->type)
		{
		case DEVICE_TYPE_WIRELESS_ETHERNET:
			n_wireless_interfaces++;
			break;
		case DEVICE_TYPE_WIRED_ETHERNET:
			n_wired_interfaces++;
			break;
		default:
			break;
		}
	}

	/* Add all devices in our device list to the menu */
	for (element = applet->devices; element; element = element->next)
	{
		NetworkDevice *dev = (NetworkDevice *)(element->data);

		if (dev && ((dev->type == DEVICE_TYPE_WIRED_ETHERNET) || (dev->type == DEVICE_TYPE_WIRELESS_ETHERNET)))
		{
			gboolean current = (dev == applet->active_device);
			gint n_devices = 0;

			if (dev->type == DEVICE_TYPE_WIRED_ETHERNET)
				n_devices = n_wired_interfaces;
			else if (dev->type == DEVICE_TYPE_WIRELESS_ETHERNET)
				n_devices = n_wireless_interfaces;

			nmwa_menu_add_device_item (menu, dev, current, n_devices, applet);
			nmwa_menu_device_add_networks (menu, dev, applet);
		}
	}

	if (n_wireless_interfaces > 0)
	{
		/* Add the 'Select a custom esssid entry */
		nmwa_menu_add_separator_item (menu);
		nmwa_menu_add_custom_essid_item (menu, applet);
	}

	g_mutex_unlock (applet->data_mutex);
}


/*
 * nmwa_menu_item_data_free
 *
 * Frees the "network" data tag on a menu item we've created
 *
 */
static void nmwa_menu_item_data_free (GtkWidget *menu_item, gpointer data)
{
	char	*tag;
	GtkWidget *menu;

	g_return_if_fail (menu_item != NULL);

	menu = GTK_WIDGET(data);

	if ((tag = g_object_get_data (G_OBJECT (menu_item), "network")))
	{
		g_object_set_data (G_OBJECT (menu_item), "network", NULL);
		g_free (tag);
	}

	if ((tag = g_object_get_data (G_OBJECT (menu_item), "nm_device")))
	{
		g_object_set_data (G_OBJECT (menu_item), "nm_device", NULL);
		g_free (tag);
	}

	gtk_container_remove(GTK_CONTAINER(menu), menu_item);
}


/*
 * nmwa_dispose_menu_items
 *
 * Destroy the menu and each of its items data tags
 *
 */
static void nmwa_dispose_menu_items (NMWirelessApplet *applet)
{
	g_return_if_fail (applet != NULL);

	/* Free the "network" data on each menu item */
	gtk_container_foreach (GTK_CONTAINER (applet->menu), nmwa_menu_item_data_free, applet->menu);
}


/*
 * nmwa_populate_menu
 *
 * Set up our networks menu from scratch
 *
 */
static GtkWidget * nmwa_populate_menu (NMWirelessApplet *applet)
{
	GtkWidget		 *menu = applet->menu;

	g_return_val_if_fail (applet != NULL, NULL);

	if (applet->applet_state == APPLET_STATE_NO_NM)
	{
		nmwa_menu_add_text_item (menu, _("NetworkManager is not running..."));
		return NULL;
	}

	nmwa_menu_add_devices (menu, applet);

	return (menu);
}

/*
 * mnwa_setup_widgets
 *
 * Intialize the applet's widgets and packing, create the initial
 * menu of networks.
 *
 */
static void nmwa_setup_widgets (NMWirelessApplet *applet)
{
	GtkWidget      *menu_bar;

	/* construct pixmap widget */
	applet->pixmap = gtk_image_new ();
	menu_bar = gtk_menu_bar_new ();
	applet->toplevel_menu = gtk_menu_item_new();
	gtk_widget_set_name (applet->toplevel_menu, "ToplevelMenu");
	gtk_container_set_border_width (GTK_CONTAINER (applet->toplevel_menu), 0);
	gtk_container_add (GTK_CONTAINER(applet->toplevel_menu), applet->pixmap);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), applet->toplevel_menu);
	g_signal_connect(applet->toplevel_menu, "activate", G_CALLBACK(nmwa_toplevel_menu_activate), applet);

	applet->menu = gtk_menu_new();
	gtk_menu_item_set_submenu (GTK_MENU_ITEM(applet->toplevel_menu), applet->menu);
	g_signal_connect (menu_bar, "button_press_event", G_CALLBACK (do_not_eat_button_press), NULL);

	applet->encryption_size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

	gtk_container_add (GTK_CONTAINER (applet), menu_bar);

	gtk_widget_show_all (GTK_WIDGET (applet));
}


static gboolean do_not_eat_button_press (GtkWidget *widget, GdkEventButton *event)
{
	/* Don't worry about this for now
	   We can use it if we need a contectual menu

	if (event->button != 1)
		g_signal_stop_emission_by_name (widget, "button_press_event");

	if (event->button == 3) {
		g_message ("3nd button pressed");
		return (TRUE);
	}
	*/

	return (FALSE);
}

/*
 * nmwa_get_instance
 *
 * Create the initial instance of our wireless applet
 *
 */
static GtkWidget * nmwa_get_instance (NMWirelessApplet *applet)
{
	GError	*error = NULL;

	gtk_widget_hide(GTK_WIDGET(applet));

	applet->gconf_client = gconf_client_get_default ();
	if (!applet->gconf_client)
		return (NULL);

	applet->ui_resources = glade_xml_new(glade_file, NULL, NULL);
	if (!applet->ui_resources)
	{
		show_warning_dialog (TRUE, _("The NetworkManager Applet could not find some required resources (the glade file was not found)."));
		g_object_unref (G_OBJECT (applet->gconf_client));
		return (NULL);
	}

	applet->applet_state = APPLET_STATE_NO_NM;
	applet->devices = NULL;
	applet->active_device = NULL;

	/* Start our dbus thread */
	if (!(applet->data_mutex = g_mutex_new ()))
	{
		g_object_unref (G_OBJECT (applet->gconf_client));
		/* FIXME: free glade file */
		return (NULL);
	}
	if (!(applet->dbus_thread = g_thread_create (nmwa_dbus_worker, applet, FALSE, &error)))
	{
		g_mutex_free (applet->data_mutex);
		g_object_unref (G_OBJECT (applet->gconf_client));
		/* FIXME: free glade file */
		return (NULL);
	}

	/* Load pixmaps and create applet widgets */
	nmwa_setup_widgets (applet);

	g_signal_connect (applet,"destroy", G_CALLBACK (nmwa_destroy),NULL);

#ifndef BUILD_NOTIFICATION_ICON
	panel_applet_setup_menu_from_file (PANEL_APPLET (applet), NULL, "NMWirelessApplet.xml", NULL,
						nmwa_context_menu_verbs, applet);
#endif



	/* Start redraw timeout */
	nmwa_start_redraw_timeout (applet);

	return (GTK_WIDGET (applet));
}

static gboolean nmwa_fill (NMWirelessApplet *applet)
{
	gnome_window_icon_set_default_from_file (ICONDIR"/NMWirelessApplet/wireless-applet.png");

	glade_gnome_init ();
	glade_file = gnome_program_locate_file (NULL, GNOME_FILE_DOMAIN_DATADIR,
		 "NMWirelessApplet/wireless-applet.glade", FALSE, NULL);
	if (!glade_file)
	{
		show_warning_dialog (TRUE, _("The NetworkManager Applet could not find some required resources (the glade file was not found)."));
		return (FALSE);
	}

	gtk_widget_show (nmwa_get_instance (applet));
	return (TRUE);
}

static void
setup_stock (void)
{
	GtkIconFactory *ifactory;
	GtkIconSet *iset;
	GtkIconSource *isource;
	static gboolean initted = FALSE;

	if (initted)
		return;

	ifactory = gtk_icon_factory_new ();
	iset = gtk_icon_set_new ();
	isource = gtk_icon_source_new ();

	/* we use the lockscreen icon to get a key */
	gtk_icon_source_set_icon_name (isource, "gnome-lockscreen");
	gtk_icon_set_add_source (iset, isource);
	gtk_icon_factory_add (ifactory, "gnome-lockscreen", iset);
	gtk_icon_factory_add_default (ifactory);

	initted = TRUE;
}

static void
nmwa_icons_free (NMWirelessApplet *applet)
{
	gint i;

        g_object_unref (applet->no_nm_icon);
        g_object_unref (applet->wired_icon);
	for (i = 0; i < NUM_WIRED_CONNECTING_FRAMES; i++)
		g_object_unref (applet->wired_connecting_icons[i]);
        g_object_unref (applet->wireless_00_icon);
        g_object_unref (applet->wireless_25_icon);
        g_object_unref (applet->wireless_50_icon);
        g_object_unref (applet->wireless_75_icon);
        g_object_unref (applet->wireless_100_icon);
	for (i = 0; i < NUM_WIRELESS_CONNECTING_FRAMES; i++)
		g_object_unref (applet->wireless_connecting_icons[i]);
	for (i = 0; i < NUM_WIRELESS_SCANNING_FRAMES; i++)
		g_object_unref (applet->wireless_scanning_icons[i]);
}

static void
nmwa_icons_load_from_disk (NMWirelessApplet *applet,
			   GtkIconTheme     *icon_theme)
{
	gint icon_size;

	/* Assume icon is square */
	icon_size = 22;

        applet->no_nm_icon = gtk_icon_theme_load_icon (icon_theme, "nm-device-broken", icon_size, 0, NULL);
        applet->wired_icon = gtk_icon_theme_load_icon (icon_theme, "nm-device-wired", icon_size, 0, NULL);
        applet->wired_connecting_icons[0] = gtk_icon_theme_load_icon (icon_theme, "nm-connecting00", icon_size, 0, NULL);
        applet->wired_connecting_icons[1] = gtk_icon_theme_load_icon (icon_theme, "nm-connecting01", icon_size, 0, NULL);
        applet->wired_connecting_icons[2] = gtk_icon_theme_load_icon (icon_theme, "nm-connecting02", icon_size, 0, NULL);
        applet->wired_connecting_icons[3] = gtk_icon_theme_load_icon (icon_theme, "nm-connecting03", icon_size, 0, NULL);
        applet->wireless_00_icon = gtk_icon_theme_load_icon (icon_theme, "nm-signal-00", icon_size, 0, NULL);
        applet->wireless_25_icon = gtk_icon_theme_load_icon (icon_theme, "nm-signal-25", icon_size, 0, NULL);
        applet->wireless_50_icon = gtk_icon_theme_load_icon (icon_theme, "nm-signal-50", icon_size, 0, NULL);
        applet->wireless_75_icon = gtk_icon_theme_load_icon (icon_theme, "nm-signal-75", icon_size, 0, NULL);
        applet->wireless_100_icon = gtk_icon_theme_load_icon (icon_theme, "nm-signal-100", icon_size, 0, NULL);
        applet->wireless_connecting_icons[0] = gtk_icon_theme_load_icon (icon_theme, "nm-connecting00", icon_size, 0, NULL);
        applet->wireless_connecting_icons[1] = gtk_icon_theme_load_icon (icon_theme, "nm-connecting01", icon_size, 0, NULL);
        applet->wireless_connecting_icons[2] = gtk_icon_theme_load_icon (icon_theme, "nm-connecting02", icon_size, 0, NULL);
        applet->wireless_connecting_icons[3] = gtk_icon_theme_load_icon (icon_theme, "nm-connecting03", icon_size, 0, NULL);
	applet->wireless_scanning_icons[0] = gtk_icon_theme_load_icon (icon_theme, "nm-detect00", icon_size, 0, NULL);
	applet->wireless_scanning_icons[1] = gtk_icon_theme_load_icon (icon_theme, "nm-detect01", icon_size, 0, NULL);
	applet->wireless_scanning_icons[2] = gtk_icon_theme_load_icon (icon_theme, "nm-detect02", icon_size, 0, NULL);
	applet->wireless_scanning_icons[3] = gtk_icon_theme_load_icon (icon_theme, "nm-detect03", icon_size, 0, NULL);
	applet->wireless_scanning_icons[4] = gtk_icon_theme_load_icon (icon_theme, "nm-detect04", icon_size, 0, NULL);
	applet->wireless_scanning_icons[5] = gtk_icon_theme_load_icon (icon_theme, "nm-detect05", icon_size, 0, NULL);
	applet->wireless_scanning_icons[6] = gtk_icon_theme_load_icon (icon_theme, "nm-detect06", icon_size, 0, NULL);
	applet->wireless_scanning_icons[7] = gtk_icon_theme_load_icon (icon_theme, "nm-detect07", icon_size, 0, NULL);
}

static void
nmwa_icon_theme_changed (GtkIconTheme     *icon_theme,
			 NMWirelessApplet *applet)
{
	nmwa_icons_free (applet);
	nmwa_icons_load_from_disk (applet, icon_theme);
	/* FIXME: force redraw */
}
const gchar *style = " \
style \"MenuBar\" \
{ \
  GtkMenuBar::shadow_type = GTK_SHADOW_NONE \
  GtkMenuBar::internal-padding = 0 \
} \
style \"MenuItem\" \
{ \
  xthickness=0 \
  ythickness=0 \
} \
class \"GtkMenuBar\" style \"MenuBar\"\
widget \"*ToplevelMenu*\" style \"MenuItem\"\
";

static void 
nmwa_icons_init (NMWirelessApplet *applet)
{
	GtkIconTheme *icon_theme;

	/* FIXME: Do we need to worry about other screens? */
	gtk_rc_parse_string (style);

	icon_theme = gtk_icon_theme_get_default ();
	nmwa_icons_load_from_disk (applet, icon_theme);
	g_signal_connect (icon_theme, "changed", G_CALLBACK (nmwa_icon_theme_changed), applet);
}


NMWirelessApplet *
nmwa_new ()
{
	return g_object_new (NM_TYPE_WIRELESS_APPLET, "title", "NetworkManager", NULL);
}

