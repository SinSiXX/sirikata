/*  Sirikata
 *  CoordinatorSessionManager.hpp
 *
 *  Copyright (c) 2010, Ewen Cheslack-Postava
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

#ifndef _SIRIKATA_LIBOH_COORDINATOR_SESSION_MANAGER_HPP_
#define _SIRIKATA_LIBOH_COORDINATOR_SESSION_MANAGER_HPP_

#include <sirikata/oh/ObjectHostContext.hpp>
#include <sirikata/core/service/Service.hpp>
#include <sirikata/oh/SpaceNodeConnection.hpp>
#include <sirikata/core/odp/SST.hpp>
#include <sirikata/core/util/MotionVector.hpp>
#include <sirikata/core/util/MotionQuaternion.hpp>
#include <sirikata/core/util/SpaceObjectReference.hpp>
#include <sirikata/core/util/Platform.hpp>
#include <sirikata/core/ohdp/DelegateService.hpp>
#include <sirikata/core/sync/TimeSyncClient.hpp>
#include <sirikata/core/network/Address4.hpp>

#include <sirikata/oh/DisconnectCodes.hpp>
#include <sirikata/oh/SpaceNodeSession.hpp>

namespace Sirikata {

class ServerIDMap;

/** CoordinatorSessionManager provides most of the session management functionality for
 * object hosts. It uses internal object host IDs (UUIDs) to track objects
 * requests and open sessions, and also handles migrating connections between
 * space servers.
 *
 *  CoordinatorSessionManager is also an OHDP::Service, allowing communication with
 *  individual space servers (e.g. used internally for time sync). It is also
 *  exposed so other services can communicate directly with space servers.
 */
class SIRIKATA_OH_EXPORT CoordinatorSessionManager
    : public PollingService,
      public OHDP::DelegateService,
      public SpaceNodeSessionManager
{
  public:

    struct ConnectionInfo {
        ServerID server;
    };

    enum ConnectionEvent {
        Connected,
        Disconnected
    };

    // Callback indicating that a connection to the server was made
    // and it is available for sessions
    typedef std::tr1::function<void(const SpaceID&, const ObjectReference&, const ConnectionInfo&)> ConnectedCallback;
    typedef std::tr1::function<void(const SpaceObjectReference&, ConnectionEvent after)> StreamCreatedCallback;
    typedef std::tr1::function<void(const SpaceObjectReference&, Disconnect::Code)> DisconnectedCallback;
    typedef std::tr1::function<void(const Sirikata::Protocol::Object::ObjectMessage&)> ObjectMessageCallback;
    // Notifies the ObjectHost class of a new object connection: void(object, connectedTo)
    typedef std::tr1::function<void(const SpaceObjectReference&,ServerID)> ObjectConnectedCallback;
    // Returns a message to the object host for handling.
    typedef std::tr1::function<void(const SpaceObjectReference&, Sirikata::Protocol::Object::ObjectMessage*)> ObjectMessageHandlerCallback;
    // Notifies the ObjectHost of object connection that was closed, including a
    // reason.
    typedef std::tr1::function<void(const SpaceObjectReference&, Disconnect::Code)> ObjectDisconnectedCallback;
    //Feng:
    typedef std::tr1::function<void(const UUID&, const String&)> ObjectMigrationCallback;
    typedef std::tr1::function<void(const UUID&, const String&, const String&, const String&)> ObjectOHMigrationCallback;

    // SST stream related typedefs
    typedef SST::Stream<SpaceObjectReference> SSTStream;
    typedef SSTStream::Ptr SSTStreamPtr;
    typedef SSTStream::EndpointType SSTEndpoint;
    typedef OHDPSST::Stream OHSSTStream;
    typedef OHSSTStream::Ptr OHSSTStreamPtr;
    typedef OHDPSST::Endpoint OHSSTEndpoint;

    CoordinatorSessionManager(ObjectHostContext* ctx, const SpaceID& space, ServerIDMap* sidmap, ObjectConnectedCallback, ObjectMessageHandlerCallback,
    						  ObjectDisconnectedCallback, ObjectMigrationCallback, ObjectOHMigrationCallback);

    ~CoordinatorSessionManager();

    // NOTE: The public interface is only safe to access from the main strand.

    /** Connect the object to the space with the given starting parameters.
    * \returns true if no other objects on this OH are trying to connect with this ID
    */
    bool connect(const SpaceObjectReference& sporef_objid, const String& init_oh_name);

    /** Disconnect the object from the space. */
    void disconnect(const SpaceObjectReference& id);
    void migrateRequest(const SpaceObjectReference& sporef_objid, const UUID& uuid);

    //Feng
    void updateCoordinator(const SpaceObjectReference& sporef_objid, const UUID& uuid, const String& oh_name);
    void handleEntityMigrationReady(const UUID& entity_id);

    /** Get offset of server time from client time for the given space. Should
     * only be called by objects with an active connection to that space.
     */
    Duration serverTimeOffset() const;
    /** Get offset of client time from server time for the given space. Should
     * only be called by objects with an active connection to that space. This
     * is just a utility, is always -serverTimeOffset(). */
    Duration clientTimeOffset() const;

    // Private version of send that doesn't verify src UUID, allows us to masquerade for session purposes
    // The allow_connecting parameter allows you to use a connection over which the object is still opening
    // a connection.  This is safe since it can only be used by this class (since this is private), so it will
    // only be used to deal with session management.
    // If dest_server is NullServerID, then getConnectedServer is used to determine where to send the packet.
    // This is used to possibly exchange data between the main and IO strands, so it acquires locks.
    //
    // The allow_connecting flag should only be used internally and allows
    // sending packets over a still-connecting session. This is only used to
    // allow this to act as an OHDP::Service while still in the connecting phase
    // (no callback from SpaceNodeConnection yet) so we can build OHDP::SST
    // streams as part of the connection process.
    bool send(const SpaceObjectReference& sporef_objid, const uint16 src_port, const UUID& dest, const uint16 dest_port, const std::string& payload, ServerID dest_server = NullServerID);

    SSTStreamPtr getSpaceStream(const ObjectReference& objectID);

    // Service Implementation
    virtual void start();
    virtual void stop();
    // PollingService Implementation
    virtual void poll();

private:
    // Implementation Note: mIOStrand is a bit misleading. All the "real" IO is isolated to that strand --
    // reads and writes to the actual sockets are handled in mIOStrand. But that is all that is handled
    // there. Since creating/connecting/disconnecting/destroying SpaceNodeConnections is cheap and relatively
    // rare, we keep these in the main strand, allowing us to leave the SpaceNodeConnection map without a lock.
    // Note that the SpaceNodeConnections may themselves be accessed from multiple threads.
    //
    // The data exchange between the strands happens in two places. When sending, it occurs in the connections
    // queue, which is thread safe.  When receiving, it occurs by posting a handler for the parsed message
    // to the main thread.
    //
    // Note that this means the majority of this class is executed in the main strand. Only reading and writing
    // are separated out, which allows us to ensure the network will be serviced as fast as possible, but
    // doesn't help if our limiting factor is the speed at which this input/output can be handled.
    //
    // Note also that this class does *not* handle multithreaded input -- currently all access of public
    // methods should be performed from the main strand.

    struct ConnectingInfo;

    // Schedules received server messages for handling
    void scheduleHandleServerMessages(SpaceNodeConnection* conn);
    void handleServerMessages(SpaceNodeConnection* conn);
    // Starting point for handling of all messages from the server -- either handled as a special case, such as
    // for session management, or dispatched to the object
    void handleServerMessage(ObjectMessage* msg, ServerID sid);

    // Handles session messages received from the server -- connection replies, migration requests, etc.
    void handleSessionMessage(Sirikata::Protocol::Object::ObjectMessage* msg);

    // This gets invoked when the connection really is ready -- after
    // successful response and we have time sync info. It does some
    // additional setup work (sst stream) and then invokes the real callback
    void handleObjectFullyConnected(const SpaceID& space, const ObjectReference& obj, ServerID server, const ConnectingInfo& ci);

    void retryOpenConnection(const SpaceObjectReference& sporef_uuid,ServerID sid);

    // Utility method which keeps trying to resend a message
    void sendRetryingMessage(const SpaceObjectReference& sporef_src, const uint16 src_port, const UUID& dest, const uint16 dest_port, const std::string& payload, ServerID dest_server, Network::IOStrand* strand, const Duration& rate);

    /** SpaceNodeConnection initiation. */

    // Get an existing space connection or initiate a new one at random
    // which can be used for bootstrapping connections
    void getAnySpaceConnection(SpaceNodeConnection::GotSpaceConnectionCallback cb);
    // Get the connection to the specified space node
    void getSpaceConnection(ServerID sid, SpaceNodeConnection::GotSpaceConnectionCallback cb);

    // Set up a space connection to the given server
    void setupSpaceConnection(ServerID server, SpaceNodeConnection::GotSpaceConnectionCallback cb);
    void finishSetupSpaceConnection(ServerID server, Address4 addr);

    // Handle a connection event, i.e. the socket either successfully connected or failed
    void handleSpaceConnection(const Sirikata::Network::Stream::ConnectionStatus status,
                               const std::string&reason,
                               ServerID sid);
    // Handle a session event, i.e. the SST stream conected.
    void handleSpaceSession(ServerID sid, SpaceNodeConnection* conn);

    /** Object session initiation. */

    // Final callback in session initiation -- we have all the info and now just have to return it to the object
    void openConnectionStartSession(const SpaceObjectReference& sporef_uuid, SpaceNodeConnection* conn);

    // Timeout handler for initial session message -- checks if the connection
    // succeeded and, if necessary, retries
    void checkConnectedAndRetry(const SpaceObjectReference& sporef_uuid, ServerID connTo);


    /** Time Sync related utilities **/


    // OHDP::DelegateService dependencies
    OHDP::DelegatePort* createDelegateOHDPPort(OHDP::DelegateService*, const OHDP::Endpoint& ept);
    bool delegateOHDPPortSend(const OHDP::Endpoint& source_ep, const OHDP::Endpoint& dest_ep, MemoryReference payload);

    void timeSyncUpdated();

    OptionSet* mStreamOptions;

    // THREAD SAFE
    // These may be accessed safely by any thread

    ObjectHostContext* mContext;
    SpaceID mSpace;
    
    SpaceObjectReference mSpaceObjectRef;

    Network::IOStrand* mIOStrand;

    ServerIDMap* mServerIDMap;

    TimeProfiler::Stage* mHandleReadProfiler;
    TimeProfiler::Stage* mHandleMessageProfiler;

    Sirikata::SerializationCheck mSerialization;

    // Main strand only

    // Callbacks for parent ObjectHost
    ObjectConnectedCallback mObjectConnectedCallback;
    ObjectMessageHandlerCallback mObjectMessageHandlerCallback;
    ObjectDisconnectedCallback mObjectDisconnectedCallback;
    //Feng:
    ObjectMigrationCallback mObjectMigrationToCallback;
    ObjectOHMigrationCallback mObjectOHMigrationCallback;

    // Only main strand accesses and manipulates the map, although other strand
    // may access the SpaceNodeConnection*'s.
    typedef std::tr1::unordered_map<ServerID, SpaceNodeConnection*> ServerConnectionMap;
    ServerConnectionMap mConnections;
    ServerConnectionMap mConnectingConnections;

    // Info associated with opening connections
    struct ConnectingInfo {
        String name;
    };
    typedef std::tr1::function<void(const SpaceID&, const ObjectReference&, ServerID, const ConnectingInfo& ci)> InternalConnectedCallback;

    // Objects connections, maintains object connections and mapping
    class ObjectConnections {
    public:
        ObjectConnections(CoordinatorSessionManager* _parent);

        // Add the object, completely disconnected, to the index
        void add(
            const SpaceObjectReference& sporef_objid, ConnectingInfo ci,
            InternalConnectedCallback connect_cb
        );

        bool exists(const SpaceObjectReference& sporef_objid);

        // Mark the object as connecting to the given server
        ConnectingInfo& connectingTo(const SpaceObjectReference& obj, ServerID connecting_to);


        WARN_UNUSED
        InternalConnectedCallback& getConnectCallback(const SpaceObjectReference& sporef_objid);

        // Marks as connected and returns the server connected to. do_cb
        // specifies whether the callback should be invoked or deferred
        ServerID handleConnectSuccess(const SpaceObjectReference& sporef_obj, bool do_cb);

        void handleConnectError(const SpaceObjectReference& sporef_objid);

        void handleConnectStream(const SpaceObjectReference& sporef_objid, ConnectionEvent after, bool do_cb);

        void remove(const SpaceObjectReference& obj);

        // Handle a disconnection triggered by the loss of the underlying
        // network connection, i.e. because the TCPSST connection was lost
        // rather than the space server closing an individual session.
        void handleUnderlyingDisconnect(ServerID sid, const String& reason);

        // Handle a graceful disconnection, notifying other objects
        void gracefulDisconnect(const SpaceObjectReference& sporef);

        void disconnectWithCode(const SpaceObjectReference& sporef, const SpaceObjectReference& connectedAs, Disconnect::Code code);
        // Lookup the server the object is connected to.  With allow_connecting, allows using
        // the server currently being connected to, not just one where a session has been
        // established
        ServerID getConnectedServer(const SpaceObjectReference& sporef_obj_id, bool allow_connecting = false);

        ServerID getConnectingToServer(const SpaceObjectReference& sporef_obj_id);

        //UUID getInternalID(const ObjectReference& space_objid) const;

        // We have to defer some callbacks sometimes for time
        // synchronization. This invokes them, allowing the connection process
        // to continue.
        void invokeDeferredCallbacks();

    private:
        CoordinatorSessionManager* parent;

        struct ObjectInfo {
            ObjectInfo();

            ConnectingInfo connectingInfo;

            // Server currently being connected to
            ServerID connectingTo;
            // Server currently connected to
            ServerID connectedTo;

            SpaceObjectReference connectedAs;

            InternalConnectedCallback connectedCB;
            StreamCreatedCallback streamCreatedCB;
            DisconnectedCallback disconnectedCB;
        };
        typedef std::tr1::unordered_map<ServerID, std::vector<SpaceObjectReference> > ObjectServerMap;
        ObjectServerMap mObjectServerMap;
        typedef std::tr1::unordered_map<SpaceObjectReference, ObjectInfo, SpaceObjectReference::Hasher> ObjectInfoMap;
        ObjectInfoMap mObjectInfo;

        // A reverse index allows us to lookup an objects internal ID
        //typedef std::tr1::unordered_map<SpaceObjectReference, UUID, ObjectReference::Hasher> InternalIDMap;
        //InternalIDMap mInternalIDs;

        typedef std::tr1::function<void()> DeferredCallback;
        typedef std::vector<DeferredCallback> DeferredCallbackList;
        DeferredCallbackList mDeferredCallbacks;
    };
    ObjectConnections mObjectConnections;
    friend class ObjectConnections;

    TimeSyncClient* mTimeSyncClient;

    bool mShuttingDown;

    void spaceConnectCallback(int err, SSTStreamPtr s, SpaceObjectReference obj, ConnectionEvent after);
    std::map<ObjectReference, SSTStreamPtr> mObjectToSpaceStreams;

#ifdef PROFILE_OH_PACKET_RTT
    // Track outstanding packets for computing RTTs
    typedef std::tr1::unordered_map<uint64, Time> OutstandingPacketMap;
    OutstandingPacketMap mOutstandingPackets;
    uint8 mClearOutstandingCount;
    // And stats
    Duration mLatencySum;
    uint32 mLatencyCount;
    // And some helpers for reporting
    const String mTimeSeriesOHRTT;
#endif
}; // class CoordinatorSessionManager

} // namespace Sirikata


#endif //_SIRIKATA_LIBOH_COORDINATOR_SESSION_MANAGER_HPP_