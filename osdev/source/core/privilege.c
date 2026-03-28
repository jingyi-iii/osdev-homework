#include "privilege.h"

ATTR(weak) void priv_switch(priv_mode mode)
{
    (void)mode;
}