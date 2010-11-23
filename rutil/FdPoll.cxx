#include <assert.h>
#include <string.h>

#include "rutil/FdPoll.hxx"
#include "rutil/Logger.hxx"
#include "rutil/BaseException.hxx"

#include <vector>

#ifdef RESIP_POLL_IMPL_EPOLL
#  include <sys/epoll.h>
#endif


using namespace resip;
#define RESIPROCATE_SUBSYSTEM Subsystem::SIP	// What is best?

/*****************************************************************
 *
 * FdPollItemIf and FdPollItemBase impl
 *
 *****************************************************************/

FdPollItemIf::~FdPollItemIf() { }

FdPollItemBase::FdPollItemBase(FdPollGrp *grp, Socket fd, FdPollEventMask mask) :
  mPollGrp(grp), mPollSocket(fd), mPollMask(mask) {
    mPollGrp->addPollItem(this, mPollMask);
}

FdPollItemBase::~FdPollItemBase() {
    mPollGrp->delPollItem(this);
}

/*****************************************************************
 *
 * FdPollGrp
 * 
 * Implementation for some of the base class methods.
 * While some of these are epoll-specific, we can (and do) implement
 * them at this level.
 * For now we use delegation for the impl data structures
 * rather than inhieritance. Long term, not sure which will
 * be cleaner.
 *
 *****************************************************************/

FdPollGrp::FdPollGrp() {
}

FdPollGrp::~FdPollGrp() {
}


void
FdPollGrp::buildFdSet(FdSet& fdset) const {
    int fd = getEPollFd();
    if ( fd!=-1 )
        fdset.setRead(fd);
}

void
FdPollGrp::buildFdSet(fd_set& readfds) const {
    int fd = getEPollFd();
    if ( fd!=-1 )
        FD_SET(fd, &readfds);
}

void
FdPollGrp::processFdSet(FdSet& fdset) {
    int fd = getEPollFd();
    if ( fd!=-1 && fdset.readyToRead(fd) )
	process();
}

void
FdPollGrp::processFdSet(fd_set& readfds) {
    int fd = getEPollFd();
    if ( fd!=-1 && FD_ISSET(fd, &readfds) )
	process();
}


FdPollItemIf*
FdPollGrp::modifyEventMaskByFd(FdPollEventMask mask, int fd) {
    FdPollItemIf* item = getItemByFd(fd);
    // There is some legit reasons why item may be NULL -- not sure where
    if ( item )
	modPollItem(item, mask);
    return item;
}


/*****************************************************************
 *
 * FdPollImplEpoll
 * 
 *****************************************************************/

#ifdef RESIP_POLL_IMPL_EPOLL

namespace resip {



class FdPollImplEpoll : public FdPollGrp {
  public:
    FdPollImplEpoll();
    ~FdPollImplEpoll();

    virtual void		addPollItem(FdPollItemIf *item,
    				  FdPollEventMask newMask);
    virtual void		modPollItem(const FdPollItemIf *item, 
    				  FdPollEventMask newMask);
    virtual void		delPollItem(FdPollItemIf *item);

    virtual void		process();

    /// See baseclass. This is integer fd, not Socket
    virtual int 		getEPollFd() const { return mEPollFd; }
    virtual FdPollItemIf*	getItemByFd(int fd);

  protected:
    void			processItem(FdPollItemIf *item,
    				  FdPollEventMask mask);
    void			killCache(Socket fd);

    std::vector<FdPollItemIf*>	mItems;	// indexed by fd
    int				mEPollFd;	// from epoll_create()

    /*
     * This is temporary cache of poll events. It is a member (and
     * not on stack) for two reasons: (1) simpler memory management,
     * and (2) so delPollItem() can traverse it and clean up.
     */
    std::vector<struct epoll_event> mEvCache;
    int				mEvCacheCur;
    int				mEvCacheLen;
};

};	// namespace

FdPollImplEpoll::FdPollImplEpoll() :
  mEPollFd(-1) {
    int sz = 200;	// ignored
    if ( (mEPollFd = epoll_create(sz)) < 0 ) {
        CritLog(<<"epoll_create() failed: "<<strerror(errno));
        abort();
    }
    mEvCache.resize(sz);
    mEvCacheCur = mEvCacheLen = 0;
}

FdPollImplEpoll::~FdPollImplEpoll() {
    assert( mEvCacheLen == 0 );	// poll not active
    unsigned itemIdx;
    for (itemIdx=0; itemIdx < mItems.size(); itemIdx++) {
	FdPollItemIf *item = mItems[itemIdx];
	if ( item ) {
	    int fd = item->getPollSocket();
	    CritLog(<<"FdPollItem idx="<<itemIdx
	      <<" fd="<<fd
	      <<" not deleted prior to destruction");
        }
    }
    if ( mEPollFd != -1 )
        close(mEPollFd);
}

static inline unsigned short CvtSysToUsrMask(unsigned long sysMask) {
    unsigned usrMask = 0;
    if ( sysMask & EPOLLIN )
        usrMask |= FPEM_Read;
    if ( sysMask & EPOLLOUT )
        usrMask |= FPEM_Write;
    if ( sysMask & EPOLLERR )
        usrMask |= FPEM_Error;
    return usrMask;
}

static inline unsigned long CvtUsrToSysMask(unsigned short usrMask) {
    unsigned long sysMask = 0;
    if ( usrMask & FPEM_Read )
        sysMask |= EPOLLIN;
    if ( usrMask & FPEM_Write )
        sysMask |= EPOLLOUT;
    if ( usrMask & FPEM_Edge )
        sysMask |= EPOLLET;
    return sysMask;
}

FdPollItemIf*
FdPollImplEpoll::getItemByFd(int fd) {
    if ( fd < 0 || fd >= ((int)mItems.size()) )
        return NULL;
    return mItems[fd];
}

void
FdPollImplEpoll::addPollItem(FdPollItemIf *item, FdPollEventMask newMask) {
    int fd = item->getPollSocket();
    assert(fd>=0);
    //DebugLog(<<"adding epoll item fd="<<fd);
    if ( mItems.size() <= (unsigned)fd ) {
	unsigned newsz = fd+1;
	newsz += newsz/3;	// plus 30% margin
	// WATCHOUT: below may trigger re-allocation, invalidating any iters
	// Currently only iterator is destructor, so should be safe
        mItems.resize(newsz);
    }
    FdPollItemIf *olditem = mItems[fd];
    assert( olditem == NULL );	// what is right thing to do?
    mItems[fd] = item;
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));	// make valgrind happy
    ev.events = CvtUsrToSysMask(newMask);
    ev.data.fd = fd;
    if ( epoll_ctl(mEPollFd, EPOLL_CTL_ADD, fd, &ev) < 0 ) {
        CritLog(<<"epoll_ctl(ADD) failed: "<<strerror(errno));
    	abort();
    }
}

void
FdPollImplEpoll::modPollItem(const FdPollItemIf *item, FdPollEventMask newMask) {
    int fd = item->getPollSocket();
    assert(fd>=0 && ((unsigned)fd) < mItems.size());
    assert( mItems[fd] != NULL );

    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));	// make valgrind happy
    ev.events = CvtUsrToSysMask(newMask);
    ev.data.fd = fd;
    if ( epoll_ctl(mEPollFd, EPOLL_CTL_MOD, fd, &ev) < 0 ) {
        CritLog(<<"epoll_ctl(MOD) failed: "<<strerror(errno));
    	abort();
    }
}

void
FdPollImplEpoll::delPollItem(FdPollItemIf *item) {
    int fd = item->getPollSocket();
    //DebugLog(<<"deleting epoll item fd="<<fd);
    assert(fd>=0 && ((unsigned)fd) < mItems.size());
    assert( mItems[fd] != NULL );
    mItems[fd] = NULL;
    if ( epoll_ctl(mEPollFd, EPOLL_CTL_DEL, fd, NULL) < 0 ) {
        CritLog(<<"epoll_ctl(DEL) fd="<<fd<<" failed: "<<strerror(errno));
    	abort();
    }
    killCache(fd);
}


/**
    There is a boundary case:
    1. fdA and fdB are added to epoll
    2. events occur on fdA and fdB
    2. process() reads queue for fdA and fdB into its cache
    3. handler for fdA deletes fdB (closing fd)
    5. handler (same or differnt) opens new fd, gets fd as fdB, and adds 
       it to epoll but under different object
    6. cache processes "old" fdB but binds it to the new (wrong) object

    For read or write events it would be relatively harmless to
    pass these events to the new object (all objects should be prepared
    to get EAGAIN). But passing an error event could incorrectly kill
    the wrong object.

    To prevent this, we walk the cache and kill any events for our fd.
    In theory, the kernel does the same.

    An alternative approach would be use a serial number counter,
    as a lifetime indicator for each fd, and store both a 32-bit serial
    and 32-bit fd into the epoll event in the kernel. We could then
    recognize stale events.
**/
void
FdPollImplEpoll::killCache(int fd) {
    int ne;
    for (ne=mEvCacheCur; ne < mEvCacheLen; ne++) {
    	if ( mEvCache[ne].data.fd == fd ) {
	    mEvCache[ne].data.fd = INVALID_SOCKET;
	}
    }
}

void
FdPollImplEpoll::processItem(FdPollItemIf *item, FdPollEventMask mask) {
    try {
        item->processPollEvent( mask );
    } catch (BaseException& e) {
	// kill it or something?
        ErrLog(<<"Exception thrown for FdPollItem: " << e);
    }
    item = NULL;	// WATCHOUT: item may have been deleted
}

void
FdPollImplEpoll::process() {
    bool maybeMore;
    do {
        int nfds = epoll_wait(mEPollFd, &mEvCache.front(), mEvCache.size(), 0);
	if ( nfds < 0 ) {
            CritLog(<<"epoll_wait() failed: "<<strerror(errno));
	    abort();
	}
	mEvCacheLen = nfds;	// for killCache()
        maybeMore = ( ((unsigned)nfds)==mEvCache.size()) ? 1 : 0;
	int ne;
        for (ne=0; ne < nfds; ne++) {
	    int fd = mEvCache[ne].data.fd;
	    if ( fd == INVALID_SOCKET )
	        continue;	// was killed by killCache()
	    int sysEvtMask = mEvCache[ne].events;
	    assert(fd>=0 && fd < (int)mItems.size());
	    FdPollItemIf *item = mItems[fd];
	    if ( item==NULL ) {
	    	/* this can happen if item was deleted after
		   event was generated in kernel, etc. */
	        continue;
	    }
	    mEvCacheCur = ne;	// for killCache()
	    processItem( item, CvtSysToUsrMask(sysEvtMask) );
	    item = NULL; // WATCHOUT: item may not exist anymore
	}
        mEvCacheLen = 0;
    } while ( maybeMore );
}

#endif // RESIP_POLL_IMPL_EPOLL

/*****************************************************************
 *
 * Factory
 * 
 *****************************************************************/

/*static*/FdPollGrp*
FdPollGrp::create() {
#ifdef RESIP_POLL_IMPL_EPOLL
    return new FdPollImplEpoll();
#else
    assert(0);
    return NULL;
#endif
}

/* ====================================================================
 * The Vovida Software License, Version 1.0 
 * 
 * Copyright (c) 2000-2005 Jacob Butcher
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 
 * 3. The names "VOCAL", "Vovida Open Communication Application Library",
 *    and "Vovida Open Communication Application Library (VOCAL)" must
 *    not be used to endorse or promote products derived from this
 *    software without prior written permission. For written
 *    permission, please contact vocal@vovida.org.
 *
 * 4. Products derived from this software may not be called "VOCAL", nor
 *    may "VOCAL" appear in their name, without prior written
 *    permission of Vovida Networks, Inc.
 * 
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, TITLE AND
 * NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL VOVIDA
 * NETWORKS, INC. OR ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT DAMAGES
 * IN EXCESS OF $1,000, NOR FOR ANY INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 * 
 */
