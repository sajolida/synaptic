/* rpackagelister.cc - package cache and list manipulation
 * 
 * Copyright (c) 2000, 2001 Conectiva S/A 
 *               2002 Michael Vogt <mvo@debian.org>
 * 
 * Author: Alfredo K. Kojima <kojima@conectiva.com.br>
 *         Michael Vogt <mvo@debian.org>
 * 
 * Portions Taken from apt-get
 *   Copyright (C) Jason Gunthorpe
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

#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <map>

#include "i18n.h"

#include "rpackagelister.h"
#include "rpackagecache.h"
#include "rpackagefilter.h"
#include "rconfiguration.h"
#include "raptoptions.h"
#include "rinstallprogress.h"

#include <apt-pkg/error.h>
#include <apt-pkg/algorithms.h>
#include <apt-pkg/pkgrecords.h>
#include <apt-pkg/configuration.h>
#include <apt-pkg/acquire.h>
#include <apt-pkg/acquire-item.h>
#include <apt-pkg/clean.h>

#include <apt-pkg/sourcelist.h>
#include <apt-pkg/pkgsystem.h>
#include <apt-pkg/strutl.h>

#include <algorithm>
#include <cstdio>

using namespace std;

RPackageLister::RPackageLister() :  _records(0), _packages(0), _filter(0)
{
    _cache = new RPackageCache();
    
    _searchData.pattern = NULL;
    _searchData.isRegex = false;
    
    _updating = true;
    
    memset(&_searchData, 0, sizeof(_searchData));
}


static string getServerErrorMessage(string errm)
{
   string msg;
   unsigned int pos = errm.find("server said");
   if (pos != string::npos) {
      msg = string(errm.c_str()+pos+sizeof("server said"));
      if (msg[0] == ' ')
	  msg = msg.c_str()+1;
   }
   return msg;
}


void RPackageLister::notifyChange(RPackage *pkg)
{
    reapplyFilter();

    for (vector<RPackageObserver*>::const_iterator i = _observers.begin();
	 i != _observers.end();
	 i++) {
	(*i)->notifyChange(pkg);
    }
}

void RPackageLister::unregisterObserver(RPackageObserver *observer)
{
    for (vector<RPackageObserver*>::iterator iter = _observers.begin();
	 iter != _observers.end();
	 iter++) {
	if (*iter == observer) {
	    _observers.erase(iter);
	    break;
	}
    }
}


void RPackageLister::registerObserver(RPackageObserver *observer)
{
    _observers.push_back(observer);
}


void RPackageLister::makePresetFilters()
{
    RFilter *filter;
    // create preset filters

    {
	filter = new RFilter();
	filter->preset = true;
    
	filter->setName(_("Search Filter"));

	registerFilter(filter);
    }
    {
	filter = new RFilter();
	filter->preset = true;
    
	filter->status.setStatus((int)RStatusPackageFilter::Installed);
	filter->setName(_("Installed"));
	
	registerFilter(filter);
    }
    {
	filter = new RFilter();
	filter->preset = true;
    
	filter->status.setStatus((int)RStatusPackageFilter::NotInstalled);
	filter->setName(_("Not Installed"));

	registerFilter(filter);
    }
#if 0 //TODO: how to find out about tasks?
    {
	filter = new RFilter();
	filter->preset = true;
    
	filter->pattern.addPattern(RPatternPackageFilter::Name,
				  "^task-.*", false);
	filter->setName(_("Tasks"));

	registerFilter(filter);
    }
#endif
    {
	filter = new RFilter();
	filter->preset = true;
    
	filter->status.setStatus((int)RStatusPackageFilter::Upgradable);
	filter->setName(_("Upgradable"));
	
	registerFilter(filter);
    }
    {
	filter = new RFilter();
	filter->preset = true;

	filter->status.setStatus(RStatusPackageFilter::Broken);
	filter->setName(_("Broken"));
    
	registerFilter(filter);
    }
    {
	filter = new RFilter();
	filter->preset = true;

	filter->status.setStatus(RStatusPackageFilter::MarkInstall
				 |RStatusPackageFilter::MarkRemove
				 |RStatusPackageFilter::Broken);
	filter->setName(_("Programmed Changes"));
	
	registerFilter(filter);
    }
}


void RPackageLister::restoreFilters()
{
  RFilter *filter = NULL;
  Configuration config;

  if (!RReadFilterData(config)) {
    makePresetFilters();
    return;
  }

  const Configuration::Item *top = config.Tree("filter");
  // we have a config file but no usable entries
  if(top == NULL) {
    makePresetFilters();
    return;
  }

  for (top = (top == 0?0:top->Child); top != 0; top = top->Next) {
    filter = new RFilter;
    filter->setName(top->Tag);
    
    string filterkey = "filter::"+filter->getName();
    if (filter->read(config, filterkey)) {
      if (!registerFilter(filter)) {
	// filter name is probably duplicated or something
	delete filter;
      }
      filter = NULL;
    }
  }
  if (filter)
    delete filter;
}


void RPackageLister::storeFilters()
{
    ofstream out;
    
    if (!RFilterDataOutFile(out))
	return;

    for (vector<RFilter*>::const_iterator iter = _filterL.begin();
	 iter != _filterL.end(); iter++) {

	(*iter)->write(out);
    }
    
    out.close();
}


bool RPackageLister::registerFilter(RFilter *filter)
{
  // FIXME: search for duplicated filters
    _filterL.push_back(filter);
    return true;
}


void RPackageLister::unregisterFilter(RFilter *filter)
{
    for (vector<RFilter*>::iterator iter = _filterL.begin();
	 iter != _filterL.end();
	 iter++) {
	if (*iter == filter) {
	    _filterL.erase(iter);
	    break;
	}
    }
}


bool RPackageLister::check()
{   
    if (_cache->deps() == NULL)
	return false;
    // Nothing is broken
    if (_cache->deps()->BrokenCount() != 0) {
	return false;
    }
    
    return true;
}

bool RPackageLister::upgradable()
{
    return _cache!=NULL && _cache->deps()!=NULL;
}

// we have to reread the cache if we using "pin". 
bool RPackageLister::openCache(bool reset)
{
    map<string,RPackage*> *pkgmap;
    static bool firstRun=true;
    string pkgName;

    if (reset) {
	if (!_cache->reset(*_progMeter)) {
	    _progMeter->Done();
	    return false;
	}
    } else {
	if (!_cache->open(*_progMeter)) {
	    _progMeter->Done();
	    return false;
        }
    }
    _progMeter->Done();

    pkgDepCache *deps = _cache->deps();

    // Apply corrections for half-installed packages
    if (pkgApplyStatus(*deps) == false)
	return 	_error->Error(_("Internal error recalculating dependency cache."));
    
    if (_error->PendingError()) {
	return 	_error->Error(_("Internal error recalculating dependency cache."));
    }

    if (_records) {
      // mvo: BUG: this will sometimes segfault. maybe bug in apt?
      //      segfault can be triggered by changing the repositories
      // pkgRecords::~pkgRecords() { delete Files[I] at last Item? }
      //cout << "delete RPackageLister::_records" << endl;
      //delete _records;
    }

    _records = new pkgRecords(*deps);
    if (_error->PendingError()) {
	return 	_error->Error(_("Internal error recalculating dependency cache."));
    }
    
    _count = 0;
    _installedCount = 0;
    pkgmap = new map<string,RPackage*>();    

    if (_packages) {
      for(int i=0;_packages[i] != NULL;i++) {
	delete _packages[i];
      }
      delete [] _packages;
    }

    _packages = new RPackage *[deps->Head().PackageCount];
    memset(_packages, 0, sizeof(*_packages)*deps->Head().PackageCount);

    pkgCache::PkgIterator I = deps->PkgBegin();
    for (; I.end() != true; I++) {
	
	if (I->VersionList==0) {// exclude virtual packages
	    continue;
	}
	// check whether package is installed
	if (!I.CurrentVer().end())
	    _installedCount++;
	
	RPackage *pkg = new RPackage(this, deps, _records, I);
	_packages[_count++] = pkg;
	(*pkgmap)[string(pkg->name())] = pkg;

	// find out about new packages
	pkgName = pkg->name();
	if(firstRun) 
	  allPackages.insert(pkgName);
	else
	  if(allPackages.find(pkgName) == allPackages.end()) {
	    pkg->setNew();
	    _roptions->setPackageNew(pkgName.c_str());
	    allPackages.insert(pkgName);
	  }

	// gather list of sections
	string sec = "";
	if (I.Section()) {
	    sec = I.Section();

	    for (vector<string>::const_iterator iter = _sectionList.begin();
		 iter != _sectionList.end();
		 iter++) {
		if (*iter == sec) {
		    sec = "";
		    break;
		}
	    }
	}

	if (!sec.empty()) 
	    _sectionList.push_back(sec);
    }
    
    for (I = deps->PkgBegin(); I.end() != true; I++) {
	if (I->VersionList==0) {
	    // find the owner of this virtual package and attach it there
	    if (I->ProvidesList == 0)
		continue;
	    string name = string(I.ProvidesList().OwnerPkg().Name());
	    if (pkgmap->find(name) != pkgmap->end()) {
		(*pkgmap)[name]->addVirtualPackage(I);
	    }
	}
    }

    delete pkgmap;

    applyInitialSelection();

    _updating = false;

    if (reset) {
	reapplyFilter();
    } else {   
	// set default filter (no filter)
	setFilter();
    }

    firstRun=false;

    return true;
}


void RPackageLister::applyInitialSelection()
{
  //cout << "RPackageLister::applyInitialSelection()" << endl;

  _roptions->rereadOrphaned();
  
  for (unsigned i = 0; i < _count; i++) {
    if (_roptions->getPackageLock(_packages[i]->name())) {
      _packages[i]->setPinned(true);
    }

    if (_roptions->getPackageNew(_packages[i]->name())) {
      _packages[i]->setNew(true);
    }

    if (_roptions->getPackageOrphaned(_packages[i]->name())) {
      //cout << "orphaned: " << _packages[i]->name() << endl;
      _packages[i]->setOrphaned(true);
    }
  }
}



RPackage *RPackageLister::getElement(pkgCache::PkgIterator &iter)
{
    unsigned i;
    
    for (i = 0; i < _count; i++) {
	if (*_packages[i]->package() == iter)
	    return _packages[i];
    }
    
    return NULL;
}


int RPackageLister::getElementIndex(RPackage *pkg)
{
    unsigned i;
    
    for (i = 0; i < _displayList.size(); i++) {
	if (_displayList[i] == pkg)
	    return i;
    }
    return -1;
}


bool RPackageLister::fixBroken()
{
    if (_cache->deps() == NULL)
	return false;

    if (_cache->deps()->BrokenCount() == 0)
	return true;
    
    pkgProblemResolver Fix(_cache->deps());
    
    Fix.InstallProtect();
    if (Fix.Resolve(true) == false)
	return false;
    
    reapplyFilter();

    return true;
}


bool RPackageLister::upgrade()
{
    if (pkgAllUpgrade(*_cache->deps()) == false) {
	return _error->Error("Internal Error, AllUpgrade broke stuff");
    }
    
    reapplyFilter();
    
    return true;
}


bool RPackageLister::distUpgrade()
{
    if (pkgDistUpgrade(*_cache->deps()) == false)
    {
	cout << "dist upgrade Failed" << endl;
	return false;
    }
    
    reapplyFilter();
    
    return true;
}



void RPackageLister::setFilter(int index)
{
    if (index < 0) {
	_filter = NULL;
    } else {
	_filter = findFilter(index);
    }

    reapplyFilter();
}

void RPackageLister::setFilter(RFilter *filter)
{
  bool found=false;

  for(unsigned int i=0;i<_filterL.size();i++)  {
    if(filter == _filterL[i]) {
      _filter = _filterL[i];
      found=true;
    }
  }
  
  if(!found)
    _filter = NULL;

  reapplyFilter();
}



void RPackageLister::getFilterNames(vector<string> &filters)
{
    filters.erase(filters.begin(), filters.end());
    
    for (vector<RFilter*>::const_iterator iter = _filterL.begin();
	 iter != _filterL.end();
	 iter++) {
	filters.push_back((*iter)->getName());
    }
}


bool RPackageLister::applyFilters(RPackage *package)
{
    if (_filter == NULL)
	return true;
    
    return _filter->apply(package);
}


void RPackageLister::reapplyFilter()
{
    getFilteredPackages(_displayList);

    sortPackagesByName(_displayList);    
}


void RPackageLister::getFilteredPackages(vector<RPackage*> &packages)
{    
    if (_updating)
	return;

    packages.erase(packages.begin(), packages.end());
    
    for (unsigned i = 0; i < _count; i++) {
	if (applyFilters(_packages[i])) {
	    packages.push_back(_packages[i]);
	}
    }
}


static void qsSortByName(vector<RPackage*> &packages,
			 int start, int end)
{
    int i, j;
    RPackage *pivot, *tmp;
    
    i = start;
    j = end;
    pivot = packages[(i+j)/2];
    do {
	while (strcoll(packages[i]->name(), pivot->name()) < 0) i++;
	while (strcoll(pivot->name(), packages[j]->name()) < 0) j--;
	if (i <= j) {
	    tmp = packages[i];
	    packages[i] = packages[j];
	    packages[j] = tmp;
	    i++;
	    j--;
	}
    } while (i <= j);
    
    if (start < j) qsSortByName(packages, start, j);
    if (i < end) qsSortByName(packages, i, end);
}


void RPackageLister::sortPackagesByName(vector<RPackage*> &packages)
{
    if (!packages.empty())
	qsSortByName(packages, 0, packages.size()-1);
}


int RPackageLister::findPackage(const char *pattern)
{
    if (_searchData.isRegex)
	regfree(&_searchData.regex);
    
    if (_searchData.pattern)
	free(_searchData.pattern);
    
    _searchData.pattern = strdup(pattern);
    
    if (!_config->FindB("Synaptic::UseRegexp", false) ||
	regcomp(&_searchData.regex, pattern, REG_EXTENDED|REG_ICASE) != 0) {
	_searchData.isRegex = false;
    } else {
	_searchData.isRegex = true;
    }
    _searchData.last = -1;
    
    return findNextPackage();
}


int RPackageLister::findNextPackage()
{
    if (!_searchData.pattern) {
	if (_searchData.last >= (int)_displayList.size())
	    _searchData.last = -1;
	return ++_searchData.last;
    }
    
    int len = strlen(_searchData.pattern);

    for (unsigned i = _searchData.last+1; i < _displayList.size(); i++) {
	if (_searchData.isRegex) {
	    if (regexec(&_searchData.regex, _displayList[i]->name(),
			0, NULL, 0) == 0) {
		_searchData.last = i;
		return i;
	    }
	} else {
	    if (strncasecmp(_searchData.pattern, _displayList[i]->name(), 
			    len) == 0) {
		_searchData.last = i;
		return i;
	    }
	}
    }
    return -1;
}


void RPackageLister::getStats(int &installed, int &broken, 
			      int &toinstall, int &toremove, 
			      double &sizeChange)
{
    pkgDepCache *deps = _cache->deps();

    if (deps != NULL) {
        sizeChange = deps->UsrSize();

        installed = _installedCount;
        broken = deps->BrokenCount();
        toinstall = deps->InstCount();
        toremove = deps->DelCount();
    } else
	sizeChange = installed = broken = toinstall = toremove = 0;
}


void RPackageLister::getDownloadSummary(int &dlCount, double &dlSize)
{
    dlCount = 0;
    dlSize = _cache->deps()->DebSize();
}


void RPackageLister::getSummary(int &held, int &kept, int &essential,
				int &toInstall, int &toUpgrade,	int &toRemove,
				double &sizeChange)
{
    pkgDepCache *deps = _cache->deps();
    unsigned i;

    held = 0;
    kept = deps->KeepCount();
    essential = 0;
    toInstall = 0;
    toUpgrade = 0;
    toRemove = 0;

    for (i = 0; i < _count; i++) {
	RPackage *pkg = _packages[i];

	switch (pkg->getMarkedStatus()) {
	 case RPackage::MKeep:
	    break;
	 case RPackage::MInstall:
	    toInstall++;
	    break;
	 case RPackage::MUpgrade:
	    toUpgrade++;
	    break;
	 case RPackage::MDowngrade:
	    break;
	 case RPackage::MRemove:
	    if (pkg->isImportant())
		essential++;
	    toRemove++;
	    break;
	 case RPackage::MHeld:
	    held++;
	    break;
	}
    }

    sizeChange = deps->UsrSize();
}




struct bla : public binary_function<RPackage*, RPackage*, bool> {
    bool operator()(RPackage *a, RPackage *b) { 
      return strcmp(a->name(), b->name()) < 0; 
    }
};

void RPackageLister::getDetailedSummary(vector<RPackage*> &held, 
					vector<RPackage*> &kept, 
					vector<RPackage*> &essential,
					vector<RPackage*> &toInstall, 
					vector<RPackage*> &toUpgrade, 
					vector<RPackage*> &toRemove,
					double &sizeChange)
{
  pkgDepCache *deps = _cache->deps();
  unsigned i;
    
  for (i = 0; i < _count; i++) {
    RPackage *pkg = _packages[i];
    
    switch (pkg->getMarkedStatus()) {
    case RPackage::MKeep: {
      if(pkg->getStatus() == RPackage::SInstalledOutdated)
	kept.push_back(pkg);
      break;
    }
    case RPackage::MInstall:
      toInstall.push_back(pkg);
      break;
    case RPackage::MUpgrade:
      toUpgrade.push_back(pkg);
      break;
    case RPackage::MDowngrade:
      break;
    case RPackage::MRemove:
      if (pkg->isImportant())
	essential.push_back(pkg);
      else
	toRemove.push_back(pkg);
      break;
    case RPackage::MHeld:
      held.push_back(pkg);
      break;
    }
  }

  sort(kept.begin(), kept.end(), bla());
  sort(toInstall.begin(), toInstall.end(), bla());
  sort(toUpgrade.begin(), toUpgrade.end(), bla());
  sort(essential.begin(), essential.end(), bla());
  sort(toRemove.begin(), toRemove.end(), bla());
  sort(held.begin(), held.end(), bla());
  
  sizeChange = deps->UsrSize();
}



bool RPackageLister::updateCache(pkgAcquireStatus *status)
{
    assert(_cache->list() != NULL);
    // Get the source list
    //pkgSourceList List;
    //if (_cache->list()->ReadMainList() == false)
    //return false;
    if (_cache->list()->Read(_config->FindFile("Dir::Etc::sourcelist")) == false)
	return false;
    
    // Lock the list directory
    FileFd Lock;
    if (_config->FindB("Debug::NoLocking",false) == false)
    {
	Lock.Fd(GetLock(_config->FindDir("Dir::State::Lists") + "lock"));
	if (_error->PendingError() == true)
	    return _error->Error(_("Unable to lock the list directory"));
    }
    
    _updating = true;
    
    // Create the download object
    pkgAcquire Fetcher(status);
   
    bool Failed = false;

#if HAVE_RPM
   if (_cache->list()->GetReleases(&Fetcher) == false)
      return false;
   Fetcher.Run();
   for (pkgAcquire::ItemIterator I = Fetcher.ItemsBegin(); I != Fetcher.ItemsEnd(); I++)
   {
      if ((*I)->Status == pkgAcquire::Item::StatDone)
	 continue;
      (*I)->Finished();
      Failed = true;
   }
   if (Failed == true)
      _error->Warning(_("Release files for some repositories could not be retrieved or authenticated. Such repositories are being ignored."));
#endif /* HAVE_RPM */

    if (!_cache->list()->GetIndexes(&Fetcher))
       return false;
    
    // Run it
    if (Fetcher.Run() == pkgAcquire::Failed)
	return false;
    
    //bool AuthFailed = false;
    Failed = false;
    for (pkgAcquire::ItemIterator I = Fetcher.ItemsBegin(); 
	 I != Fetcher.ItemsEnd(); 
	 I++)
    {
	if ((*I)->Status == pkgAcquire::Item::StatDone)
	    continue;
	(*I)->Finished();
//	cerr << _("Failed to fetch ") << (*I)->DescURI() << endl;
//	cerr << "  " << (*I)->ErrorText << endl;
	Failed = true;
    }
    
    // Clean out any old list files
    if (_config->FindB("APT::Get::List-Cleanup",true) == true)
    {
	if (Fetcher.Clean(_config->FindDir("Dir::State::lists")) == false ||
	    Fetcher.Clean(_config->FindDir("Dir::State::lists") + "partial/") == false)
	    return false;
    }
    if (Failed == true)
	return _error->Error(_("Some index files failed to download, they have been ignored, or old ones used instead."));

    return true;
}



bool RPackageLister::commitChanges(pkgAcquireStatus *status,
				   RInstallProgress *iprog)
{
    FileFd lock;
    
    _updating = true;
    
    if (!lockPackageCache(lock))
	return false;

    pkgAcquire fetcher(status);

    assert(_cache->list() != NULL);
    // Read the source list
    //pkgSourceList list;
    //if (_cache->list()->ReadMainList() == false) {
    //return _error->Error(_("The list of sources could not be read."));
    //}
    if (_cache->list()->Read(_config->FindFile("Dir::Etc::sourcelist")) == false) {
	return _error->Error(_("The list of sources could not be read."));
    }
    
    _packMan = _system->CreatePM(_cache->deps());
    if (!_packMan->GetArchives(&fetcher, _cache->list(), _records) ||
	_error->PendingError())
	goto gave_wood;

    // ripped from apt-get
    while (1) {
	bool Transient = false;
	
	if (fetcher.Run() == pkgAcquire::Failed)
	    goto gave_wood;
	
        string serverError;

	// Print out errors
	bool Failed = false;
	for (pkgAcquire::ItemIterator I = fetcher.ItemsBegin();
	     I != fetcher.ItemsEnd(); 
	     I++) {
	    if ((*I)->Status == pkgAcquire::Item::StatDone &&
		(*I)->Complete)
		continue;
	    
	    if ((*I)->Status == pkgAcquire::Item::StatIdle)
	    {
		Transient = true;
		continue;
	    }
	    
	    string errm = (*I)->ErrorText;
	    string tmp = "Failed to fetch " + (*I)->DescURI() + "\n"
		"  " + errm;
	   
	    serverError = getServerErrorMessage(errm);

	    _error->Warning(tmp.c_str());
	    Failed = true;
	}

	if (_config->FindB("Synaptic::Download-Only", false)) {
	    _updating = false;
	    return !Failed;
	}

	if (Failed) {
	    string message;
	   
	    if (Transient)
		goto gave_wood;

	    message = _("Some of the packages could not be retrieved from the server(s).\n");
	    if (!serverError.empty())
	       message += "("+serverError+")\n";
	    message += _("Do you want to continue, ignoring these packages?");

	    if (!_userDialog->confirm(_("Warning"), (char*)message.c_str()))
		goto gave_wood;
	}
	// Try to deal with missing package files
	if (Failed == true && _packMan->FixMissing() == false) {
	    _error->Error(_("Unabled to correct missing packages"));
	    goto gave_wood;
	}
       
        // need this so that we first fetch everything and then install (for CDs)
        if (Transient == false || _config->FindB("Acquire::cdrom::copy", false) == false) {
	  
	    _cache->releaseLock();
	    
	    pkgPackageManager::OrderResult Res = iprog->start(_packMan);
	    
	    if (Res == pkgPackageManager::Failed || _error->PendingError())
		goto gave_wood;
	    if (Res == pkgPackageManager::Completed)
		break;
	    
	    _cache->lock();
	}
	
	// Reload the fetcher object and loop again for media swapping
	fetcher.Shutdown();

	if (!_packMan->GetArchives(&fetcher, _cache->list(), _records))
	    goto gave_wood;
    }

    //cout << _("Finished.")<<endl;

    
    // erase downloaded packages
    cleanPackageCache();

    delete _packMan;
    return true;
    
gave_wood:
    delete _packMan;
    
    return false;
}


bool RPackageLister::lockPackageCache(FileFd &lock)
{
    // Lock the archive directory
    
    if (_config->FindB("Debug::NoLocking",false) == false)
    {
	lock.Fd(GetLock(_config->FindDir("Dir::Cache::Archives") + "lock"));
	if (_error->PendingError() == true)
	    return _error->Error(_("Unable to lock the download directory"));
    }
    
    return true;
}


bool RPackageLister::cleanPackageCache()
{
    FileFd lock;
    
    if (_config->FindB("Synaptic::CleanCache", false)) {

	lockPackageCache(lock);
	
	pkgAcquire Fetcher;
	Fetcher.Clean(_config->FindDir("Dir::Cache::archives"));
	Fetcher.Clean(_config->FindDir("Dir::Cache::archives") + "partial/");
    } else if (_config->FindB("Synaptic::AutoCleanCache", false)) {

	lockPackageCache(lock);
   
	pkgArchiveCleaner cleaner;

	bool res;

	res = cleaner.Go(_config->FindDir("Dir::Cache::archives"), 
			 *_cache->deps());
	
	if (!res)
	    return false;
	
	res = cleaner.Go(_config->FindDir("Dir::Cache::archives") + "partial/",
			 *_cache->deps());

	if (!res)
	    return false;

    } else {
	
    }
    
    return true;
}

bool RPackageLister::readSelections(istream &in)
{
    char Buffer[300];
    int CurLine = 0;

    while (in.eof() == false)
    {
        in.getline(Buffer,sizeof(Buffer));
        CurLine++;

        if (in.fail() && !in.eof())
            return _error->Error(_("Line %u too long in selection file."),
                                 CurLine);

        _strtabexpand(Buffer,sizeof(Buffer));
        _strstrip(Buffer);
      
        const char *C = Buffer;
      
        // Comment or blank
        if (C[0] == '#' || C[0] == 0)
	        continue;

        string PkgName;
        if (ParseQuoteWord(C,PkgName) == false)
	        return _error->Error(_("Malformed line %u in selection file"),
			                     CurLine);
        string Action;
        if (ParseQuoteWord(C,Action) == false)
	        return _error->Error(_("Malformed line %u in selection file"),
			                     CurLine);

        for (unsigned i = 0; i < _displayList.size(); i++) {
            if (strcasecmp(PkgName.c_str(), _displayList[i]->name()) == 0) {
                if (Action == "install") {
                    _displayList[i]->setInstall();
                } else if (Action == "uninstall") {
                    _displayList[i]->setRemove();
		}
            }
        }
    }
    if (!check()) {
        fixBroken();
    }
}
