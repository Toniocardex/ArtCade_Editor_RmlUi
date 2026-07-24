#pragma once

namespace ArtCade {

/**
 * IModule — base interface for every engine system.
 *
 * Rules:
 *  - init()     : allocate resources; called once at startup in dependency order.
 *  - shutdown() : release resources; called in reverse dependency order.
 *  - No logic in constructors/destructors beyond default initialisation.
 */
class IModule {
public:
    virtual ~IModule() = default;

    virtual bool init()     = 0;
    virtual void shutdown() = 0;

    // Modules are non-copyable, non-movable by default.
    IModule(const IModule&)            = delete;
    IModule& operator=(const IModule&) = delete;
    IModule(IModule&&)                 = delete;
    IModule& operator=(IModule&&)      = delete;

protected:
    IModule() = default;
};

} // namespace ArtCade
