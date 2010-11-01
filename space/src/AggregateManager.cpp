/*  Sirikata
 *  AggregateManager.cpp
 *
 *  Copyright (c) 2010, Tahir Azim.
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

#include "AggregateManager.hpp"

#include <sirikata/proxyobject/ModelsSystemFactory.hpp>

namespace Sirikata {





AggregateManager::AggregateManager(SpaceContext* ctx, LocationService* loc) :
  mContext(ctx), mLoc(loc), mThreadRunning(true)
{
    mModelsSystem = NULL;
    if (ModelsSystemFactory::getSingleton().hasConstructor("any"))
        mModelsSystem = ModelsSystemFactory::getSingleton().getConstructor("any")("");

    mTransferMediator = &(Transfer::TransferMediator::getSingleton());

    static char x = '1';
    mTransferPool = mTransferMediator->registerClient("SpaceAggregator_"+x);
    x++;

    boost::thread thrd( boost::bind(&AggregateManager::uploadQueueServiceThread, this) );
}

AggregateManager::~AggregateManager() {
    delete mModelsSystem;

  mThreadRunning = false;

  mCondVar.notify_one();
}

void AggregateManager::addAggregate(const UUID& uuid) {
  std::cout << "addAggregate called: uuid=" << uuid.toString()  << "\n";

  boost::mutex::scoped_lock lock(mAggregateObjectsMutex);
  mAggregateObjects[uuid] = std::tr1::shared_ptr<AggregateObject> (new AggregateObject(uuid, UUID::null()));
}

void AggregateManager::removeAggregate(const UUID& uuid) {
  std::cout << "removeAggregate: " << uuid.toString() << "\n";

  boost::mutex::scoped_lock lock(mAggregateObjectsMutex);

  mAggregateObjects.erase(uuid);
}

void AggregateManager::addChild(const UUID& uuid, const UUID& child_uuid) {
  std::vector<UUID>& children = getChildren(uuid);

  if ( std::find(children.begin(), children.end(), child_uuid) == children.end() ) {
    children.push_back(child_uuid);

    boost::mutex::scoped_lock lock(mAggregateObjectsMutex);

    if (mAggregateObjects.find(child_uuid) == mAggregateObjects.end()) {
      mAggregateObjects[child_uuid] = std::tr1::shared_ptr<AggregateObject> (new AggregateObject(child_uuid, uuid));
    }
    else {
      mAggregateObjects[child_uuid]->mParentUUID = uuid;
    }
    lock.unlock();

    //String locationStr  = ( (mLoc->contains(child_uuid)) ? (mLoc->currentPosition(child_uuid).toString()) : " NOT IN LOC ");

    std::cout << "addChild: generateAggregateMesh called: "  << uuid.toString()
              << " CHILD " << child_uuid.toString() << " "   
              //<< locationStr    
              << "\n";
    fflush(stdout);


    generateAggregateMesh(uuid, Duration::seconds(120.0f+rand()%2));
  }
}

void AggregateManager::removeChild(const UUID& uuid, const UUID& child_uuid) {
  std::vector<UUID>& children = getChildren(uuid);

  std::vector<UUID>::iterator it = std::find(children.begin(), children.end(), child_uuid);

  if (it != children.end()) {
    children.erase( it );

    String locationStr  = ( (mLoc->contains(child_uuid)) ? (mLoc->currentPosition(child_uuid).toString()) : " NOT IN LOC ");

    //std::cout << "removeChild: " <<  uuid.toString() << " CHILD " << child_uuid.toString() << " "
    //          <<  locationStr
    //          << " generateAggregateMesh called\n";

    generateAggregateMesh(uuid, Duration::seconds(120.0f+rand() % 2));
  }
}

void AggregateManager::generateAggregateMesh(const UUID& uuid, const Duration& delayFor) {
  if (mModelsSystem == NULL) return;
  boost::mutex::scoped_lock lock(mAggregateObjectsMutex);
  if (mAggregateObjects.find(uuid) == mAggregateObjects.end()) return;
  std::tr1::shared_ptr<AggregateObject> aggObject = mAggregateObjects[uuid];
  lock.unlock();
  aggObject->mLastGenerateTime = Timer::now();

  //std::cout << "Posted generateAggregateMesh for " << uuid.toString() << "\n";

  mContext->mainStrand->post( delayFor, std::tr1::bind(&AggregateManager::generateAggregateMeshAsync, this, uuid, aggObject->mLastGenerateTime)  );
}



Vector3f fixUp(int up, Vector3f v) {
    if (up==3) return Vector3f(v[0],v[2], -v[1]);
    else if (up==2) return v;
    std::cerr << "ERROR: X up? You gotta be frakkin' kiddin'\n";
    assert(false);
}

void AggregateManager::generateAggregateMeshAsync(const UUID uuid, Time postTime) {
  /* Get the aggregate object corresponding to UUID 'uuid'.  */
  boost::mutex::scoped_lock lock(mAggregateObjectsMutex);
  if (mAggregateObjects.find(uuid) == mAggregateObjects.end()) {
    //std::cout << "0\n";
    return;
  }
  std::tr1::shared_ptr<AggregateObject> aggObject = mAggregateObjects[uuid];
  lock.unlock();
  /****/

  

  if (postTime < aggObject->mLastGenerateTime) {
    //std::cout << "1\n";fflush(stdout);
    return;
  }

  std::vector<UUID>& children = aggObject->mChildren;

  if (children.size() < 1) {
    //std::cout << "2\n"; fflush(stdout);
    return;
  }

  for (uint i= 0; i < children.size(); i++) {
    UUID child_uuid = children[i];

    if (!mLoc->contains(child_uuid)) {
      generateAggregateMesh(uuid, Duration::milliseconds(10.0f));
      //std::cout << "4\n";
      return;
    }

    std::string meshName = mLoc->mesh(child_uuid);
    
    MeshdataPtr childMeshPtr = MeshdataPtr();

    boost::mutex::scoped_lock lock(mAggregateObjectsMutex);
    if (mAggregateObjects.find(child_uuid) != mAggregateObjects.end()) {
      childMeshPtr = mAggregateObjects[child_uuid]->mMeshdata;
    }
    lock.unlock();   

    if (meshName == "" && !childMeshPtr) {
      generateAggregateMesh(child_uuid, Duration::microseconds(1.0f));
      
      return;
    }
  }

  if (!mLoc->contains(uuid)) {   
    //std::cout << "3\n"; fflush(stdout); 
    generateAggregateMesh(uuid, Duration::milliseconds(10.0f));
    return;
  }

  std::tr1::shared_ptr<Meshdata> agg_mesh =  std::tr1::shared_ptr<Meshdata>( new Meshdata() );
  BoundingSphere3f bnds = mLoc->bounds(uuid);

  uint   numAddedSubMeshGeometries = 0;
  double totalVertices = 0;
  for (uint i= 0; i < children.size(); i++) {
    UUID child_uuid = children[i];

    Vector3f location = mLoc->currentPosition(child_uuid);
    Quaternion orientation = mLoc->currentOrientation(child_uuid);

    boost::mutex::scoped_lock lock(mAggregateObjectsMutex);
    if ( mAggregateObjects.find(child_uuid) == mAggregateObjects.end()) {
      continue;
    }
    std::tr1::shared_ptr<Meshdata> m = mAggregateObjects[child_uuid]->mMeshdata;    

    if (!m) {
      //request a download or generation of the mesh
      std::string meshName = mLoc->mesh(child_uuid);

      if (meshName != "") {

        boost::mutex::scoped_lock meshStoreLock(mMeshStoreMutex);
        if (mMeshStore.find(meshName) != mMeshStore.end()) {
          mAggregateObjects[child_uuid]->mMeshdata = m = mMeshStore[meshName];
        }
        else {
          std::cout << meshName << " = meshName requesting download\n";
          Transfer::TransferRequestPtr req(
                                       new Transfer::MetadataRequest( Transfer::URI(meshName), 1.0, std::tr1::bind(
                                       &AggregateManager::metadataFinished, this, uuid, child_uuid, meshName,
                                       std::tr1::placeholders::_1, std::tr1::placeholders::_2)));

          mTransferPool->addRequest(req);

          //once requested, wait until the mesh gets downloaded and loaded up;
          return;
        }
      }
    }
    

    lock.unlock();
    
    agg_mesh->up_axis = m->up_axis;
    agg_mesh->lightInstances.insert(agg_mesh->lightInstances.end(),
                                    m->lightInstances.begin(),
                                    m->lightInstances.end() );
    agg_mesh->lights.insert(agg_mesh->lights.end(),
                            m->lights.begin(),
                            m->lights.end());

    for (Meshdata::URIMap::const_iterator tex_it = m->textureMap.begin();
         tex_it != m->textureMap.end(); tex_it++)
      {
        agg_mesh->textureMap[tex_it->first+"-"+child_uuid.toString()] = tex_it->second;
      }


    /** Find scaling factor **/
    BoundingBox3f3f originalMeshBoundingBox = BoundingBox3f3f::null();
    for (uint i = 0; i < m->instances.size(); i++) {
      const GeometryInstance& geomInstance = m->instances[i];
      SubMeshGeometry smg = m->geometry[geomInstance.geometryIndex];

      for (uint j = 0; j < smg.positions.size(); j++) {
        Vector4f jth_vertex_4f =  geomInstance.transform*Vector4f(smg.positions[j].x,
                                                                  smg.positions[j].y,
                                                                  smg.positions[j].z,
                                                                  1.0f);
        Vector3f jth_vertex(jth_vertex_4f.x, jth_vertex_4f.y, jth_vertex_4f.z);
        jth_vertex = fixUp(m->up_axis, jth_vertex);

        if (originalMeshBoundingBox == BoundingBox3f3f::null()) {
          originalMeshBoundingBox = BoundingBox3f3f(jth_vertex, 0);
        }
        else {
           originalMeshBoundingBox.mergeIn(jth_vertex );
        }
      }
    }
    BoundingSphere3f originalMeshBounds = originalMeshBoundingBox.toBoundingSphere();
    BoundingSphere3f scaledMeshBounds = mLoc->bounds(child_uuid);
    double scalingfactor = scaledMeshBounds.radius()/(2.0*originalMeshBounds.radius());

    //std::cout << mLoc->mesh(child_uuid) << " :mLoc->mesh(child_uuid)\n";
    //std::cout << originalMeshBounds << " , scaled = " << scaledMeshBounds << "\n";
    //std::cout << scalingfactor  << " : scalingfactor\n";
    /** End: find scaling factor **/

    uint geometrySize = agg_mesh->geometry.size();

    std::vector<GeometryInstance> instances;
    for (uint i = 0; i < m->instances.size(); i++) {
      GeometryInstance geomInstance = m->instances[i];

      assert (geomInstance.geometryIndex < m->geometry.size());
      SubMeshGeometry smg = m->geometry[geomInstance.geometryIndex];

      geomInstance.geometryIndex =  numAddedSubMeshGeometries;

      smg.aabb = BoundingBox3f3f::null();

      for (uint j = 0; j < smg.positions.size(); j++) {
        Vector4f jth_vertex_4f =  geomInstance.transform*Vector4f(smg.positions[j].x,
                                                                  smg.positions[j].y,
                                                                  smg.positions[j].z,
                                                                  1.0f);
        smg.positions[j] = Vector3f( jth_vertex_4f.x, jth_vertex_4f.y, jth_vertex_4f.z );
        smg.positions[j] = fixUp(m->up_axis, smg.positions[j]);

        smg.positions[j] = smg.positions[j] * scalingfactor;

        smg.positions[j] = orientation * smg.positions[j] ;

        smg.positions[j].x += (location.x - bnds.center().x);
        smg.positions[j].y += (location.y - bnds.center().y);
        smg.positions[j].z += (location.z - bnds.center().z);


        if (smg.aabb == BoundingBox3f3f::null()) {
          smg.aabb = BoundingBox3f3f(smg.positions[j], 0);
        }
        else {
          smg.aabb.mergeIn(smg.positions[j]);
        }
      }

      instances.push_back(geomInstance);
      agg_mesh->geometry.push_back(smg);

      numAddedSubMeshGeometries++;

      totalVertices = totalVertices + smg.positions.size();
    }

    for (uint i = 0; i < instances.size(); i++) {
      GeometryInstance geomInstance = instances[i];

      for (GeometryInstance::MaterialBindingMap::iterator mat_it = geomInstance.materialBindingMap.begin();
           mat_it != geomInstance.materialBindingMap.end(); mat_it++)
        {
          (mat_it->second) += agg_mesh->materials.size();
        }

      agg_mesh->instances.push_back(geomInstance);
    }

    agg_mesh->materials.insert(agg_mesh->materials.end(),
                               m->materials.begin(),
                               m->materials.end());

    uint lightsSize = agg_mesh->lights.size();
    agg_mesh->lights.insert(agg_mesh->lights.end(),
                            m->lights.begin(),
                            m->lights.end());
    for (uint j = 0; j < m->lightInstances.size(); j++) {
      LightInstance& lightInstance = m->lightInstances[j];
      lightInstance.lightIndex += lightsSize;
      agg_mesh->lightInstances.push_back(lightInstance);
    }

    for (Meshdata::URIMap::const_iterator it = m->textureMap.begin();
         it != m->textureMap.end(); it++)
      {
        agg_mesh->textureMap[it->first] = it->second;
      }
  }

  for (uint i= 0; i < children.size(); i++) {
    UUID child_uuid = children[i];
    boost::mutex::scoped_lock lock(mAggregateObjectsMutex);

    if ( mAggregateObjects.find(child_uuid) == mAggregateObjects.end()) {
      assert(false);
    }
    
    MeshdataPtr mptr = mAggregateObjects[child_uuid]->mMeshdata;
    mAggregateObjects[child_uuid]->mMeshdata = std::tr1::shared_ptr<Meshdata>();    
  }

  mMeshSimplifier.simplify(agg_mesh, 20000);
  
  aggObject->mMeshdata = agg_mesh;    

  uploadMesh(uuid, agg_mesh);

  std::cout << "Generated another one...\n";

  /* Set the mesh for the aggregated object and if it has a parent, schedule
   a task to update the parent's mesh */
  if (aggObject->mParentUUID != UUID::null()) {
    generateAggregateMeshAsync(aggObject->mParentUUID, Timer::now());
    std::cout << "Posted parent generation task...\n";
  }
}

void AggregateManager::metadataFinished(const UUID uuid, const UUID child_uuid, std::string meshName,
                                          std::tr1::shared_ptr<Transfer::MetadataRequest> request,
                                          std::tr1::shared_ptr<Transfer::RemoteFileMetadata> response)
{
  if (response != NULL) {
    const Transfer::RemoteFileMetadata metadata = *response;

    Transfer::TransferRequestPtr req(new Transfer::ChunkRequest(response->getURI(), metadata,
                                               response->getChunkList().front(), 1.0,
                                               std::tr1::bind(&AggregateManager::chunkFinished, this, uuid, child_uuid,
                                                              std::tr1::placeholders::_1,
                                                              std::tr1::placeholders::_2) ) );

    mTransferPool->addRequest(req);
  }
  else {
    std::cout<<"Failed metadata download"   <<std::endl;
  }
}

void AggregateManager::chunkFinished(const UUID uuid, const UUID child_uuid,
                                       std::tr1::shared_ptr<Transfer::ChunkRequest> request,
                                       std::tr1::shared_ptr<const Transfer::DenseData> response)
{
    if (response != NULL) {
      boost::mutex::scoped_lock aggregateObjectsLock(mAggregateObjectsMutex);
      if (mAggregateObjects[child_uuid]->mMeshdata == std::tr1::shared_ptr<Meshdata>() ) {

        MeshdataPtr m = mModelsSystem->load(request->getURI(), request->getMetadata().getFingerprint(), response);

        mAggregateObjects[child_uuid]->mMeshdata = m;

        aggregateObjectsLock.unlock();

        {
          boost::mutex::scoped_lock meshStoreLock(mMeshStoreMutex);
          mMeshStore[request->getURI().toString()] = m;
        }

        generateAggregateMesh(uuid);
      }
    }
    else {
      std::cout << "ChunkFinished fail!\n";
    }
}

void AggregateManager::uploadMesh(const UUID& uuid, std::tr1::shared_ptr<Meshdata> meshptr) {
  boost::mutex::scoped_lock lock(mUploadQueueMutex);

  mUploadQueue[uuid] = meshptr;


  std::cout << mUploadQueue.size() << " : mUploadQueue.size\n";

  mCondVar.notify_one();
}

void AggregateManager::uploadQueueServiceThread() {

  while (mThreadRunning) {
    boost::mutex::scoped_lock lock(mUploadQueueMutex);

    while (mUploadQueue.empty()) {
      mCondVar.wait(lock);

      if (!mThreadRunning) {
        return;
      }
    }

    if (mUploadQueue.size() < 40) {
      std::map<UUID, std::tr1::shared_ptr<Meshdata>  >::iterator it = mUploadQueue.begin();
      UUID uuid = it->first;
      std::tr1::shared_ptr<Meshdata> meshptr = it->second;

      mUploadQueue.erase(it);

      lock.unlock();   

      const int MESHNAME_LEN = 1024;
      char localMeshName[MESHNAME_LEN];
      snprintf(localMeshName, MESHNAME_LEN, "aggregate_mesh_%s.dae", uuid.toString().c_str());
      
      mModelsSystem->convertMeshdata(*meshptr, "colladamodels", std::string("/home/tahir/merucdn/meru/dump/") + localMeshName);      
      //Upload to CDN
      std::string cmdline = std::string("./upload_to_cdn.sh ") +  localMeshName;
      system( cmdline.c_str()  );    

      //Update loc
      std::string cdnMeshName = "meerkat:///tahir/" + std::string(localMeshName);
      mLoc->updateLocalAggregateMesh(uuid, cdnMeshName);    
    }
    else {
      std::map<UUID, std::tr1::shared_ptr<Meshdata>  >::iterator it = mUploadQueue.begin();
      
      while (it != mUploadQueue.end()) {
        UUID uuid = it->first;
        MeshdataPtr meshptr = it->second;

        const int MESHNAME_LEN = 1024;
        char localMeshName[MESHNAME_LEN];
        snprintf(localMeshName, MESHNAME_LEN, "aggregate_mesh_%s.dae", uuid.toString().c_str());
      
        mModelsSystem->convertMeshdata(*meshptr, "colladamodels", std::string("/home/tahir/merucdn/meru/dump/") + localMeshName);        
      
        //Upload to CDN
        std::string cmdline = std::string("./upload_to_cdn.sh ") +  localMeshName;
        system( cmdline.c_str()  );    

        //Update loc
        std::string cdnMeshName = "meerkat:///tahir/" + std::string(localMeshName);
        mLoc->updateLocalAggregateMesh(uuid, cdnMeshName);    

        it++;
      }

      mUploadQueue.clear();
    }
  }
}

}