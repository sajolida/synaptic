/* rgzvtinstallprogress.h
 *
 * Copyright (c) 2002 Michael Vogt
 *
 * Author: Michael Vogt <mvo@debian.org>
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


#ifndef _RGZVTNSTALLPROGRESS_H_
#define _RGZVTINSTALLPROGRESS_H_


#include "rgmainwindow.h"
#include "rinstallprogress.h"
#include "rgwindow.h"

#ifdef HAVE_ZVT

class RGZvtInstallProgress : public RInstallProgress, public RGWindow {
  GtkWidget *_term;
  GtkWidget *_statusL;
  GtkWidget *_closeOnF;
  bool updateFinished;
  pkgPackageManager::OrderResult res;

protected:
   virtual void startUpdate();
   virtual void updateInterface();
   virtual void finishUpdate();
   static void stopShell(GtkWidget *self, void* data);

public:
   RGZvtInstallProgress(RGMainWindow *main);
   ~RGZvtInstallProgress() {};

   virtual pkgPackageManager::OrderResult start(pkgPackageManager *pm);
};

#endif /* HAVT_ZVT */

#endif
