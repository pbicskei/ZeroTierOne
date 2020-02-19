/*
 * Copyright (c)2013-2020 ZeroTier, Inc.
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file in the project's root directory.
 *
 * Change Date: 2024-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2.0 of the Apache License.
 */
/****/

#ifndef ZT_TOPOLOGY_HPP
#define ZT_TOPOLOGY_HPP

#include <cstring>
#include <vector>
#include <algorithm>
#include <utility>
#include <set>

#include "Constants.hpp"
#include "Address.hpp"
#include "Identity.hpp"
#include "Peer.hpp"
#include "Path.hpp"
#include "Mutex.hpp"
#include "InetAddress.hpp"
#include "Hashtable.hpp"
#include "SharedPtr.hpp"
#include "ScopedPtr.hpp"
#include "H.hpp"

namespace ZeroTier {

class RuntimeEnvironment;

/**
 * Database of network topology
 */
class Topology
{
public:
	Topology(const RuntimeEnvironment *renv,const Identity &myId,void *tPtr);
	~Topology();

	/**
	 * Add peer to database
	 *
	 * This will not replace existing peers. In that case the existing peer
	 * record is returned.
	 *
	 * @param peer Peer to add
	 * @return New or existing peer (should replace 'peer')
	 */
	SharedPtr<Peer> add(void *tPtr,const SharedPtr<Peer> &peer);

	/**
	 * Get a peer from its address
	 *
	 * @param tPtr Thread pointer to be handed through to any callbacks called as a result of this call
	 * @param zta ZeroTier address of peer
	 * @param loadFromCached If false do not load from cache if not in memory (default: true)
	 * @return Peer or NULL if not found
	 */
	ZT_ALWAYS_INLINE SharedPtr<Peer> peer(void *tPtr,const Address &zta,const bool loadFromCached = true)
	{
		{
			RWMutex::RLock l(_peers_l);
			const SharedPtr<Peer> *const ap = _peers.get(zta);
			if (ap)
				return *ap;
		}
		{
			SharedPtr<Peer> p;
			if (loadFromCached) {
				_loadCached(tPtr,zta,p);
				if (p) {
					RWMutex::Lock l(_peers_l);
					SharedPtr<Peer> &hp = _peers[zta];
					if (hp)
						return hp;
					hp = p;
				}
			}
			return p;
		}
	}

	/**
	 * Get a peer by its 384-bit identity public key hash
	 *
	 * @param hash Identity hash
	 * @return Peer or NULL if no peer is currently in memory for this hash (cache is not checked in this case)
	 */
	ZT_ALWAYS_INLINE SharedPtr<Peer> peerByHash(const H<384> &hash)
	{
		RWMutex::RLock _l(_peers_l);
		const SharedPtr<Peer> *const ap = _peersByIdentityHash.get(hash);
		if (ap)
			return *ap;
		return SharedPtr<Peer>();
	}

	/**
	 * Get a peer by its incoming short probe packet payload
	 *
	 * @param probe Short probe payload (in big-endian byte order)
	 * @return Peer or NULL if no peer is currently in memory matching this probe (cache is not checked in this case)
	 */
	ZT_ALWAYS_INLINE SharedPtr<Peer> peerByProbe(const uint64_t probe)
	{
		RWMutex::RLock _l(_peers_l);
		const SharedPtr<Peer> *const ap = _peersByIncomingProbe.get(probe);
		if (ap)
			return *ap;
		return SharedPtr<Peer>();
	}

	/**
	 * Get a Path object for a given local and remote physical address, creating if needed
	 *
	 * @param l Local socket
	 * @param r Remote address
	 * @return Pointer to canonicalized Path object or NULL on error
	 */
	ZT_ALWAYS_INLINE SharedPtr<Path> path(const int64_t l,const InetAddress &r)
	{
		const uint64_t k = _pathHash(l,r);
		{
			RWMutex::RLock lck(_paths_l);
			SharedPtr<Path> *const p = _paths.get(k);
			if (p)
				return *p;
		}
		{
			SharedPtr<Path> p(new Path(l,r));
			RWMutex::Lock lck(_paths_l);
			SharedPtr<Path> &p2 = _paths[k];
			if (p2)
				return p2;
			p2 = p;
			return p;
		}
	}

	/**
	 * @return Current best root server
	 */
	ZT_ALWAYS_INLINE SharedPtr<Peer> root() const
	{
		RWMutex::RLock l(_peers_l);
		if (_rootPeers.empty())
			return SharedPtr<Peer>();
		return _rootPeers.front();
	}

	/**
	 * @param id Identity to check
	 * @return True if this identity corresponds to a root
	 */
	ZT_ALWAYS_INLINE bool isRoot(const Identity &id) const
	{
		RWMutex::RLock l(_peers_l);
		return (_roots.count(id) > 0);
	}

	/**
	 * Apply a function or function object to all peers
	 *
	 * This locks the peer map during execution, so calls to get() etc. during
	 * eachPeer() will deadlock.
	 *
	 * @param f Function to apply
	 * @tparam F Function or function object type
	 */
	template<typename F>
	ZT_ALWAYS_INLINE void eachPeer(F f) const
	{
		RWMutex::RLock l(_peers_l);
		Hashtable< Address,SharedPtr<Peer> >::Iterator i(const_cast<Topology *>(this)->_peers);
		Address *a = nullptr;
		SharedPtr<Peer> *p = nullptr;
		while (i.next(a,p))
			f(*((const SharedPtr<Peer> *)p));
	}

	/**
	 * Apply a function or function object to all peers
	 *
	 * This locks the peer map during execution, so calls to get() etc. during
	 * eachPeer() will deadlock.
	 *
	 * @param f Function to apply
	 * @tparam F Function or function object type
	 */
	template<typename F>
	ZT_ALWAYS_INLINE void eachPeerWithRoot(F f) const
	{
		RWMutex::RLock l(_peers_l);

		std::vector<uintptr_t> rootPeerPtrs;
		rootPeerPtrs.reserve(_rootPeers.size());
		for(std::vector< SharedPtr<Peer> >::const_iterator rp(_rootPeers.begin());rp!=_rootPeers.end();++rp)
			rootPeerPtrs.push_back((uintptr_t)rp->ptr());
		std::sort(rootPeerPtrs.begin(),rootPeerPtrs.end());

		try {
			Hashtable< Address,SharedPtr<Peer> >::Iterator i(const_cast<Topology *>(this)->_peers);
			Address *a = nullptr;
			SharedPtr<Peer> *p = nullptr;
			while (i.next(a,p))
				f(*((const SharedPtr<Peer> *)p),std::binary_search(rootPeerPtrs.begin(),rootPeerPtrs.end(),(uintptr_t)p->ptr()));
		} catch ( ... ) {} // should not throw
	}

	/**
	 * Iterate through all paths in the system
	 *
	 * @tparam F Function to call for each path
	 * @param f
	 */
	template<typename F>
	ZT_ALWAYS_INLINE void eachPath(F f) const
	{
		RWMutex::RLock l(_paths_l);
		Hashtable< uint64_t,SharedPtr<Path> >::Iterator i(const_cast<Topology *>(this)->_paths);
		uint64_t *k = nullptr;
		SharedPtr<Path> *p = nullptr;
		while (i.next(k,p))
			f(*((const SharedPtr<Path> *)p));
	}

	/**
	 * @param allPeers vector to fill with all current peers
	 */
	void getAllPeers(std::vector< SharedPtr<Peer> > &allPeers) const;

	/**
	 * Get info about a path
	 *
	 * The supplied result variables are not modified if no special config info is found.
	 *
	 * @param physicalAddress Physical endpoint address
	 * @param mtu Variable set to MTU
	 * @param trustedPathId Variable set to trusted path ID
	 */
	ZT_ALWAYS_INLINE void getOutboundPathInfo(const InetAddress &physicalAddress,unsigned int &mtu,uint64_t &trustedPathId)
	{
		for(unsigned int i=0,j=_numConfiguredPhysicalPaths;i<j;++i) {
			if (_physicalPathConfig[i].first.containsAddress(physicalAddress)) {
				trustedPathId = _physicalPathConfig[i].second.trustedPathId;
				mtu = _physicalPathConfig[i].second.mtu;
				return;
			}
		}
	}

	/**
	 * Get the outbound trusted path ID for a physical address, or 0 if none
	 *
	 * @param physicalAddress Physical address to which we are sending the packet
	 * @return Trusted path ID or 0 if none (0 is not a valid trusted path ID)
	 */
	ZT_ALWAYS_INLINE uint64_t getOutboundPathTrust(const InetAddress &physicalAddress)
	{
		for(unsigned int i=0,j=_numConfiguredPhysicalPaths;i<j;++i) {
			if (_physicalPathConfig[i].first.containsAddress(physicalAddress))
				return _physicalPathConfig[i].second.trustedPathId;
		}
		return 0;
	}

	/**
	 * Check whether in incoming trusted path marked packet is valid
	 *
	 * @param physicalAddress Originating physical address
	 * @param trustedPathId Trusted path ID from packet (from MAC field)
	 */
	ZT_ALWAYS_INLINE bool shouldInboundPathBeTrusted(const InetAddress &physicalAddress,const uint64_t trustedPathId)
	{
		for(unsigned int i=0,j=_numConfiguredPhysicalPaths;i<j;++i) {
			if ((_physicalPathConfig[i].second.trustedPathId == trustedPathId)&&(_physicalPathConfig[i].first.containsAddress(physicalAddress)))
				return true;
		}
		return false;
	}

	/**
	 * Set or clear physical path configuration (called via Node::setPhysicalPathConfiguration)
	 */
	void setPhysicalPathConfiguration(const struct sockaddr_storage *pathNetwork,const ZT_PhysicalPathConfiguration *pathConfig);

	/**
	 * Add a root server's identity to the root server set
	 *
	 * @param tPtr Thread pointer
	 * @param id Root server identity
	 * @param bootstrap If non-NULL, a bootstrap address to attempt to find this root
	 */
	void addRoot(void *tPtr,const Identity &id,const InetAddress &bootstrap);

	/**
	 * Remove a root server's identity from the root server set
	 *
	 * @param id Root server identity
	 * @return True if root found and removed, false if not found
	 */
	bool removeRoot(const Identity &id);

	/**
	 * Sort roots in asecnding order of apparent latency
	 *
	 * @param now Current time
	 */
	void rankRoots(int64_t now);

	/**
	 * Do periodic tasks such as database cleanup
	 */
	void doPeriodicTasks(void *tPtr,int64_t now);

	/**
	 * Save all currently known peers to data store
	 */
	void saveAll(void *tPtr);

private:
	void _loadCached(void *tPtr,const Address &zta,SharedPtr<Peer> &peer);

	// This is a secure random integer created at startup to salt the calculation of path hash map keys
	static const uint64_t s_pathHashSalt;

	// Get a hash key for looking up paths by their local port and destination address
	ZT_ALWAYS_INLINE uint64_t _pathHash(int64_t l,const InetAddress &r) const
	{
		if (r.ss_family == AF_INET) {
			return Utils::hash64(s_pathHashSalt ^ (uint64_t)(reinterpret_cast<const struct sockaddr_in *>(&r)->sin_addr.s_addr)) + (uint64_t)Utils::ntoh(reinterpret_cast<const struct sockaddr_in *>(&r)->sin_port) + (uint64_t)l;
		} else if (r.ss_family == AF_INET6) {
#ifdef ZT_NO_UNALIGNED_ACCESS
			uint64_t h = s_pathHashSalt;
			for(int i=0;i<16;++i) {
				h += (uint64_t)((reinterpret_cast<const struct sockaddr_in6 *>(&r)->sin6_addr.s6_addr)[i]);
				h += (h << 10U);
				h ^= (h >> 6U);
			}
#else
			uint64_t h = Utils::hash64(s_pathHashSalt ^ (reinterpret_cast<const uint64_t *>(reinterpret_cast<const struct sockaddr_in6 *>(&r)->sin6_addr.s6_addr)[0] + reinterpret_cast<const uint64_t *>(reinterpret_cast<const struct sockaddr_in6 *>(&r)->sin6_addr.s6_addr)[1]));
#endif
			return h + (uint64_t)Utils::ntoh(reinterpret_cast<const struct sockaddr_in6 *>(&r)->sin6_port) + (uint64_t)l;
		} else {
			return Utils::hashString(reinterpret_cast<const void *>(&r),sizeof(InetAddress)) + (uint64_t)l;
		}
	}

	const RuntimeEnvironment *const RR;
	const Identity _myIdentity;

	RWMutex _peers_l;
	RWMutex _paths_l;

	std::pair< InetAddress,ZT_PhysicalPathConfiguration > _physicalPathConfig[ZT_MAX_CONFIGURABLE_PATHS];
	unsigned int _numConfiguredPhysicalPaths;

	Hashtable< Address,SharedPtr<Peer> > _peers;
	Hashtable< uint64_t,SharedPtr<Peer> > _peersByIncomingProbe;
	Hashtable< H<384>,SharedPtr<Peer> > _peersByIdentityHash;
	Hashtable< uint64_t,SharedPtr<Path> > _paths;
	std::set< Identity > _roots; // locked by _peers_l
	std::vector< SharedPtr<Peer> > _rootPeers; // locked by _peers_l
};

} // namespace ZeroTier

#endif
