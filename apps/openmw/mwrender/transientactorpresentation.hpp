#ifndef OPENMW_MWRENDER_TRANSIENTACTORPRESENTATION_H
#define OPENMW_MWRENDER_TRANSIENTACTORPRESENTATION_H

#include <memory>

namespace ESM
{
    class RefId;
    struct Position;
}

namespace MWWorld
{
    class CellStore;
    class ESMStore;
}

namespace MWRender
{
    class RenderingManager;

    class TransientActorPresentation
    {
    public:
        ~TransientActorPresentation();
        TransientActorPresentation(const TransientActorPresentation&) = delete;
        TransientActorPresentation& operator=(const TransientActorPresentation&) = delete;

        void update(const ESM::Position& position);

        static std::unique_ptr<TransientActorPresentation> create(RenderingManager& rendering,
            const MWWorld::ESMStore& store, const ESM::RefId& npcRecord, MWWorld::CellStore& cell,
            const ESM::Position& position);

    private:
        class Impl;
        explicit TransientActorPresentation(std::unique_ptr<Impl> impl) noexcept;
        std::unique_ptr<Impl> mImpl;
    };
}

#endif
