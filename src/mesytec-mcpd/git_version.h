#ifndef __MESYTEC_MCPD_GIT_VERSION_H__
#define __MESYTEC_MCPD_GIT_VERSION_H__

namespace mesytec
{
namespace mcpd
{

extern const char GIT_SHA1[];
extern const char GIT_VERSION[];
extern const char GIT_VERSION_SHORT[];

inline const char *library_version() { return GIT_VERSION_SHORT; }

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MESYTEC_MCPD_GIT_VERSION_H__ */
