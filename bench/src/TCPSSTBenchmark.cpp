/*  Sirikata
 *  SSTBenchmark.cpp
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

#include <sirikata/core/network/IOService.hpp>
#include <sirikata/core/network/StreamFactory.hpp>
#include <sirikata/core/network/StreamListenerFactory.hpp>

#include <sirikata/core/options/Options.hpp>

#include <functional>
#include "TCPSSTBenchmark.hpp"
#include <sirikata/core/util/Timer.hpp>
#include <sirikata/core/options/Options.hpp>
#include <sirikata/core/util/PluginManager.hpp>
#define ITERATIONS 1000000

namespace Sirikata {

using std::tr1::placeholders::_1;
using std::tr1::placeholders::_2;

SSTBenchmark::SSTBenchmark(const FinishedCallback& finished_cb, const String&param)
        : Benchmark(finished_cb),
         mForceStop(false),
         mPingRate(Duration::seconds(0)),
          mStream(NULL),
          mListener(NULL),
          mStartTime(Time::epoch()),
          mPingsAttemptedSent(0),
          mPingsSent(0),
          mPingsReceived(0)
{
    OptionValue*port;
    OptionValue*host;
    OptionValue*pingSize;
    OptionValue*pingRate;
    OptionValue*ordered;
    OptionValue*streamOptions;
    OptionValue*listenOptions;
    OptionValue*whichPlugin;
    OptionValue*numPings;
    mIOService = new Sirikata::Network::IOService("SSTBenchmark");
    mIOStrand = mIOService->createStrand("SSTBenchmark Main");
    Sirikata::InitializeClassOptions ico("SSTBenchmark",this,
                                         port=new OptionValue("port","4091",Sirikata::OptionValueType<String>(),"port to connect/listen on"),
                                         host=new OptionValue("host","",Sirikata::OptionValueType<String>(),"host to connect to (blank for listen)"),
                                         pingSize=new OptionValue("ping-size","10",Sirikata::OptionValueType<size_t>(),"Size of each individual ping"),
                                         pingRate=new OptionValue("pings-per-second","0",Sirikata::OptionValueType<double>(),"number of pings launched per second"),
                                         ordered=new OptionValue("ordered","true",Sirikata::OptionValueType<bool>(),"are the pings unordered"),
                                         listenOptions=new OptionValue("listen-options","--send-buffer-size=32768",Sirikata::OptionValueType<String>(),"options passed to tcpsst"),
                                         streamOptions=new OptionValue("stream-options","--send-buffer-size=32768",Sirikata::OptionValueType<String>(),"options passed to tcpsst"),
                                         whichPlugin=new OptionValue("stream-plugin","tcpsst",Sirikata::OptionValueType<String>(),"which plugin to load for sst functionality"),
                                         numPings=new OptionValue("num-pings","1000",Sirikata::OptionValueType<size_t>(),"How many pings to "),
                                         NULL);

    OptionSet* optionsSet = OptionSet::getOptions("SSTBenchmark",this);
    optionsSet->parse(param);

    mPort=port->as<String>();
    mHost=host->as<String>();
    mPingSize=pingSize->as<size_t>();
    double pr=pingRate->as<double>();
    mPingRate=Duration::seconds(pr?1./pr:0);
    mListenOptions=listenOptions->as<String>();
    mStreamOptions=streamOptions->as<String>();
    mStreamPlugin=whichPlugin->as<String>();
    mOrdered=ordered->as<bool>();
    mPingFunction=std::tr1::bind(&SSTBenchmark::pingPoller,this);
    mNumPings=numPings->as<size_t>();
}

String SSTBenchmark::name() {
    return "ping";
}

void SSTBenchmark::pingPoller(){
    if (mPingsReceived<mNumPings&&!mForceStop) {
        Time cur=Time::now(Duration::zero());
        double invPingRate=mPingRate.toSeconds();

        uint32 numMorePings = 0;
        if (invPingRate == 0) {
            numMorePings = 1;
        }
        else {
            // Compute how many pings we expect to have seen since the beginning
            // of the test
            size_t numPings = (cur-mStartTime).toSeconds() / invPingRate;
            numMorePings = numPings - mPingsAttemptedSent;
            mPingsAttemptedSent = numPings; // BEWARE: Don't use
                                            // mPingsAttemptedSent below unless
                                            // you move this to after the loop
        }
        uint32 ii;
        for(ii = 0; ii < numMorePings; ii++) {
            Sirikata::Network::Chunk serializedChunk;
            size_t pingNumber=mOutstandingPings.size();
            for (int i=0;i<8;++i) {
                unsigned char pn=pingNumber%256;
                serializedChunk.push_back(pn);
                pingNumber/=256;
            }
            if (mPingSize>serializedChunk.size()) {
                serializedChunk.resize(mPingSize);
            }
            if (mStream->send(serializedChunk,mOrdered?Sirikata::Network::ReliableOrdered:Sirikata::Network::ReliableUnordered)) {
                ++mPingsSent;
                mOutstandingPings.push_back(cur);
                mPingResponses.push_back(Duration::zero());
            }
            else {
                break;
            }
        }
        if (mPingRate.toSeconds()!=0) {
            mIOService->post(mPingRate,mPingFunction,"SSTBenchmark Ping");
        }
    }

}
void SSTBenchmark::connected(Sirikata::Network::Stream::ConnectionStatus connectionStatus,const std::string&reason){
    if (connectionStatus==Sirikata::Network::Stream::Connected) {
       //FIXME start pinging
        mStartTime = Time::now(Duration::zero());
        mIOService->post(mPingRate,mPingFunction,"SSTBenchmark Ping");
    }
}
void SSTBenchmark::remoteConnected(Sirikata::Network::Stream*strm, Sirikata::Network::Stream::ConnectionStatus connectionStatus,const std::string&reason){
    if (connectionStatus!=Sirikata::Network::Stream::Connected) {
        strm->close();
    }
}


void SSTBenchmark::computePingTime(Sirikata::Network::Chunk&chk, const Sirikata::Network::Stream::PauseReceiveCallback& pause){
    Time cur=Time::now(Duration::zero());
    size_t index=0;
    if (chk.size()>=8) {
        for (int i=8;i-- > 0;) {
            index*=256;
            index+=chk[i];
        }
        if (index<mOutstandingPings.size()&&index<mPingResponses.size()) {
            mPingResponses[index]=cur-mOutstandingPings[index];
        }else {
            SILOG(benchmark,error,"Malformed ping packet contained invalida index");
        }
    }else {
        SILOG(benchmark,error,"Malformed time packet");
    }
    if (++mPingsReceived>=mNumPings) {
        Duration avg(Duration::zero());
        for(size_t i=0;i<mPingResponses.size();++i){
            avg+=mPingResponses[i];
        }
        avg/=(double)mPingResponses.size();
        SILOG(benchmark,info,"Test Time: "<<cur-mStartTime);
        SILOG(benchmark,info,"Ping Average "<<avg);
        SILOG(benchmark,info,"Transfer Rate "<<2*mNumPings*(double)chk.size()/(cur-mStartTime).toSeconds());
        stop();
    }else
    if (mPingRate.toSeconds()==0) {
        pingPoller();
    }
}
void SSTBenchmark::bouncePing(Sirikata::Network::Stream* strm, Sirikata::Network::Chunk&chk, const Sirikata::Network::Stream::PauseReceiveCallback& pause){
    if (!mForceStop) {
        strm->send(chk,mOrdered?Sirikata::Network::ReliableOrdered:Sirikata::Network::ReliableUnordered);
    }
}
void SSTBenchmark::newStream(Sirikata::Network::Stream*newStream, Sirikata::Network::Stream::SetCallbacks&cb) {
    if (newStream) {
      cb(std::tr1::bind(&SSTBenchmark::remoteConnected,this,newStream,std::tr1::placeholders::_1,std::tr1::placeholders::_2),
          std::tr1::bind(&SSTBenchmark::bouncePing,this,newStream,std::tr1::placeholders::_1,std::tr1::placeholders::_2),
           &Sirikata::Network::Stream::ignoreReadySendCallback);
    }
}

void noop(){}
void SSTBenchmark::start() {
    static Sirikata::PluginManager pluginManager;
    pluginManager.load(mStreamPlugin);
    mForceStop = false;
    if (!mHost.empty()) {
        mStream=Sirikata::Network::StreamFactory::getSingleton().getConstructor(mStreamPlugin)(mIOStrand,Sirikata::Network::StreamFactory::getSingleton().getOptionParser(mStreamPlugin)(mStreamOptions));
        mStream->connect(Sirikata::Network::Address(mHost,mPort),
                         &Sirikata::Network::Stream::ignoreSubstreamCallback,
                         std::tr1::bind(&SSTBenchmark::connected,this,std::tr1::placeholders::_1,std::tr1::placeholders::_2),
            std::tr1::bind(&SSTBenchmark::computePingTime,this,std::tr1::placeholders::_1,std::tr1::placeholders::_2),
                         &Sirikata::Network::Stream::ignoreReadySendCallback);

    }else {
        mListener=Sirikata::Network::StreamListenerFactory::getSingleton().getConstructor(mStreamPlugin)(mIOStrand,Sirikata::Network::StreamFactory::getSingleton().getOptionParser(mStreamPlugin)(mListenOptions));
        mListener->listen(Sirikata::Network::Address("127.0.0.1",mPort),
                          std::tr1::bind(&SSTBenchmark::newStream,this,std::tr1::placeholders::_1,std::tr1::placeholders::_2));

        mIOService->post(Duration::seconds(10000000.),&noop,"SSTBenchmark noop");

    }
    mIOService->run();
}

void SSTBenchmark::stop() {
    mForceStop = true;
    if(mIOService)
        mIOService->stop();
    if (mListener)
        mListener->close();
    if(mStream)
        mStream->close();
    if (mIOStrand)
        delete mIOStrand;
    if (mIOService)
        delete mIOService;
    mIOService=NULL;
    mStream=NULL;
    mListener=NULL;
        notifyFinished();
}


} // namespace Sirikata
