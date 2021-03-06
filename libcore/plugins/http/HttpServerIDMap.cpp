// Copyright (c) 2012 Sirikata Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE file.

#include "HttpServerIDMap.hpp"
#include <sirikata/core/network/Address4.hpp>
#include <boost/lexical_cast.hpp>

namespace Sirikata {

using namespace Sirikata::Transfer;

HttpServerIDMap::HttpServerIDMap(
    Context* ctx,
    const String& internal_host, const String& internal_service, const String& internal_path,
    const String& external_host, const String& external_service, const String& external_path)
 : ServerIDMap(ctx),
   mInternalHost(internal_host),
   mInternalService(internal_service),
   mInternalPath(internal_path),
   mExternalHost(external_host),
   mExternalService(external_service),
   mExternalPath(external_path)
{
}


void HttpServerIDMap::lookupInternal(const ServerID& sid, Address4LookupCallback cb) {
    startLookup(
        sid, cb,
        mInternalHost, mInternalService, mInternalPath
    );
}

void HttpServerIDMap::lookupExternal(const ServerID& sid, Address4LookupCallback cb) {
    startLookup(
        sid, cb,
        mExternalHost, mExternalService, mExternalPath
    );
}

void HttpServerIDMap::lookupRandomExternal(Address4LookupCallback cb) {
    // NullServerID indicates pick randomly
    startLookup(
        NullServerID, cb,
        mExternalHost, mExternalService, mExternalPath
    );
}

void HttpServerIDMap::startLookup(
    const ServerID& sid, Address4LookupCallback cb,
    const String& host,
    const String& service,
    const String& path
){
    Network::Address addr(host, service);

    HttpManager::Headers request_headers;
    request_headers["Host"] = host;
    HttpManager::QueryParameters query_params;
    // Empty query params if 0, i.e. random, is requested
    if (sid != NullServerID)
        query_params["server"] = boost::lexical_cast<String>(sid);
    HttpManager::getSingleton().get(
        addr, path,
        std::tr1::bind(&HttpServerIDMap::finishLookup, this, sid, cb, _1, _2, _3),
        request_headers, query_params
    );
}

void HttpServerIDMap::finishLookup(
    const ServerID& sid, Address4LookupCallback cb,
    HttpManager::HttpResponsePtr response,
    HttpManager::ERR_TYPE error,
    const boost::system::error_code &boost_error)
{
    // Error conditions
    if (error != HttpManager::SUCCESS) {
        SILOG(http-serverid-map, error, "Failed to lookup server " << sid);
        mContext->ioService->post(std::tr1::bind(cb, sid, Address4::Null), "HttpServerIDMap::finishLookup");
        return;
    }

    // Parse the response. Currently we assume we have (serverid)\n(host):(service).
    // TODO(ewencp) we could decide how to parse based on content type (e.g. for
    // json)
    String resp = response->getData()->asString();
    String::size_type line_split_pos = resp.find("\n");
    String::size_type split_pos = resp.find(":");
    if (line_split_pos == String::npos || split_pos == String::npos) {
        SILOG(http-serverid-map, error, "Couldn't parse response for server lookup " << sid << ": " << resp);
        mContext->ioService->post(std::tr1::bind(cb, sid, Address4::Null), "HttpServerIDMap::finishLookup");
        return;
    }

    ServerID resolved_sid = boost::lexical_cast<ServerID>( resp.substr(0,line_split_pos) );
    assert(resolved_sid == sid || sid == NullServerID);
    Network::Address retval(
        resp.substr(line_split_pos+1,split_pos-line_split_pos-1),
        resp.substr(split_pos+1)
    );
    mContext->ioService->post(std::tr1::bind(cb, resolved_sid, retval), "HttpServerIDMap::finishLookup");
}

}//end namespace sirikata
