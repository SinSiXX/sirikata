/*  Sirikata
 *  QuakeMotionPath.hpp
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

#ifndef _SIRIKATA_QUAKE_MOTION_PATH_HPP_
#define _SIRIKATA_QUAKE_MOTION_PATH_HPP_

#include "MotionPath.hpp"

namespace Sirikata {

/** Quake motion path. The path is initialized and stored
    from a data trace generated by Quake III OpenArena.
 */
class QuakeMotionPath : public MotionPath {
public:
    QuakeMotionPath( const char* quakeDataTraceFile, float scaleDownFactor,const BoundingBox3f& region );

    virtual const TimedMotionVector3f initial() const;
    virtual const TimedMotionVector3f* nextUpdate(const Time& curtime) const;
    virtual const TimedMotionVector3f at(const Time& t) const;
private:
    TimedMotionVector3f parseTraceLines(String firstLine, String secondLine, float scaleDownFactor);

    uint32 getIDFromTraceLine(String line);

    std::vector<TimedMotionVector3f> mUpdates;

    std::string findLineMatchingID(std::ifstream& inputFile);

    uint32 countObjectsInFile(const char* inputFileName);

    uint32 mID;

    //the total number of objects whose traces are in a file.
    static std::map<const char*, uint32> mObjectsInFile;

    //the number of objects created so far from a file.
    static std::map<const char*, uint32> mObjectsCreatedFromFile;

}; // class QuakeMotionPath

} // namespace Sirikata

#endif //_SIRIKATA_QUAKE_MOTION_PATH_HPP_