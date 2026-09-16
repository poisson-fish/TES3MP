#ifndef OPENMW_MWWORLD_PLAINEQUIPMENT_H
#define OPENMW_MWWORLD_PLAINEQUIPMENT_H

#include "containerstore.hpp"

#include <components/esm3/objectstate.hpp>

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

    // Owned semantic values, including raw dormant membership in stock order.
    // ObjectState's legacy converter is always null; no live pointers/iterators
    // or executable effects cross this boundary. Not a file or network format.
    struct PlainEquipmentValues
    {
        // A bounded source may gain one split node.
        static constexpr size_t MaxItems = 65, MaxAnimations = 256, MaxText = 4096;
        ESM::RefNum mActor, mShirt, mSelected, mLastGenerated;
        std::vector<ESM::ObjectState> mObjects;
        void validate(const ESMStore& content, ESM::RefNum expectedActor) const;
        void swap(PlainEquipmentValues& other) noexcept;
    };

    // Fresh, protected stock clothing storage. Content must outlive this object.
    // Restore validates all input before allocation; move assignment publishes
    // only a complete result. It never binds an owner/service or executes effects.
    // Runtime change tracking, scene bindings and caches are not saved values;
    // export rejects postponed physics rather than silently dropping it.
    class RestoredPlainEquipment
    {
        friend class Testing::PlainEquipmentFixture;
        struct State;
        std::unique_ptr<State> mState;
        explicit RestoredPlainEquipment(std::unique_ptr<State> state);

    public:
        static RestoredPlainEquipment restore(
            const PlainEquipmentValues& input, const ESMStore& content, ESM::RefNum expectedActor);
        RestoredPlainEquipment(RestoredPlainEquipment&&) noexcept;
        RestoredPlainEquipment& operator=(RestoredPlainEquipment&&) noexcept;
        ~RestoredPlainEquipment();
        void exportValues(PlainEquipmentValues& output) const;
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
        // Revalidate the source and serialize the protected candidate through
        // stock field writers. Failure preserves output storage as well as value.
        void exportValues(const PlainEquipmentContext& context, PlainEquipmentValues& output) const;
    };
}

#endif
