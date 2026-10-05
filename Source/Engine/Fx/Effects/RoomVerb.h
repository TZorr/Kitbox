//
//  RoomVerb.h
//  Kitbox (copied from Rackbox - fix bugs in both)
//
//  The Room: the eight-line network tuned short. Lines between 7 and 31 ms
//  are the delays of a room a few metres across, and Size runs the decay from
//  0.15 to 1.4 s.
//

#pragma once

#include "FdnReverb.h"

class RoomVerb : public FdnReverb
{
public:
    RoomVerb() : FdnReverb ({ 7.0f, 31.0f, 0.15f, 1.4f }) {}
};
