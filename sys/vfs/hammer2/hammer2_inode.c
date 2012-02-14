/*
 * Copyright (c) 2011-2012 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
 * by Venkatesh Srinivas <vsrinivas@dragonflybsd.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <sys/cdefs.h>
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/lock.h>
#include <sys/uuid.h>

#include "hammer2.h"

/*
 * Adding a ref to an inode is only legal if the inode already has at least
 * one ref.
 */
void
hammer2_inode_ref(hammer2_inode_t *ip)
{
	hammer2_chain_ref(ip->hmp, &ip->chain);
}

/*
 * Drop an inode reference, freeing the inode when the last reference goes
 * away.
 */
void
hammer2_inode_drop(hammer2_inode_t *ip)
{
	hammer2_chain_drop(ip->hmp, &ip->chain);
}

/*
 * Get the vnode associated with the given inode, allocating the vnode if
 * necessary.
 *
 * Great care must be taken to avoid deadlocks and vnode acquisition/reclaim
 * races.
 *
 * The vnode will be returned exclusively locked and referenced.  The
 * reference on the vnode prevents it from being reclaimed.
 *
 * The inode (ip) must be referenced by the caller and not locked to avoid
 * it getting ripped out from under us or deadlocked.
 */
struct vnode *
hammer2_igetv(hammer2_inode_t *ip, int *errorp)
{
	struct vnode *vp;
	hammer2_mount_t *hmp;

	hmp = ip->hmp;
	*errorp = 0;

	for (;;) {
		/*
		 * Attempt to reuse an existing vnode assignment.  It is
		 * possible to race a reclaim so the vget() may fail.  The
		 * inode must be unlocked during the vget() to avoid a
		 * deadlock against a reclaim.
		 */
		vp = ip->vp;
		if (vp) {
			/*
			 * Lock the inode and check for a reclaim race
			 */
			hammer2_inode_lock_ex(ip);
			if (ip->vp != vp) {
				hammer2_inode_unlock_ex(ip);
				continue;
			}

			/*
			 * Inode must be unlocked during the vget() to avoid
			 * possible deadlocks, vnode is held to prevent
			 * destruction during the vget().  The vget() can
			 * still fail if we lost a reclaim race on the vnode.
			 */
			vhold_interlocked(vp);
			hammer2_inode_unlock_ex(ip);
			if (vget(vp, LK_EXCLUSIVE)) {
				vdrop(vp);
				continue;
			}
			vdrop(vp);
			/* vp still locked and ref from vget */
			*errorp = 0;
			break;
		}

		/*
		 * No vnode exists, allocate a new vnode.  Beware of
		 * allocation races.  This function will return an
		 * exclusively locked and referenced vnode.
		 */
		*errorp = getnewvnode(VT_HAMMER2, H2TOMP(hmp), &vp, 0, 0);
		if (*errorp) {
			vp = NULL;
			break;
		}

		/*
		 * Lock the inode and check for an allocation race.
		 */
		hammer2_inode_lock_ex(ip);
		if (ip->vp != NULL) {
			vp->v_type = VBAD;
			vx_put(vp);
			hammer2_inode_unlock_ex(ip);
			continue;
		}

		kprintf("igetv new\n");
		switch (ip->ip_data.type) {
		case HAMMER2_OBJTYPE_DIRECTORY:
			vp->v_type = VDIR;
			break;
		case HAMMER2_OBJTYPE_REGFILE:
			vp->v_type = VREG;
			vinitvmio(vp, 0, HAMMER2_LBUFSIZE,
				  (int)ip->ip_data.size & HAMMER2_LBUFMASK);
			break;
		/* XXX FIFO */
		default:
			panic("hammer2: unhandled objtype %d",
			      ip->ip_data.type);
			break;
		}

		if (ip == hmp->iroot)
			vsetflags(vp, VROOT);

		vp->v_data = ip;
		ip->vp = vp;
		hammer2_chain_ref(hmp, &ip->chain);	/* vp association */
		hammer2_inode_unlock_ex(ip);
		break;
	}

	/*
	 * Return non-NULL vp and *errorp == 0, or NULL vp and *errorp != 0.
	 */
	kprintf("igetv vp %p refs %d aux %d\n",
		vp, vp->v_sysref.refcnt, vp->v_auxrefs);
	return (vp);
}

/*
 * Create a new inode in the specified directory using the vattr to
 * figure out the type of inode.
 *
 * If no error occurs the new inode is returned in *nipp, otherwise an
 * error is returned and *nipp is set to NULL.
 */
int
hammer2_create_inode(hammer2_mount_t *hmp,
		     struct vattr *vap, struct ucred *cred,
		     hammer2_inode_t *dip,
		     const uint8_t *name, size_t name_len,
		     hammer2_inode_t **nipp)
{
	hammer2_chain_t *chain;
	hammer2_chain_t *parent;
	hammer2_inode_t *nip;
	hammer2_key_t lhc;
	int error;

	lhc = hammer2_dirhash(name, name_len);

	/*
	 * Locate the inode or indirect block to create the new
	 * entry in.  At the same time check for key collisions
	 * and iterate until we don't get one.
	 */
	parent = &dip->chain;
	hammer2_chain_ref(hmp, parent);
	hammer2_chain_lock(hmp, parent);

	error = 0;
	while (error == 0) {
		chain = hammer2_chain_lookup(hmp, &parent, lhc, lhc);
		if (chain == NULL)
			break;
		if ((lhc & HAMMER2_DIRHASH_LOMASK) == HAMMER2_DIRHASH_LOMASK)
			error = ENOSPC;
		hammer2_chain_put(hmp, chain);
		chain = NULL;
		++lhc;
	}
	if (error == 0) {
		chain = hammer2_chain_create(hmp, &dip->chain, lhc, 0,
					     HAMMER2_BREF_TYPE_INODE,
					     HAMMER2_INODE_BYTES);
		if (chain == NULL)
			error = EIO;
	}
	hammer2_chain_put(hmp, parent);

	/*
	 * Handle the error case
	 */
	if (error) {
		KKASSERT(chain == NULL);
		*nipp = NULL;
		return (error);
	}

	/*
	 * Set up the new inode
	 */
	nip = chain->u.ip;
	*nipp = nip;

	nip->ip_data.type = hammer2_get_obj_type(vap->va_type);
	nip->ip_data.inum = hmp->voldata.alloc_tid++;	/* XXX modify/lock */
	nip->ip_data.version = HAMMER2_INODE_VERSION_ONE;
	nip->ip_data.ctime = 0;
	nip->ip_data.mtime = 0;
	nip->ip_data.mode = vap->va_mode;
	nip->ip_data.nlinks = 1;
	/* uid, gid, etc */

	KKASSERT(name_len < HAMMER2_INODE_MAXNAME);
	bcopy(name, nip->ip_data.filename, name_len);
	nip->ip_data.name_key = lhc;
	nip->ip_data.name_len = name_len;

	return (0);
}