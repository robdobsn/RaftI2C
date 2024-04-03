
#pragma once 

#include "RaftUtils.h"

class DeviceIdent
{
public:
    DeviceIdent(String ident) : ident(ident)
    {
    }
    DeviceIdent() : ident("")
    {
    }
    bool isValid() const
    {
        return ident.length() > 0;
    } 
    String ident;
};
