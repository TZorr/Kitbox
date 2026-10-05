//
//  HallVerb.h
//  Kitbox (copied from Rackbox - fix bugs in both)
//
//  The Hall: the same network tuned long. Lines between 29 and 97 ms put the
//  first reflections far apart, and Size runs the decay from 0.8 to 7 s.
//

#pragma once

#include "FdnReverb.h"

class HallVerb : public FdnReverb
{
public:
    HallVerb() : FdnReverb ({ 29.0f, 97.0f, 0.8f, 7.0f }) {}
};
