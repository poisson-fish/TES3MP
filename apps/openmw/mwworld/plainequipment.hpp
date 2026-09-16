#ifndef OPENMW_MWWORLD_PLAINEQUIPMENT_H
#define OPENMW_MWWORLD_PLAINEQUIPMENT_H

#include "containerstore.hpp"

namespace MWWorld
{
    struct PlainEquipmentContext
    {
        const ESMStore& mStore;
        const WorldModel& mWorldModel;
        const LocalScripts& mLocalScripts;
        Ptr mActor;
        Ptr mPlayer;
    };

    struct PlainEquipmentResult
    {
        struct Item
        {
            ESM::RefNum mIdentity;
            ESM::RefId mBase;
            int mCount;
            bool operator==(const Item&) const = default;
        };
        enum class EffectKind
        {
            RegisterSplit,
            InventoryUpdated,
            ItemRemoved,
            DeleteStackScript,
            EquipmentChanged
        };
        struct Effect
        {
            EffectKind mKind;
            ESM::RefNum mActor;
            ESM::RefNum mItem;
            int mCount;
            bool operator==(const Effect&) const = default;
        };
        ESM::RefNum mActor, mShirt, mSelected, mLastGenerated;
        std::vector<Item> mItems; // Includes dormant nodes; owned IDs/values only.
        std::vector<Effect> mEffects;
        bool operator==(const PlainEquipmentResult&) const = default;
    };

    // Bounded engine preparation, NOT a command/installation/persistence API.
    // Only resolved, <=64-node, plain-shirt inventories and one slot are supported.
    // Actor/player must match; callers still provide authentication/serialization.
    // Reject scripts, enchantments, Lua/custom state and other slots/types visibly.
    // Content/WorldModel must outlive validation; store/reference/service lifetimes
    // are witnessed. Revalidation checks current state, not mutation history.
    // Failure leaves live storage, services, effects and prior caller output intact.
    class PreparedPlainEquipment
    {
        friend class Testing::PlainEquipmentFixture;
        struct State;
        std::unique_ptr<State> mState;
        explicit PreparedPlainEquipment(std::unique_ptr<State> state);

    public:
        static constexpr size_t MaxItems = 64;
        static PreparedPlainEquipment prepare(const ContainerStoreResolution& inventory, const ConstPtr& item,
            ESM::RefNum expectedIdentity, size_t expectedRegistryRevision, bool equip,
            const PlainEquipmentContext& context);
        PreparedPlainEquipment(PreparedPlainEquipment&&) noexcept;
        PreparedPlainEquipment& operator=(PreparedPlainEquipment&&) noexcept;
        ~PreparedPlainEquipment();
        void validate(const PlainEquipmentContext& context) const;
        const PlainEquipmentResult& result() const;
    };
}

#endif
