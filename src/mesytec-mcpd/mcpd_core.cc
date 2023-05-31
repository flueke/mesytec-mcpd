#include "mcpd_core.h"

namespace
{
    class McpdErrorCategory: public std::error_category
    {
        const char *name() const noexcept override
        {
            return "mcpd_error";
        }

        std::string message(int ev) const override
        {
            using mesytec::mcpd::CommandError;

            switch (static_cast<CommandError>(ev))
            {
                case CommandError::NoError:
                    return "No Error";

                case CommandError::IdMismatch:
                    return "ID mismatch";

                default:
                    break;
            }

            return fmt::format("Unknown error code {}", ev);
        }
    };

    const McpdErrorCategory theMcpdErrorCategory {};
}

namespace mesytec
{
namespace mcpd
{

std::error_code make_error_code(CommandError error)
{
    return { static_cast<int>(error), theMcpdErrorCategory };
}

}
}
