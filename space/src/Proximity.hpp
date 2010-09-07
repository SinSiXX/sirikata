/*  Sirikata
 *  Proximity.hpp
 *
 *  Copyright (c) 2009, Ewen Cheslack-Postava
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are
 *  met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name of Sirikata nor the names of its contributors may
 *    be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _SIRIKATA_PROXIMITY_HPP_
#define _SIRIKATA_PROXIMITY_HPP_

#include <sirikata/space/ProxSimulationTraits.hpp>
#include "CBRLocationServiceCache.hpp"
#include <sirikata/space/CoordinateSegmentation.hpp>
#include "MigrationDataClient.hpp"
#include <prox/QueryHandler.hpp>
#include <prox/LocationUpdateListener.hpp>
#include <prox/AggregateListener.hpp>
#include <sirikata/core/service/PollingService.hpp>

#include <sirikata/core/network/SSTImpl.hpp>
#include <sirikata/core/queue/ThreadSafeQueue.hpp>

#include <sirikata/space/PintoServerQuerier.hpp>


namespace Sirikata {

class LocationService;
class ProximityInputEvent;
class ProximityOutputEvent;

class Proximity :
        Prox::QueryEventListener<ObjectProxSimulationTraits>,
        LocationServiceListener,
        CoordinateSegmentation::Listener,
        MessageRecipient,
        MigrationDataClient,
        public PollingService,
        PintoServerQuerierListener,
        Prox::AggregateListener<ObjectProxSimulationTraits>
{
private:
    typedef Prox::QueryHandler<ObjectProxSimulationTraits> ProxQueryHandler;
public:
    // MAIN Thread: All public interface is expected to be called only from the main thread.
    typedef Prox::Query<ObjectProxSimulationTraits> Query;
    typedef Prox::QueryEvent<ObjectProxSimulationTraits> QueryEvent;

    Proximity(SpaceContext* ctx, LocationService* locservice);
    ~Proximity();

    // Initialize prox.  Must be called after everything else (specifically message router) is set up since it
    // needs to send messages.
    void initialize(CoordinateSegmentation* cseg);

    // Shutdown the proximity thread.
    void shutdown();

    // Objects
    void addQuery(UUID obj, SolidAngle sa);
    void removeQuery(UUID obj);

    // QueryEventListener Interface
    void queryHasEvents(Query* query);

    // LocationServiceListener Interface
    virtual void localObjectAdded(const UUID& uuid, const TimedMotionVector3f& loc, const TimedMotionQuaternion& orient, const BoundingSphere3f& bounds, const String& mesh);
    virtual void localObjectRemoved(const UUID& uuid);
    virtual void localLocationUpdated(const UUID& uuid, const TimedMotionVector3f& newval);
    virtual void localOrientationUpdated(const UUID& uuid, const TimedMotionQuaternion& newval);
    virtual void localBoundsUpdated(const UUID& uuid, const BoundingSphere3f& newval);
    virtual void localMeshUpdated(const UUID& uuid, const String& newval);
    virtual void replicaObjectAdded(const UUID& uuid, const TimedMotionVector3f& loc, const TimedMotionQuaternion& orient, const BoundingSphere3f& bounds, const String& mesh);
    virtual void replicaObjectRemoved(const UUID& uuid);
    virtual void replicaLocationUpdated(const UUID& uuid, const TimedMotionVector3f& newval);
    virtual void replicaOrientationUpdated(const UUID& uuid, const TimedMotionQuaternion& newval);
    virtual void replicaBoundsUpdated(const UUID& uuid, const BoundingSphere3f& newval);
    virtual void replicaMeshUpdated(const UUID& uuid, const String& newval);

    // CoordinateSegmentation::Listener Interface
    virtual void updatedSegmentation(CoordinateSegmentation* cseg, const std::vector<SegmentationInfo>& new_seg);

    // MessageRecipient Interface
    virtual void receiveMessage(Message* msg);

    // MigrationDataClient Interface
    virtual std::string migrationClientTag();
    virtual std::string generateMigrationData(const UUID& obj, ServerID source_server, ServerID dest_server);
    virtual void receiveMigrationData(const UUID& obj, ServerID source_server, ServerID dest_server, const std::string& data);

    // PintoServerQuerierListener Interface
    virtual void addRelevantServer(ServerID sid);
    virtual void removeRelevantServer(ServerID sid);

    // AggregateListener Interface
    virtual void aggregateCreated(ProxQueryHandler* handler, const UUID& objid);
    virtual void aggregateChildAdded(ProxQueryHandler* handler, const UUID& objid, const UUID& child, const BoundingSphere3f& bnds);
    virtual void aggregateChildRemoved(ProxQueryHandler* handler, const UUID& objid, const UUID& child, const BoundingSphere3f& bnds);
    virtual void aggregateBoundsUpdated(ProxQueryHandler* handler, const UUID& objid, const BoundingSphere3f& bnds);
    virtual void aggregateDestroyed(ProxQueryHandler* handler, const UUID& objid);
    virtual void aggregateObserved(ProxQueryHandler* handler, const UUID& objid, uint32 nobservers);
private:

    // MAIN Thread: These are utility methods which should only be called from the main thread.

    // Update queries based on current state.
    void poll();

    // Server queries requests, generated by receiving messages
    void updateQuery(ServerID sid, const TimedMotionVector3f& loc, const BoundingSphere3f& bounds, const SolidAngle& sa);
    void removeQuery(ServerID sid);

    // Object queries
    void updateQuery(UUID obj, const TimedMotionVector3f& loc, const BoundingSphere3f& bounds, SolidAngle sa);

    // Object sizes
    void updateObjectSize(const UUID& obj, float rad);
    void removeObjectSize(const UUID& obj);

    // Setup all known servers for a server query update
    void addAllServersForUpdate();

    // Send a query add/update request to all the other servers
    void sendQueryRequests();


    // Handle various events in the main thread that are triggered in the prox thread
    void handleAddObjectLocSubscription(const UUID& subscriber, const UUID& observed);
    void handleRemoveObjectLocSubscription(const UUID& subscriber, const UUID& observed);
    void handleRemoveAllObjectLocSubscription(const UUID& subscriber);
    void handleAddServerLocSubscription(const ServerID& subscriber, const UUID& observed);
    void handleRemoveServerLocSubscription(const ServerID& subscriber, const UUID& observed);
    void handleRemoveAllServerLocSubscription(const ServerID& subscriber);


    // PROX Thread: These are utility methods which should only be called from the prox thread.
    // The main loop for the prox processing thread
    void proxThreadMain();
    // Handle various query events from the main thread
    void handleUpdateServerQuery(const ServerID& server, const TimedMotionVector3f& loc, const BoundingSphere3f& bounds, const SolidAngle& angle);
    void handleRemoveServerQuery(const ServerID& server);
    void handleUpdateObjectQuery(const UUID& object, const TimedMotionVector3f& loc, const BoundingSphere3f& bounds, const SolidAngle& angle);
    void handleRemoveObjectQuery(const UUID& object);
    // Generate query events based on results collected from query handlers
    void generateServerQueryEvents();
    void generateObjectQueryEvents();

    typedef std::set<UUID> ObjectSet;
    typedef std::map<ServerID, Query*> ServerQueryMap;
    typedef std::map<UUID, Query*> ObjectQueryMap;


    SpaceContext* mContext;

    PintoServerQuerier* mServerQuerier;

    // MAIN Thread - Should only be accessed in methods used by the main thread

    LocationService* mLocService;
    CoordinateSegmentation* mCSeg;

    Router<Message*>* mProxServerMessageService;

    // Tracks object query angles for quick access in the main thread
    // NOTE: It really sucks that we're duplicating this information
    // but we'd have to provide a safe query map and query angle accessor
    // to avoid this.
    typedef std::map<UUID, SolidAngle> ObjectQueryAngleMap;
    ObjectQueryAngleMap mObjectQueryAngles;

    // Track object sizes and the maximum of all of them.
    typedef std::tr1::unordered_map<UUID, float32, UUID::Hasher> ObjectSizeMap;
    ObjectSizeMap mObjectSizes;
    float32 mMaxObject;

    // This tracks the minimum object query size, which is used
    // as the angle for queries to other servers.
    SolidAngle mMinObjectQueryAngle;

    typedef std::tr1::unordered_set<ServerID> ServerSet;
    boost::mutex mServerSetMutex;
    // This tracks the servers we currently have subscriptions with
    ServerSet mServersQueried;
    // And this indicates whether we need to send new requests
    // out to other servers
    ServerSet mNeedServerQueryUpdate;

    std::deque<Message*> mServerResultsToSend; // server query results waiting to be sent
    std::deque<Sirikata::Protocol::Object::ObjectMessage*> mObjectResultsToSend; // object query results waiting to be sent


    // PROX Thread - Should only be accessed in methods used by the main thread

    void tickQueryHandler(ProxQueryHandler* qh);

    Thread* mProxThread;
    Network::IOService* mProxService;
    Network::IOStrand* mProxStrand;
    Sirikata::AtomicValue<bool> mShutdownProxThread;

    // These track local objects and answer queries from other
    // servers.
    ServerQueryMap mServerQueries;
    CBRLocationServiceCache* mLocalLocCache;
    ProxQueryHandler* mServerQueryHandler;

    // These track all objects being reported to this server and
    // answer queries for objects connected to this server.
    ObjectQueryMap mObjectQueries;
    CBRLocationServiceCache* mGlobalLocCache;
    ProxQueryHandler* mObjectQueryHandler;

    // Threads: Thread-safe data used for exchange between threads
    Sirikata::ThreadSafeQueue<Message*> mServerResults; // server query results that need to be sent
    Sirikata::ThreadSafeQueue<Sirikata::Protocol::Object::ObjectMessage*> mObjectResults; // object query results that need to be sent

}; //class Proximity

} // namespace Sirikata

#endif //_SIRIKATA_PROXIMITY_HPP_
