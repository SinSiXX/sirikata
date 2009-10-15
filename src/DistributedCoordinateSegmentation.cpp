/*  cbr
 *  DistributedCoordinateSegmentation.cpp
 *
 *  Copyright (c) 2009, Tahir Azim
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
 *  * Neither the name of cbr nor the names of its contributors may
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

#include "DistributedCoordinateSegmentation.hpp"

#include <algorithm>
#include <boost/tokenizer.hpp>
#include <boost/bind.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <gcrypt.h>

#include "Options.hpp"
#include "Network.hpp"
#include "Message.hpp"
#include "WorldPopulationBSPTree.hpp"

namespace CBR {

template<typename T>
T clamp(T val, T minval, T maxval) {
    if (val < minval) return minval;
    if (val > maxval) return maxval;
    return val;
}

void memdump(uint8* buffer, int len) {
  for (int i=0; i<len; i++) {
    uint8* val = buffer+i;
    printf("at addr %p, val=%d\n",  val, *val);
  }
  printf("\n");
  fflush(stdout);
}


String sha1(void* data, size_t len) {
  int hash_len = gcry_md_get_algo_dlen( GCRY_MD_SHA1 );
  /* output sha1 hash - this will be binary data */
  unsigned char hash[ hash_len ];
  gcry_md_hash_buffer( GCRY_MD_SHA1, hash, data, len);

  char *out = (char *) malloc( sizeof(char) * ((hash_len*2)+1) );
  char *p = out;

  /* Convert each byte to its 2 digit ascii
   * hex representation and place in out */
  for ( int i = 0; i < hash_len; i++, p += 2 ) {
    snprintf ( p, 3, "%02x", hash[i] );
  }

  String retval = out;
  free(out);

  return retval;
}

DistributedCoordinateSegmentation::DistributedCoordinateSegmentation(SpaceContext* ctx, const BoundingBox3f& region, const Vector3ui32& perdim, int nservers, ServerIDMap * sidmap)
 : CoordinateSegmentation(ctx),
   mLastUpdateTime(mContext->time),
   mSidMap(sidmap)
{
  mAvailableCSEGServers = GetOption("num-cseg-servers")->as<uint16>();

  assert (nservers >= perdim.x * perdim.y * perdim.z);

  if (GetOption("cseg-uses-world-pop")->as<bool>()) {
    WorldPopulationBSPTree wPopTree;
    wPopTree.constructBSPTree(mTopLevelRegion);
  }
  else {
    
    mTopLevelRegion.mBoundingBox = region;

    mTopLevelRegion.mLeftChild = new SegmentedRegion();
    mTopLevelRegion.mRightChild = new SegmentedRegion();

    float minX = region.min().x; float minY = region.min().y;
    float maxX = region.max().x; float maxY = region.max().y;
    float minZ = region.min().z; float maxZ = region.max().z;

    mTopLevelRegion.mLeftChild->mBoundingBox = BoundingBox3f( region.min(),
							      Vector3f( maxX, (minY+maxY)/2, maxZ) );
    mTopLevelRegion.mRightChild->mBoundingBox = BoundingBox3f( Vector3f(minX,(minY+maxY)/2,minZ),
							       region.max() );

    mTopLevelRegion.mLeftChild->mLeftChild = new SegmentedRegion();
    mTopLevelRegion.mRightChild->mRightChild = new SegmentedRegion();
    mTopLevelRegion.mLeftChild->mRightChild = new SegmentedRegion();
    mTopLevelRegion.mRightChild->mLeftChild = new SegmentedRegion();

    mTopLevelRegion.mLeftChild->mLeftChild->mServer = 1;
    mTopLevelRegion.mLeftChild->mRightChild->mServer = 2;
    mTopLevelRegion.mRightChild->mLeftChild->mServer = 3;
    mTopLevelRegion.mRightChild->mRightChild->mServer = 4;

    mTopLevelRegion.mLeftChild->mLeftChild->mBoundingBox = BoundingBox3f( Vector3f(minX, minY, minZ), Vector3f( (minX+maxX)/2, (minY+maxY)/2, maxZ) );
    mTopLevelRegion.mLeftChild->mRightChild->mBoundingBox = BoundingBox3f( Vector3f((minX+maxX)/2, minY, minZ), Vector3f(maxX, (minY+maxY)/2, maxZ));

    mTopLevelRegion.mRightChild->mLeftChild->mBoundingBox = BoundingBox3f( Vector3f(minX, (minY+maxY)/2, minZ), Vector3f( (minX+maxX)/2, maxY, maxZ));
    mTopLevelRegion.mRightChild->mRightChild->mBoundingBox = BoundingBox3f( Vector3f((minX+maxX)/2, (minY+maxY)/2, minZ), Vector3f(maxX, maxY, maxZ));
  }

  int numLLTreesSoFar = 0;
  generateHierarchicalTrees(&mTopLevelRegion, 1, numLLTreesSoFar);

  for (int i=0; i<nservers;i++) {
    ServerAvailability sa;
    sa.mServer = i+1;
    (i<perdim.x*perdim.y*perdim.z) ? (sa.mAvailable = 0) : (sa.mAvailable = 1);

    mAvailableServers.push_back(sa);
  }

  mAcceptor = boost::shared_ptr<tcp::acceptor>(new tcp::acceptor(mIOService,tcp::endpoint(tcp::v4(), atoi( GetOption("cseg-service-tcp-port")->as<String>().c_str() ))));

  Address4* addy = sidmap->lookupInternal(ctx->id());
  uint16 cseg_server_ll_port = addy->getPort()+10000;
  mLLTreeAcceptor = boost::shared_ptr<tcp::acceptor>(new tcp::acceptor(mIOService,tcp::endpoint(tcp::v4(), cseg_server_ll_port)));

  startAcceptingLLRequests();

  startAccepting();

  boost::thread thrd(boost::bind(&DistributedCoordinateSegmentation::ioServicingLoop, this));
}

DistributedCoordinateSegmentation::~DistributedCoordinateSegmentation() {
    //delete all the SegmentedRegion objects created with 'new'
    mTopLevelRegion.destroy();
}

void DistributedCoordinateSegmentation::ioServicingLoop() {
  while (true) {
    mIOService.poll_one();
  }
}

ServerID DistributedCoordinateSegmentation::lookup(const Vector3f& pos) {
  Vector3f searchVec = pos;
  BoundingBox3f region = mTopLevelRegion.mBoundingBox;

  int i=0;
  (searchVec.z < region.min().z) ? searchVec.z = region.min().z : (i=0);
  (searchVec.x < region.min().x) ? searchVec.x = region.min().x : (i=0);
  (searchVec.y < region.min().y) ? searchVec.y = region.min().y : (i=0);

  (searchVec.z > region.max().z) ? searchVec.z = region.max().z : (i=0);
  (searchVec.x > region.max().x) ? searchVec.x = region.max().x : (i=0);
  (searchVec.y > region.max().y) ? searchVec.y = region.max().y : (i=0);

  const SegmentedRegion* segRegion = mTopLevelRegion.lookup(searchVec);
  ServerID topLevelIdx = segRegion->mServer;

  //std::cout << "Returned remote CSEG: " << topLevelIdx << " for vector " << pos <<"\n";

  if (topLevelIdx == mContext->id())
  {
    SerializedBBox serializedBBox;
    serializedBBox.serialize(segRegion->mBoundingBox);
    String bbox_hash = sha1(&serializedBBox, sizeof(serializedBBox));

    std::map<String, SegmentedRegion*>::iterator it=  mHigherLevelTrees.find(bbox_hash);

    if (it != mHigherLevelTrees.end()) {
      segRegion = (*it).second->lookup(searchVec);

      //printf("local lookup returned %d\n", segRegion->mServer);

      return segRegion->mServer;
    }

    it =  mLowerLevelTrees.find(bbox_hash);

    if (it != mLowerLevelTrees.end()) {
      segRegion = (*it).second->lookup(searchVec);

      //printf("local lookup returned %d\n", segRegion->mServer);

      return segRegion->mServer;
    }
    else {
      return 0;
    }
  }
  else {
    //remote function call to the relevant server
    return callLowerLevelCSEGServer(topLevelIdx, searchVec, segRegion->mBoundingBox);
  }
}

BoundingBoxList DistributedCoordinateSegmentation::serverRegion(const ServerID& server)
{
  if (mWholeTreeServerRegionMap.find(server) != mWholeTreeServerRegionMap.end()) {
    return mWholeTreeServerRegionMap[server];
  }

  BoundingBoxList boundingBoxList;

  for(std::map<String, SegmentedRegion*>::const_iterator it = mHigherLevelTrees.begin();
      it != mHigherLevelTrees.end(); ++it)
  {
      it->second->serverRegion(server, boundingBoxList);
  }

  for(std::map<String, SegmentedRegion*>::const_iterator it = mLowerLevelTrees.begin();
      it != mLowerLevelTrees.end(); ++it)
  {
      it->second->serverRegion(server, boundingBoxList);
  }

  callLowerLevelCSEGServersForServerRegions(server, boundingBoxList);

  if (boundingBoxList.size() > 0) {
      mWholeTreeServerRegionMap[server] = boundingBoxList;
  }

  if (boundingBoxList.size() == 0) {
    boundingBoxList.push_back(BoundingBox3f(Vector3f(0,0,0), Vector3f(0,0,0)));
  }

  //printf("Finally, server regions=%d for server ID %d\n", boundingBoxList.size(), server);
  //std::cout << "boundingBox for server " << server << " : " << boundingBoxList[0] << "\n";

  return boundingBoxList;
}

BoundingBox3f DistributedCoordinateSegmentation::region()  {
  return mTopLevelRegion.mBoundingBox;
}

uint32 DistributedCoordinateSegmentation::numServers() {
  int count = mTopLevelRegion.countServers();
  return mAvailableServers.size();
  //return count;
}

uint32 DistributedCoordinateSegmentation::getAvailableServerIndex() {
  uint32 availableSvrIndex = INT_MAX;
  
  for (uint32 i=0; i<mAvailableServers.size(); i++) {
    if (mAvailableServers[i].mAvailable == true) {
      availableSvrIndex = i;      
      break;
    }
  }

  return availableSvrIndex;
}

void DistributedCoordinateSegmentation::getRandomLeafParentSibling(SegmentedRegion** randomLeaf, 
								   SegmentedRegion** sibling,
								   SegmentedRegion** parent)
{
  int numLowerLevelTrees = rand() % mLowerLevelTrees.size();
  int i=0;

  for (std::map<String, SegmentedRegion*>::iterator it=mLowerLevelTrees.begin();
       it != mLowerLevelTrees.end();
       it++, i++)
  {
    if (i == numLowerLevelTrees) {      

      *randomLeaf = it->second->getRandomLeaf();

      *sibling = it->second->getSibling(*randomLeaf);
      *parent = it->second->getParent(*randomLeaf);
      
      break;
    }
  }
}

void DistributedCoordinateSegmentation::service() {
  Time t = mContext->time;

  if ( !GetOption("random-splits-merges")->as<bool>()) {
    return;
  }

  uint32 availableSvrIndex = getAvailableServerIndex();

  if (availableSvrIndex == INT_MAX) 
    return;

  ServerID availableServer = mAvailableServers[availableSvrIndex].mServer;

  static bool splitAlready = false;

  /* Do splits and merges every 15 seconds, except the first time, when its done
     15 seconds after startup.
   */
  static int duration = 15;
  if (t - mLastUpdateTime > Duration::seconds(duration)) {
    mLastUpdateTime = t;
    duration = 15;

    SegmentedRegion* randomLeaf = NULL;
    SegmentedRegion* sibling = NULL;
    SegmentedRegion* parent = NULL;
    getRandomLeafParentSibling(&randomLeaf, &sibling, &parent);

    bool okToMerge = false;
    if ( sibling != NULL) {
      if (sibling->mLeftChild == NULL && sibling->mRightChild == NULL) {
        okToMerge = true;
      }
    }

    std::vector<Listener::SegmentationInfo> segInfoVector;
    if (okToMerge && rand() % 2 == 0 && false) {
      if (parent == NULL) {	
	return;
      }

      SegmentedRegion* leftChild = parent->mLeftChild;
      SegmentedRegion* rightChild = parent->mRightChild;

      parent->mServer = leftChild->mServer;
      parent->mRightChild = parent->mLeftChild = NULL;

      for (uint32 i=0; i<mAvailableServers.size(); i++) {
	if (mAvailableServers[i].mServer == rightChild->mServer) {
	  mAvailableServers[i].mAvailable = true;
	  break;
	}
      }

      std::cout << "Merged! " << leftChild->mServer << " : " << rightChild->mServer << "\n";

      mWholeTreeServerRegionMap.erase(parent->mServer);
      mLowerTreeServerRegionMap.erase(rightChild->mServer);

      Listener::SegmentationInfo segInfo, segInfo2;
      segInfo.server = parent->mServer;
      segInfo.region = serverRegion(parent->mServer);
      segInfoVector.push_back( segInfo );

      segInfo2.server = rightChild->mServer;
      segInfo2.region = serverRegion(rightChild->mServer);
      segInfoVector.push_back(segInfo2);      

      delete rightChild;
      delete leftChild;
    }
    else if (!splitAlready) {

      splitAlready = true;
      std::cout << "split\n";
      randomLeaf->mLeftChild = new SegmentedRegion();
      randomLeaf->mRightChild = new SegmentedRegion();

      BoundingBox3f region = randomLeaf->mBoundingBox;
      float minX = region.min().x; float minY = region.min().y;
      float maxX = region.max().x; float maxY = region.max().y;
      float minZ = region.min().z; float maxZ = region.max().z;

      randomLeaf->mLeftChild->mBoundingBox = BoundingBox3f( region.min(),
							    Vector3f( (minX+maxX)/2, maxY, maxZ) );
      randomLeaf->mRightChild->mBoundingBox = BoundingBox3f( Vector3f( (minX+maxX)/2,minY,minZ),
                                                             region.max() );
      randomLeaf->mLeftChild->mServer = randomLeaf->mServer;
      randomLeaf->mRightChild->mServer = availableServer;

      std::cout << randomLeaf->mServer << " : " << randomLeaf->mLeftChild->mBoundingBox << "\n";
      std::cout << availableServer << " : " << randomLeaf->mRightChild->mBoundingBox << "\n";

      mAvailableServers[availableSvrIndex].mAvailable = false;

      mWholeTreeServerRegionMap.erase(randomLeaf->mServer);
      mLowerTreeServerRegionMap.erase(availableServer);

      Listener::SegmentationInfo segInfo, segInfo2;
      segInfo.server = randomLeaf->mServer;
      segInfo.region = serverRegion(randomLeaf->mServer);
      segInfoVector.push_back( segInfo );

      segInfo2.server = availableServer;
      segInfo2.region = serverRegion(availableServer);
      segInfoVector.push_back(segInfo2);      
    }

    boost::thread thrd(boost::bind(&DistributedCoordinateSegmentation::notifySpaceServersOfChange,this,segInfoVector));
  }
}

void DistributedCoordinateSegmentation::receiveMessage(Message* msg) {
}

void DistributedCoordinateSegmentation::notifySpaceServersOfChange(const std::vector<Listener::SegmentationInfo> segInfoVector)
{
  if (segInfoVector.size() == 0) {
    return;
  }
  
  /* Initialize the serialized message to send over the wire */
  SegmentationChangeMessage* segChangeMsg = new SegmentationChangeMessage();


  segChangeMsg->numEntries = (MAX_SERVER_REGIONS_CHANGED < segInfoVector.size())
                            ? MAX_SERVER_REGIONS_CHANGED : segInfoVector.size();

  uint32 msgSize = 1+1;

  /* Fill in the fields in the message */
  for (unsigned int i=0 ;i<MAX_SERVER_REGIONS_CHANGED && i<segInfoVector.size(); i++) {
    SerializedSegmentChange* segmentChange = &segChangeMsg->changedSegments[i];
    segmentChange->serverID = segInfoVector[i].server;
    msgSize += sizeof(ServerID);

    segmentChange->listLength = segInfoVector[i].region.size();
    msgSize += sizeof(uint32);

    for (unsigned int j=0; j<segInfoVector[i].region.size(); j++) {
      BoundingBox3f bbox = segInfoVector[i].region[j];

      segmentChange->bboxList[j].serialize(bbox);
    }

    msgSize += segInfoVector[i].region.size() * sizeof(SerializedBBox);
  }

  uint8* buffer=NULL;
  uint32 buflen = segChangeMsg->serialize(&buffer);

  /* Send to CSEG servers connected to this server.  */
  sendToAllCSEGServers(buffer, buflen);

  /* Send to space servers connected to this server.  */
  sendToAllSpaceServers(buffer, buflen);
  
  delete buffer;

  delete segChangeMsg;

  printf("Notified all space servers of change\n");
}

void DistributedCoordinateSegmentation::csegChangeMessage(CBR::Protocol::CSeg::ChangeMessage* ccMsg) {
}

void DistributedCoordinateSegmentation::migrationHint( std::vector<ServerLoadInfo>& svrLoadInfo ) {
}

void DistributedCoordinateSegmentation::serializeBSPTree(SerializedBSPTree* serializedBSPTree) {
  uint32 idx = 0;
  traverseAndStoreTree(&mTopLevelRegion, idx, serializedBSPTree);
}

void DistributedCoordinateSegmentation::traverseAndStoreTree(SegmentedRegion* region, uint32& idx,
							     SerializedBSPTree* serializedTree)
{
  uint32 localIdx = idx;
  serializedTree->mSegmentedRegions[localIdx].mServerID = region->mServer;
  serializedTree->mSegmentedRegions[localIdx].mLeafCount = region->mLeafCount;
  serializedTree->mSegmentedRegions[localIdx].mBoundingBox.serialize(region->mBoundingBox);

  // std::cout << "at index " << localIdx  <<" bbox=" << region->mBoundingBox << "\n";

  if (region->mLeftChild != NULL) {
    serializedTree->mSegmentedRegions[localIdx].mLeftChildIdx = idx+1;
    traverseAndStoreTree(region->mLeftChild, ++idx, serializedTree);
  }

  if (region->mRightChild != NULL) {
    serializedTree->mSegmentedRegions[localIdx].mRightChildIdx = idx+1;
    traverseAndStoreTree(region->mRightChild, ++idx, serializedTree);
  }
}

uint32 DistributedCoordinateSegmentation::readFromSocket(boost::shared_ptr<tcp::socket> socket,
							 uint8** dataReceived,
							 bool readTillEOF)
{
  uint32 bytesReceived = 0;

  /* Read in the data from the socket. */
  for (;;)
  {
      boost::array<uint8, 65536> buf;
      boost::system::error_code error;

      size_t len = socket->read_some(boost::asio::buffer(buf), error);

      if (dataReceived == NULL) {
	*dataReceived = (uint8*) malloc (len);
      }
      else if (len > 0){
	*dataReceived = (uint8*) realloc(*dataReceived, bytesReceived+len);
      }
      memcpy((*dataReceived)+bytesReceived, buf.c_array(), len);

      bytesReceived += len;

      if ( !readTillEOF && fullMessageReceived((*dataReceived), bytesReceived) ) {
	break;
      }

      if (error == boost::asio::error::eof)
        break; // Connection closed cleanly by peer.
      else if (error) {
	std::cout << "Error reading request from "
	          << socket->remote_endpoint().address().to_string() << "\n";
		    
        throw boost::system::system_error(error); // Some other error.
      }
  }

  return bytesReceived;
}

void DistributedCoordinateSegmentation::accept_handler()
{
  uint8* dataReceived = NULL;
  uint32 bytesReceived = readFromSocket(mSocket, &dataReceived, false);

  /* Deal with the request included in the received data */
  if ( dataReceived != NULL) {
    GenericMessage* genericMessage = (GenericMessage*) dataReceived;    

    if (genericMessage->type == LOOKUP_REQUEST) {

      LookupRequestMessage* lookupMessage = (LookupRequestMessage*) dataReceived;

      LookupResponseMessage lookupResponseMessage;
      lookupResponseMessage.serverID =
	lookup(Vector3f(lookupMessage->x, lookupMessage->y, lookupMessage->z));

      boost::asio::write(*mSocket,
			 boost::asio::buffer( &lookupResponseMessage, sizeof(lookupResponseMessage)),
			 boost::asio::transfer_all() );

    }
    else if (genericMessage->type == NUM_SERVERS_REQUEST) {
      NumServersResponseMessage responseMessage;
      responseMessage.numServers = numServers();

      boost::asio::write(*mSocket,
			 boost::asio::buffer( &responseMessage, sizeof(responseMessage)),
			 boost::asio::transfer_all() );
    }
    else if (genericMessage->type == REGION_REQUEST) {
      RegionResponseMessage responseMessage;
      BoundingBox3f bbox = region();

      responseMessage.bbox.serialize(bbox);

      boost::asio::write(*mSocket,
			 boost::asio::buffer((void*) &responseMessage, sizeof(responseMessage)),
			 boost::asio::transfer_all() );
    }
    else if (genericMessage->type == SERVER_REGION_REQUEST) {
      ServerRegionRequestMessage* message = (ServerRegionRequestMessage*) dataReceived;      

      BoundingBoxList bboxList = serverRegion(message->serverID);

      ServerRegionResponseMessage* responseMessage = new ServerRegionResponseMessage();
      uint i=0;
      for (i=0; i<MAX_BBOX_LIST_SIZE && i < bboxList.size() ; i++) {
	BoundingBox3f bbox = bboxList[i];
	responseMessage->bboxList[i].serialize(bbox);
      }

      responseMessage->listLength = bboxList.size();      

      boost::asio::write(*mSocket,
			 boost::asio::buffer((void*) responseMessage, 1+4+bboxList.size()*sizeof(SerializedBBox)),
			 boost::asio::transfer_all() );
      delete responseMessage;
    }
    else if (genericMessage->type == SEGMENTATION_LISTEN) {
      SegmentationListenMessage* message = (SegmentationListenMessage*) dataReceived;
      SegmentationChangeListener sl;
      memcpy(sl.host, message->host, 255);
      printf("subscribing hostname=%s\n", message->host);
      sl.port = message->port;

      mSpacePeers.push_back(sl);
    }

    free(dataReceived);
  }

  mSocket->close();

  startAccepting();
}

void DistributedCoordinateSegmentation::acceptLLTreeRequestHandler() {
  uint8* dataReceived = NULL;
  uint32 bytesReceived = readFromSocket(mLLTreeAcceptorSocket, &dataReceived, false);
  
  // Now decode the packet from the received buffer
  uint8 packetType = dataReceived[0];

  if (packetType == LL_LOOKUP_REQUEST) {
    SerializedVector serialVector;
    SerializedBBox serialBox;
    memcpy(&serialVector, dataReceived+1, sizeof(serialVector));
    memcpy(&serialBox, dataReceived+1+sizeof(serialVector), sizeof(serialBox));

    BoundingBox3f bbox; Vector3f vect;
    serialBox.deserialize(bbox);
    serialVector.deserialize(vect);

    //std::cout << "received vect,bbox=" << vect << " , " << bbox << "\n";

    String bbox_hash = sha1(&serialBox, sizeof(serialBox));
    std::map<String, SegmentedRegion*>::iterator it=  mLowerLevelTrees.find(bbox_hash);

    ServerID retval = 0;
    if (it != mLowerLevelTrees.end()) {
      const SegmentedRegion*segRegion = (*it).second->lookup(vect);


      retval = segRegion->mServer;
    }

    uint8* buffer = new uint8[sizeof(ServerID)];
    memcpy(buffer, &retval, sizeof(retval));
    boost::asio::write(*mLLTreeAcceptorSocket,
		       boost::asio::buffer((const void*)buffer, sizeof(ServerID)),
		       boost::asio::transfer_all() );

    //std::cout << "Written reply with retval=" << retval << "\n";

    delete buffer;
  }
  else if (packetType == LL_SERVER_REGION_REQUEST) {
    ServerID serverID;
    memcpy(&serverID, dataReceived + 1, sizeof(ServerID));
    BoundingBoxList boundingBoxList;

    if (mLowerTreeServerRegionMap.find(serverID) != mLowerTreeServerRegionMap.end()) {
      boundingBoxList = mLowerTreeServerRegionMap[serverID];
    }
    else {
      for(std::map<String, SegmentedRegion*>::const_iterator it = mLowerLevelTrees.begin();
	  it != mLowerLevelTrees.end(); ++it)
	{
	  //printf("serverRegion being called on ll tree with server id=%d\n", serverID);
	  it->second->serverRegion(serverID, boundingBoxList);
	}
    }

    if (boundingBoxList.size()>0 && mLowerTreeServerRegionMap.find(serverID) == mLowerTreeServerRegionMap.end()) {
      mLowerTreeServerRegionMap[serverID] = boundingBoxList;
    }

    uint8* buffer = new uint8[sizeof(uint32) + boundingBoxList.size() * sizeof(SerializedBBox)];
    uint32 numBBoxes = boundingBoxList.size();
    memcpy(buffer, &numBBoxes, sizeof(numBBoxes));
    for (uint32 i = 0; i < boundingBoxList.size(); i++) {
      SerializedBBox serializedBbox;
      serializedBbox.serialize(boundingBoxList[i]);

      uint8* bufferPos = buffer + sizeof(numBBoxes) + i * sizeof(SerializedBBox);
      memcpy(bufferPos, &serializedBbox , sizeof(SerializedBBox));
    }

    boost::asio::write(*mLLTreeAcceptorSocket,
		       boost::asio::buffer((const void*)buffer, sizeof(uint32) + boundingBoxList.size() * sizeof(SerializedBBox)),
		       boost::asio::transfer_all() );

    //std::cout << "Written reply with numBboxes=" << boundingBoxList.size() << "\n";

    delete buffer;
  }
  else if (packetType == SEGMENTATION_CHANGE) {
    boost::asio::io_service io_service;

    tcp::resolver resolver(io_service);

    mWholeTreeServerRegionMap.clear();

    SegmentationChangeMessage* segChangeMessage = (SegmentationChangeMessage*) dataReceived;

    int offset = 2;

    for (int i=0; i<segChangeMessage->numEntries; i++) {
      SerializedSegmentChange* segChange = (SerializedSegmentChange*) (dataReceived+offset);	
      BoundingBox3f bbox;
      segChange->bboxList[0].deserialize(bbox);
      std::cout << "segchange: serverID "<<segChange->serverID
		<< " bbox: " << bbox << "\n";
	
      for (uint32 i=0; i<mAvailableServers.size(); i++) {
	if (mAvailableServers[i].mServer == segChange->serverID &&
	    segChange->listLength>=1 &&
	    segChange->bboxList[0].minX != segChange->bboxList[0].maxX)
          {
	    mAvailableServers[i].mAvailable = false;
	    break;
	  }
      }

      offset += (4 + 4 + sizeof(SerializedBBox)*segChange->listLength);
    }

    sendToAllSpaceServers(dataReceived, bytesReceived);      
  }
  
  mLLTreeAcceptorSocket->close();

  if (dataReceived != NULL) {
    free(dataReceived);
  }

  startAcceptingLLRequests();
}

void DistributedCoordinateSegmentation::startAccepting() {
  mSocket = boost::shared_ptr<tcp::socket>(new tcp::socket(mIOService));
  mAcceptor->async_accept(*mSocket, boost::bind(&DistributedCoordinateSegmentation::accept_handler,this));
}

void DistributedCoordinateSegmentation::startAcceptingLLRequests() {
  mLLTreeAcceptorSocket = boost::shared_ptr<tcp::socket>(new tcp::socket(mIOService));
  mLLTreeAcceptor->async_accept(*mLLTreeAcceptorSocket, boost::bind(&DistributedCoordinateSegmentation::acceptLLTreeRequestHandler,this));
}


void DistributedCoordinateSegmentation::generateHierarchicalTrees(SegmentedRegion* region, int depth, int& numLLTreesSoFar) {
  int cutOffDepth = 3;

  if (depth>=cutOffDepth) {
    region->mServer =  (numLLTreesSoFar % mAvailableCSEGServers)+1;

    if ( region->mServer == mContext->id()) {
      std::cout << " Assigned to me, bbox : " << region->mBoundingBox<<"\n";
      SegmentedRegion* segRegion = new SegmentedRegion();
      segRegion->mBoundingBox = region->mBoundingBox;
      segRegion->mLeftChild = region->mLeftChild;
      segRegion->mRightChild = region->mRightChild;

      if (region->mLeftChild == NULL && region->mRightChild == NULL) {
	segRegion->mServer = region->mServer;
      }

      SerializedBBox serializedBBox;
      serializedBBox.serialize(segRegion->mBoundingBox);
      String bbox_hash = sha1(&serializedBBox, sizeof(serializedBBox));
      mLowerLevelTrees[bbox_hash] = segRegion;
    }

    numLLTreesSoFar++;

    region->mLeftChild = NULL;
    region->mRightChild = NULL;

    return;
  }

  if (region->mLeftChild == NULL && region->mRightChild == NULL) {

    SegmentedRegion* segRegion = new SegmentedRegion();
    segRegion->mBoundingBox = region->mBoundingBox;
    segRegion->mServer = region->mServer;
    region->mServer = mContext->id();

    SerializedBBox serializedBBox;
    serializedBBox.serialize(segRegion->mBoundingBox);
    //std::cout << segRegion->mServer << " : " << segRegion->mBoundingBox << "\n";
    String bbox_hash = sha1(&serializedBBox, sizeof(serializedBBox));
    mHigherLevelTrees[bbox_hash] = segRegion;
    return;
  }

  generateHierarchicalTrees(region->mLeftChild, depth+1, numLLTreesSoFar);
  generateHierarchicalTrees(region->mRightChild, depth+1, numLLTreesSoFar);
}

boost::shared_ptr<tcp::socket> DistributedCoordinateSegmentation::getSocketToCSEGServer(ServerID server_id) {
  
  
  tcp::resolver resolver(mIOService);

  Address4* addy = mSidMap->lookupInternal(server_id);
  struct in_addr ip_addr;
  ip_addr.s_addr = addy->ip;

  char* addr = inet_ntoa(ip_addr);
  char port_str[20];
  sprintf(port_str,"%d", (addy->port+10000));

  tcp::resolver::query query(tcp::v4(), addr, port_str);

  tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);

  tcp::resolver::iterator end;

  //std::cout << "Calling " << addr << "@" << port_str << "!\n";
  boost::shared_ptr<tcp::socket> socket = boost::shared_ptr<tcp::socket>(new tcp::socket (mIOService));
  boost::system::error_code error = boost::asio::error::host_not_found;
  while (error && endpoint_iterator != end)
    {
      socket->close();
      socket->connect(*endpoint_iterator++, error);
    }

  if (error) {
    std::cout << "Error connecting to  " << addr << ":" << port_str
	      <<"\n";
    

    return boost::shared_ptr<tcp::socket>();
  }

  return socket;
}

ServerID DistributedCoordinateSegmentation::callLowerLevelCSEGServer( ServerID server_id,
								      const Vector3f& searchVec,
								      const BoundingBox3f& boundingBox)
{
  boost::shared_ptr<tcp::socket> socket = getSocketToCSEGServer(server_id);
  if (socket.get() == 0) {
    assert(false);
    return 0;
  }
  

  SerializedVector serialVector;
  serialVector.serialize(searchVec);

  SerializedBBox serialBox;
  serialBox.serialize(boundingBox);

  uint16 dataSize = 1 + sizeof(serialVector) + sizeof(serialBox);
  uint8* buffer = new uint8[dataSize];
  buffer[0] = LL_LOOKUP_REQUEST;
  memcpy(buffer+1, &serialVector , sizeof(serialVector));
  memcpy(buffer+1+sizeof(serialVector), &serialBox, sizeof(serialBox));

  boost::asio::write(*socket,
                     boost::asio::buffer((const void*)buffer,dataSize),
                     boost::asio::transfer_all() );

  uint8* dataReceived = NULL;
  uint32 bytesReceived = readFromSocket(socket, &dataReceived, true);

  //std::cout << "Received reply from " << addr << "\n";

  ServerID retval = *dataReceived;

  if (dataReceived != NULL) {
    free(dataReceived);
  }

  return retval;
}

void DistributedCoordinateSegmentation::callLowerLevelCSEGServersForServerRegions(
 									ServerID server_id,
									BoundingBoxList& bbList)
{
  std::vector< boost::shared_ptr<tcp::socket> > socketList;
  bool sentToAll = false;

  for (uint32 i = 1; i <= mAvailableCSEGServers; i++) {
    if (i == mContext->id()) continue;

    boost::shared_ptr<tcp::socket> socket = getSocketToCSEGServer(i);
    if (socket.get() == 0) {      
      continue;
    }

    uint16 dataSize = 1 + sizeof(ServerID);
    uint8* buffer = new uint8[dataSize];
    buffer[0] = LL_SERVER_REGION_REQUEST; 
    memcpy(buffer+1, &server_id , sizeof(ServerID));

    boost::asio::write(*socket,
		       boost::asio::buffer((const void*)buffer,dataSize),
		       boost::asio::transfer_all() );

    socketList.push_back(socket);
    //std::cout << "Sent over serverRegion request to " << addr << "@" << port_str  <<"\n";
  }


  for (uint32 i = 0; i<socketList.size(); i++)
  {
    uint8* dataReceived = NULL;
    boost::shared_ptr<tcp::socket> socket = socketList[i];
    uint32 bytesReceived = readFromSocket(socket, &dataReceived, true);

    assert(bytesReceived >= 4);
    uint32 numBBoxesReturned;
    memcpy(&numBBoxesReturned, dataReceived, sizeof(uint32));

    assert( bytesReceived >= 4 + numBBoxesReturned * sizeof(SerializedBBox) );

    for (uint32 j=0; j < numBBoxesReturned; j++) {
      SerializedBBox serializedBbox;
      BoundingBox3f bbox;
      memcpy(&serializedBbox, dataReceived+sizeof(uint32)+sizeof(SerializedBBox)*j, sizeof(SerializedBBox));
      serializedBbox.deserialize(bbox);
      bbList.push_back(bbox);
      
    }

    if (dataReceived != NULL) {
      free(dataReceived);
    }    
  }

  socketList.clear();
}

void DistributedCoordinateSegmentation::sendToAllCSEGServers(uint8* buffer, int buflen) {
  /* Send to other CSEG servers so they can forward the segmentation change message to
     space servers connected to them. */
  

  for (int i = 1; i <= mAvailableCSEGServers; i++) {
    if (i == mContext->id()) continue;

    boost::shared_ptr<tcp::socket> socket = getSocketToCSEGServer(i);
    if (socket.get() == 0) {      
      continue;
    }

    boost::asio::write(*socket,
		       boost::asio::buffer((void*) buffer, buflen),
		       boost::asio::transfer_all() );

    socket->close();
  }
}

void DistributedCoordinateSegmentation::sendToAllSpaceServers(uint8* buffer, int buflen) {
  boost::asio::io_service io_service;
  tcp::resolver resolver(io_service);

  for (std::vector<SegmentationChangeListener>::const_iterator it=mSpacePeers.begin(); 
       it!=mSpacePeers.end(); it++) 
  {
    SegmentationChangeListener scl = *it;

    char* addr = scl.host;
    char port_str[20];
    sprintf(port_str,"%d", scl.port);

    tcp::resolver::query query(tcp::v4(), addr, port_str);

    tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);

    tcp::resolver::iterator end;

    std::cout << "Calling " << addr << "@" << port_str << "!\n";
    tcp::socket socket(io_service);
    boost::system::error_code error = boost::asio::error::host_not_found;
    while (error && endpoint_iterator != end)
      {
	socket.close();
	socket.connect(*endpoint_iterator++, error);
      }
    if (error) {
      std::cout << "Connection refused to " << addr << ":"<<port_str <<"\n";
      continue;
    }
    
    boost::asio::write(socket,
		       boost::asio::buffer((void*) buffer, buflen),
		       boost::asio::transfer_all() );    

    socket.close();
  }
}

bool DistributedCoordinateSegmentation::fullMessageReceived(uint8* dataReceived, uint32 bytesReceived){
  if (bytesReceived <= 0) {
    return false;
  }

  uint8 type = dataReceived[0];

  switch (type) {
  case LOOKUP_REQUEST: 
    return (bytesReceived >= sizeof(LookupRequestMessage));

  case NUM_SERVERS_REQUEST:
    return (bytesReceived >= sizeof(NumServersRequestMessage));

  case SERVER_REGION_REQUEST: 
    return (bytesReceived >= sizeof(ServerRegionRequestMessage));

  case REGION_REQUEST: 
    return (bytesReceived >= sizeof(RegionRequestMessage));

  case SEGMENTATION_LISTEN: 
    return (bytesReceived >= sizeof(SegmentationListenMessage));

  case SEGMENTATION_CHANGE: 
    return false;

  case LL_SERVER_REGION_REQUEST:
    return (bytesReceived >= 1+sizeof(ServerID));

  case LL_LOOKUP_REQUEST: 
    return (bytesReceived >= 1+sizeof(SerializedVector)+sizeof(SerializedBBox));

  default: return false;
  }

  return false;
}


} // namespace CBR
