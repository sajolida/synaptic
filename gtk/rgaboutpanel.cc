/* rgaboutpanel.cc
 *
 * Copyright (c) 2000, 2001 Conectiva S/A
 *               2002 Michael Vogt <mvo@debian.org>
 *
 * Author: Alfredo K. Kojima <kojima@conectiva.com.br>
 *         Michael Vogt <mvo@debian.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */


#include "config.h"
#include "i18n.h"
#include "rgaboutpanel.h"

static void closeWindow(GtkWidget *self, void *data)
{
    RGAboutPanel *about = (RGAboutPanel*)data;
    
    about->hide();
}


RGAboutPanel::RGAboutPanel(RGWindow *parent) 
    : RGWindow(parent, "about", false, true, true)
{
   glade_xml_signal_connect_data(_gladeXML,
				 "on_okbutton_clicked",
				 G_CALLBACK(closeWindow),
				 this); 
    
   setTitle(PACKAGE" version "VERSION);
}
