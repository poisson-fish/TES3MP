#include "transientactorpresentation.hpp"

#include "objects.hpp"
#include "renderingmanager.hpp"

#include <components/esm3/loadnpc.hpp>
#include <components/misc/convert.hpp>

#include "../mwclass/npc.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/manualref.hpp"

namespace MWRender
{
    class TransientActorPresentation::Impl
    {
    public:
        Impl(RenderingManager& rendering, const MWWorld::ESMStore& store, const ESM::RefId& npcRecord,
            MWWorld::CellStore& cell, const ESM::Position& position)
            : mRendering(rendering)
            , mReference(store, npcRecord)
            , mPtr(mReference.getPtr().mRef, &cell)
        {
            if (mPtr.getType() != ESM::REC_NPC_)
                throw std::runtime_error("transient actor record is not an NPC");
            static_cast<const MWClass::Npc&>(mPtr.getClass()).ensureTransientPresentationData(mPtr);
            mPtr.getRefData().setPosition(position);
            mRendering.getObjects().insertNPC(mPtr);
        }

        ~Impl()
        {
            mRendering.removeObject(mPtr);
        }

        void update(const ESM::Position& position)
        {
            mPtr.getRefData().setPosition(position);
            mRendering.moveObject(mPtr, position.asVec3());
            mRendering.rotateObject(mPtr, Misc::Convert::makeOsgQuat(position.rot));
        }

    private:
        RenderingManager& mRendering;
        MWWorld::ManualRef mReference;
        MWWorld::Ptr mPtr;
    };

    TransientActorPresentation::TransientActorPresentation(std::unique_ptr<Impl> impl) noexcept
        : mImpl(std::move(impl))
    {
    }

    TransientActorPresentation::~TransientActorPresentation() = default;

    void TransientActorPresentation::update(const ESM::Position& position)
    {
        mImpl->update(position);
    }

    std::unique_ptr<TransientActorPresentation> TransientActorPresentation::create(RenderingManager& rendering,
        const MWWorld::ESMStore& store, const ESM::RefId& npcRecord, MWWorld::CellStore& cell,
        const ESM::Position& position)
    {
        return std::unique_ptr<TransientActorPresentation>(new TransientActorPresentation(
            std::make_unique<Impl>(rendering, store, npcRecord, cell, position)));
    }
}
