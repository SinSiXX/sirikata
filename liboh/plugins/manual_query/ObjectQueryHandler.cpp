// Copyright (c) 2012 Sirikata Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE file.

#include "ObjectQueryHandler.hpp"
#include "ManualObjectQueryProcessor.hpp"
#include "Options.hpp"
#include <sirikata/core/options/CommonOptions.hpp>
#include <sirikata/core/network/IOServiceFactory.hpp>

#include <sirikata/core/prox/QueryHandlerFactory.hpp>

#include "Protocol_Prox.pbj.hpp"

#include <sirikata/proxyobject/PresencePropertiesLocUpdate.hpp>

// Property tree for old API for queries
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#define QPLOG(level,msg) SILOG(manual-query-processor,level,msg)

namespace Sirikata {
namespace OH {
namespace Manual {

static SolidAngle NoUpdateSolidAngle = SolidAngle(0.f);
// Note that this is different from sentinals indicating infinite
// results. Instead, this indicates that we shouldn't even request the update,
// leaving it as is, because we don't actually have a new value.
static uint32 NoUpdateMaxResults = ((uint32)INT_MAX)+1;

namespace {

bool parseQueryRequest(const String& query, SolidAngle* qangle_out, uint32* max_results_out) {
    if (query.empty())
        return false;

    using namespace boost::property_tree;
    ptree pt;
    try {
        std::stringstream data_json(query);
        read_json(data_json, pt);
    }
    catch(json_parser::json_parser_error exc) {
        return false;
    }


    if (pt.find("angle") != pt.not_found())
        *qangle_out = SolidAngle( pt.get<float32>("angle") );
    else
        *qangle_out = SolidAngle::Max;

    if (pt.find("max_results") != pt.not_found())
        *max_results_out = pt.get<uint32>("max_results");
    else
        *max_results_out = 0;

    return true;
}

}

ObjectQueryHandler::ObjectQueryHandler(ObjectHostContext* ctx, ManualObjectQueryProcessor* parent, const SpaceID& space, Network::IOStrandPtr prox_strand, OHLocationServiceCachePtr loc_cache)
 : ObjectQueryHandlerBase(ctx, parent, space, prox_strand, loc_cache),
   mObjectQueries(),
   mObjectDistance(false),
   mObjectHandlerPoller(mProxStrand.get(), std::tr1::bind(&ObjectQueryHandler::tickQueryHandler, this, mObjectQueryHandler), Duration::milliseconds((int64)100)),
   mStaticRebuilderPoller(mProxStrand.get(), std::tr1::bind(&ObjectQueryHandler::rebuildHandler, this, OBJECT_CLASS_STATIC), Duration::seconds(3600.f)),
   mDynamicRebuilderPoller(mProxStrand.get(), std::tr1::bind(&ObjectQueryHandler::rebuildHandler, this, OBJECT_CLASS_DYNAMIC), Duration::seconds(3600.f)),
   mObjectResults( std::tr1::bind(&ObjectQueryHandler::handleDeliverEvents, this) )
{
    using std::tr1::placeholders::_1;
    using std::tr1::placeholders::_2;
    using std::tr1::placeholders::_3;
    using std::tr1::placeholders::_4;
    using std::tr1::placeholders::_5;

    // Object Queries
    String object_handler_type = GetOptionValue<String>(OPT_MANUAL_QUERY_HANDLER_TYPE);
    String object_handler_options = GetOptionValue<String>(OPT_MANUAL_QUERY_HANDLER_OPTIONS);
    for(int i = 0; i < NUM_OBJECT_CLASSES; i++) {
        if (i >= mNumQueryHandlers) {
            mObjectQueryHandler[i] = NULL;
            continue;
        }
        mObjectQueryHandler[i] = QueryHandlerFactory<ObjectProxSimulationTraits>(object_handler_type, object_handler_options);
        // No tracking of aggregates -- querying should be using the tree
        // replicated from the server.
        //mObjectQueryHandler[i]->setAggregateListener(this); // *Must* be before handler->initialize
        bool object_static_objects = (mSeparateDynamicObjects && i == OBJECT_CLASS_STATIC);
        mObjectQueryHandler[i]->initialize(
            mLocCache.get(), mLocCache.get(), object_static_objects,
            std::tr1::bind(&ObjectQueryHandler::handlerShouldHandleObject, this, object_static_objects, true, _1, _2, _3, _4, _5)
        );
    }
    if (object_handler_type == "dist" || object_handler_type == "rtreedist") mObjectDistance = true;
}

ObjectQueryHandler::~ObjectQueryHandler() {
    for(int i = 0; i < NUM_OBJECT_CLASSES; i++) {
        delete mObjectQueryHandler[i];
    }
}


// MAIN Thread Methods: The following should only be called from the main thread.

void ObjectQueryHandler::start() {
    mLocCache->addListener(this);

    mObjectHandlerPoller.start();
    mStaticRebuilderPoller.start();
    mDynamicRebuilderPoller.start();
}

void ObjectQueryHandler::stop() {
    mObjectHandlerPoller.stop();
    mStaticRebuilderPoller.stop();
    mDynamicRebuilderPoller.stop();

    mLocCache->removeListener(this);
}

void ObjectQueryHandler::presenceConnected(const ObjectReference& objid) {
}

void ObjectQueryHandler::presenceDisconnected(const ObjectReference& objid) {
    // Prox strand may  have some state to clean up
    mProxStrand->post(
        std::tr1::bind(&ObjectQueryHandler::handleDisconnectedObject, this, objid)
    );
}

void ObjectQueryHandler::addQuery(HostedObjectPtr ho, const SpaceObjectReference& obj, const String& params) {
    updateQuery(ho, obj, params);
}

void ObjectQueryHandler::updateQuery(HostedObjectPtr ho, const SpaceObjectReference& obj, const String& params) {
    SolidAngle sa;
    uint32 max_results;
    if (parseQueryRequest(params, &sa, &max_results))
        updateQuery(ho, obj, sa, max_results);
}

void ObjectQueryHandler::updateQuery(HostedObjectPtr ho, const SpaceObjectReference& obj, SolidAngle sa, uint32 max_results) {
    // We don't use mLocCache here becuase the querier might not be in
    // there yet
    SequencedPresencePropertiesPtr querier_props = ho->presenceRequestedLocation(obj);
    TimedMotionVector3f loc = querier_props->location();
    BoundingSphere3f bounds = querier_props->bounds();
    updateQuery(obj.object(), loc, bounds, sa, max_results);
}

void ObjectQueryHandler::updateQuery(const ObjectReference& obj, const TimedMotionVector3f& loc, const BoundingSphere3f& bounds, SolidAngle sa, uint32 max_results) {
    // Update the prox thread
    mProxStrand->post(
        std::tr1::bind(&ObjectQueryHandler::handleUpdateObjectQuery, this, obj, loc, bounds, sa, max_results)
    );
}

void ObjectQueryHandler::removeQuery(HostedObjectPtr ho, const SpaceObjectReference& obj) {
    // Update the prox thread
    mProxStrand->post(
        std::tr1::bind(&ObjectQueryHandler::handleRemoveObjectQuery, this, obj.object(), true)
    );
}

void ObjectQueryHandler::checkObjectClass(const ObjectReference& objid, const TimedMotionVector3f& newval) {
    mProxStrand->post(
        std::tr1::bind(&ObjectQueryHandler::handleCheckObjectClass, this, objid, newval)
    );
}

int32 ObjectQueryHandler::objectQueries() const {
    return mObjectQueries[OBJECT_CLASS_STATIC].size();
}


void ObjectQueryHandler::handleDeliverEvents() {
    // Get and ship object results
    std::deque<ProximityResultInfo> object_results_copy;
    mObjectResults.swap(object_results_copy);

    while(!object_results_copy.empty()) {
        ProximityResultInfo info = object_results_copy.front();
        object_results_copy.pop_front();

        // Deal with subscriptions
        for(int aidx = 0; aidx < info.results->addition_size(); aidx++) {
            Sirikata::Protocol::Prox::ObjectAddition addition = info.results->addition(aidx);
            ObjectReference viewed(addition.object());
            if (!mSubscribers[viewed]) mSubscribers[viewed] = SubscriberSetPtr(new SubscriberSet());
            if (mSubscribers[viewed]->find(info.querier) == mSubscribers[viewed]->end())
                mSubscribers[viewed]->insert(info.querier);
        }
        for(int ridx = 0; ridx < info.results->removal_size(); ridx++) {
            Sirikata::Protocol::Prox::ObjectRemoval removal = info.results->removal(ridx);
            ObjectReference viewed(removal.object());
            if (mSubscribers.find(viewed) == mSubscribers.end()) continue;
            if (mSubscribers[viewed]->find(info.querier) != mSubscribers[viewed]->end()) {
                mSubscribers[viewed]->erase(info.querier);
                if (mSubscribers[viewed]->empty()) mSubscribers.erase(viewed);
            }
        }

        // And deliver the results
        mParent->deliverProximityResult(SpaceObjectReference(mSpace, info.querier), *(info.results));
        delete info.results;
    }
}

void ObjectQueryHandler::handleNotifySubscribersLocUpdate(const ObjectReference& oref) {
    SubscribersMap::iterator it = mSubscribers.find(oref);
    if (it == mSubscribers.end()) return;
    SubscriberSetPtr subscribers = it->second;

    PresencePropertiesLocUpdate lu( oref, mLocCache->properties(oref) );

    for(SubscriberSet::iterator sub_it = subscribers->begin(); sub_it != subscribers->end(); sub_it++) {
        const ObjectReference& querier = *sub_it;
        mParent->deliverLocationResult(SpaceObjectReference(mSpace, querier), lu);
    }
}



void ObjectQueryHandler::queryHasEvents(Query* query) {
    generateObjectQueryEvents(query);
}




void ObjectQueryHandler::onObjectAdded(const ObjectReference& obj) {
}

void ObjectQueryHandler::onObjectRemoved(const ObjectReference& obj) {
}

void ObjectQueryHandler::onLocationUpdated(const ObjectReference& obj) {
    updateQuery(obj, mLocCache->location(obj), mLocCache->bounds(obj), NoUpdateSolidAngle, NoUpdateMaxResults);
    if (mSeparateDynamicObjects)
        checkObjectClass(obj, mLocCache->location(obj));

    mContext->mainStrand->post(
        std::tr1::bind(&ObjectQueryHandler::handleNotifySubscribersLocUpdate, this, obj)
    );
}

void ObjectQueryHandler::onOrientationUpdated(const ObjectReference& obj) {
    mContext->mainStrand->post(
        std::tr1::bind(&ObjectQueryHandler::handleNotifySubscribersLocUpdate, this, obj)
    );
}

void ObjectQueryHandler::onBoundsUpdated(const ObjectReference& obj) {
    updateQuery(obj, mLocCache->location(obj), mLocCache->bounds(obj), NoUpdateSolidAngle, NoUpdateMaxResults);

    mContext->mainStrand->post(
        std::tr1::bind(&ObjectQueryHandler::handleNotifySubscribersLocUpdate, this, obj)
    );
}

void ObjectQueryHandler::onMeshUpdated(const ObjectReference& obj) {
    mContext->mainStrand->post(
        std::tr1::bind(&ObjectQueryHandler::handleNotifySubscribersLocUpdate, this, obj)
    );
}

void ObjectQueryHandler::onPhysicsUpdated(const ObjectReference& obj) {
    mContext->mainStrand->post(
        std::tr1::bind(&ObjectQueryHandler::handleNotifySubscribersLocUpdate, this, obj)
    );
}


// PROX Thread: Everything after this should only be called from within the prox thread.

void ObjectQueryHandler::tickQueryHandler(ProxQueryHandler* qh[NUM_OBJECT_CLASSES]) {
    Time simT = mContext->simTime();
    for(int i = 0; i < NUM_OBJECT_CLASSES; i++) {
        if (qh[i] != NULL)
            qh[i]->tick(simT);
    }
}

void ObjectQueryHandler::rebuildHandler(ObjectClass objtype) {
    if (mObjectQueryHandler[objtype] != NULL)
        mObjectQueryHandler[objtype]->rebuild();
}

void ObjectQueryHandler::generateObjectQueryEvents(Query* query) {
    typedef std::deque<QueryEvent> QueryEventList;

    assert(mInvertedObjectQueries.find(query) != mInvertedObjectQueries.end());
    ObjectReference query_id = mInvertedObjectQueries[query];

    QueryEventList evts;
    query->popEvents(evts);

    while(!evts.empty()) {
        const QueryEvent& evt = evts.front();
        Sirikata::Protocol::Prox::ProximityUpdate* event_results = new Sirikata::Protocol::Prox::ProximityUpdate();

        for(uint32 aidx = 0; aidx < evt.additions().size(); aidx++) {
            ObjectReference objid = evt.additions()[aidx].id();
            if (mLocCache->tracking(objid)) { // If the cache already lost it, we can't do anything

                mContext->mainStrand->post(
                    std::tr1::bind(&ObjectQueryHandler::handleAddObjectLocSubscription, this, query_id, objid)
                );

                Sirikata::Protocol::Prox::IObjectAddition addition = event_results->add_addition();
                addition.set_object( objid.getAsUUID() );

                uint64 seqNo = mLocCache->properties(objid).maxSeqNo();
                addition.set_seqno (seqNo);


                Sirikata::Protocol::ITimedMotionVector motion = addition.mutable_location();
                TimedMotionVector3f loc = mLocCache->location(objid);
                motion.set_t(loc.updateTime());
                motion.set_position(loc.position());
                motion.set_velocity(loc.velocity());

                TimedMotionQuaternion orient = mLocCache->orientation(objid);
                Sirikata::Protocol::ITimedMotionQuaternion msg_orient = addition.mutable_orientation();
                msg_orient.set_t(orient.updateTime());
                msg_orient.set_position(orient.position());
                msg_orient.set_velocity(orient.velocity());

                addition.set_bounds( mLocCache->bounds(objid) );
                Transfer::URI mesh = mLocCache->mesh(objid);
                if (!mesh.empty())
                    addition.set_mesh(mesh.toString());
                String phy = mLocCache->physics(objid);
                if (phy.size() > 0)
                    addition.set_physics(phy);
            }
        }
        for(uint32 ridx = 0; ridx < evt.removals().size(); ridx++) {
            ObjectReference objid = evt.removals()[ridx].id();
            // Clear out seqno and let main strand remove loc
            // subcription
            mContext->mainStrand->post(
                std::tr1::bind(&ObjectQueryHandler::handleRemoveObjectLocSubscription, this, query_id, objid)
            );

            Sirikata::Protocol::Prox::IObjectRemoval removal = event_results->add_removal();
            removal.set_object( objid.getAsUUID() );
            uint64 seqNo = mLocCache->properties(objid).maxSeqNo();
            removal.set_seqno (seqNo);
            removal.set_type(
                (evt.removals()[ridx].permanent() == QueryEvent::Permanent)
                ? Sirikata::Protocol::Prox::ObjectRemoval::Permanent
                : Sirikata::Protocol::Prox::ObjectRemoval::Transient
            );
        }
        evts.pop_front();

        mObjectResults.push( ProximityResultInfo(query_id, event_results) );
    }
}

void ObjectQueryHandler::handleUpdateObjectQuery(const ObjectReference& object, const TimedMotionVector3f& loc, const BoundingSphere3f& bounds, const SolidAngle& angle, uint32 max_results) {
    BoundingSphere3f region(bounds.center(), 0);
    float ms = bounds.radius();

    QPLOG(detailed,"Update object query from " << object.toString() << ", min angle " << angle.asFloat() << ", max results " << max_results);

    for(int i = 0; i < NUM_OBJECT_CLASSES; i++) {
        if (mObjectQueryHandler[i] == NULL) continue;

        ObjectQueryMap::iterator it = mObjectQueries[i].find(object);

        if (it == mObjectQueries[i].end()) {
            // We only add if we actually have all the necessary info, most importantly a real minimum angle.
            // This is necessary because we get this update for all location updates, even those for objects
            // which don't have subscriptions.
            if (angle != NoUpdateSolidAngle) {
                float32 query_dist = 0.f;//TODO(ewencp) enable getting query
                                         //distance from parameters
                Query* q = mObjectDistance ?
                    mObjectQueryHandler[i]->registerQuery(loc, region, ms, SolidAngle::Min, query_dist) :
                    mObjectQueryHandler[i]->registerQuery(loc, region, ms, angle);
                if (max_results != NoUpdateMaxResults && max_results > 0)
                    q->maxResults(max_results);
                mObjectQueries[i][object] = q;
                mInvertedObjectQueries[q] = object;
                q->setEventListener(this);
            }
        }
        else {
            Query* query = it->second;
            query->position(loc);
            query->region( region );
            query->maxSize( ms );
            if (angle != NoUpdateSolidAngle)
                query->angle(angle);
            if (max_results != NoUpdateMaxResults && max_results > 0)
                query->maxResults(max_results);
        }
    }
}

void ObjectQueryHandler::handleRemoveObjectQuery(const ObjectReference& object, bool notify_main_thread) {
    // Clear out queries
    for(int i = 0; i < NUM_OBJECT_CLASSES; i++) {
        if (mObjectQueryHandler[i] == NULL) continue;

        ObjectQueryMap::iterator it = mObjectQueries[i].find(object);
        if (it == mObjectQueries[i].end()) continue;

        Query* q = it->second;
        mObjectQueries[i].erase(it);
        mInvertedObjectQueries.erase(q);
        delete q; // Note: Deleting query notifies QueryHandler and unsubscribes.
    }

    // Optionally let the main thread know to clear its communication state
    if (notify_main_thread) {
        mContext->mainStrand->post(
            std::tr1::bind(&ObjectQueryHandler::handleRemoveAllObjectLocSubscription, this, object)
        );
    }
}

void ObjectQueryHandler::handleDisconnectedObject(const ObjectReference& object) {
    // Clear out query state if it exists
    handleRemoveObjectQuery(object, false);
}

bool ObjectQueryHandler::handlerShouldHandleObject(bool is_static_handler, bool is_global_handler, const ObjectReference& obj_id, bool is_local, const TimedMotionVector3f& pos, const BoundingSphere3f& region, float maxSize) {
    // We just need to decide whether the query handler should handle
    // the object. We need to consider local vs. replica and static
    // vs. dynamic.  All must 'vote' for handling the object for us to
    // say it should be handled, so as soon as we find a negative
    // response we can return false.

    // First classify by local vs. replica. Only say no on a local
    // handler looking at a replica.
    if (!is_local && !is_global_handler) return false;

    // If we're not doing the static/dynamic split, then this is a non-issue
    if (!mSeparateDynamicObjects) return true;

    // If we are splitting them, check velocity against is_static_handler. The
    // value here as arbitrary, just meant to indicate such small movement that
    // the object is effectively
    bool is_static = velocityIsStatic(pos.velocity());
    if ((is_static && is_static_handler) ||
        (!is_static && !is_static_handler))
        return true;
    else
        return false;
}

void ObjectQueryHandler::handleCheckObjectClassForHandlers(const ObjectReference& objid, bool is_static, ProxQueryHandler* handlers[NUM_OBJECT_CLASSES]) {
    if ( (is_static && handlers[OBJECT_CLASS_STATIC]->containsObject(objid)) ||
        (!is_static && handlers[OBJECT_CLASS_DYNAMIC]->containsObject(objid)) )
        return;

    // Validate that the other handler has the object.
    assert(
        (is_static && handlers[OBJECT_CLASS_DYNAMIC]->containsObject(objid)) ||
        (!is_static && handlers[OBJECT_CLASS_STATIC]->containsObject(objid))
    );

    // If it wasn't in the right place, switch it.
    int swap_out = is_static ? OBJECT_CLASS_DYNAMIC : OBJECT_CLASS_STATIC;
    int swap_in = is_static ? OBJECT_CLASS_STATIC : OBJECT_CLASS_DYNAMIC;
    QPLOG(debug, "Swapping " << objid.toString() << " from " << ObjectClassToString((ObjectClass)swap_out) << " to " << ObjectClassToString((ObjectClass)swap_in));
    handlers[swap_out]->removeObject(objid);
    handlers[swap_in]->addObject(objid);
}

void ObjectQueryHandler::handleCheckObjectClass(const ObjectReference& objid, const TimedMotionVector3f& newval) {
    assert(mSeparateDynamicObjects == true);

    // Basic approach: we need to check if the object has switched between
    // static/dynamic. We need to do this for both the local (object query) and
    // global (server query) handlers.
    bool is_static = velocityIsStatic(newval.velocity());
    handleCheckObjectClassForHandlers(objid, is_static, mObjectQueryHandler);
}

} // namespace Manual
} // namespace OH
} // namespace Sirikata