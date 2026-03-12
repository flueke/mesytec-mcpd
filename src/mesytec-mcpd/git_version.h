#ifndef __MESYTEC_MCPD_GIT_VERSION_H__
#define __MESYTEC_MCPD_GIT_VERSION_H__

#include "mesytec-mcpd_export.h"

namespace mesytec
{
namespace mcpd
{

extern const char MESYTEC_MCPD_EXPORT GIT_SHA1[];
extern const char MESYTEC_MCPD_EXPORT GIT_VERSION[];
extern const char MESYTEC_MCPD_EXPORT GIT_VERSION_SHORT[];

inline const char *library_version() { return GIT_VERSION_SHORT; }

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MESYTEC_MCPD_GIT_VERSION_H__ */
