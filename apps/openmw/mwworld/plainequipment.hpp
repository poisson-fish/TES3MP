#ifndef OPENMW_MWWORLD_PLAINEQUIPMENT_H
#define OPENMW_MWWORLD_PLAINEQUIPMENT_H

#include "containerstore.hpp"

#include "../mwmechanics/npcstats.hpp"

#include <array>
#include <optional>

#include <components/compiler/locals.hpp>
#include <components/esm3/objectstate.hpp>

namespace MWWorld
{
    class InventoryStore;

    // Trusted, immutable declaration binding for one shirt script. Uses the
    // stock declaration parser; scripts must belong to content, which outlives
    // this binding. No script instructions or registration run.
    class EquipmentScriptLocals
    {
        const ESM::Script* mRecord;
        ESM::RefId mId;
        std::string mText;
        const Compiler::Locals mDeclarations;

    public:
        static constexpr size_t MaxVariables = 32, MaxName = 64;
        EquipmentScriptLocals(const ESMStore& content, ESM::RefId script, MWBase::ScriptManager& scripts);
        const Compiler::Locals& declarations(const ESMStore& content, ESM::RefId script) const;
        void validate(const MWScript::Locals& locals, const ESMStore& content, ESM::RefId script) const;
    };

    // Empty script needs no service; nonempty script requires the exact binding.
    const Compiler::Locals& equipmentDeclarations(
        const ESMStore& content, ESM::RefId script, const EquipmentScriptLocals* binding);

    // Bounded equipment save fields, not a complete NPC save. Each triple is
    // base/modifier/damage for attributes, base/modifier/current for dynamics.
    struct EquipmentNpcStatsValues
    {
        ESM::RefId mBase;
        // Initialized spell IDs in stock insertion order; unused tail is empty.
        // Fixed storage keeps untrusted decode preflight allocation-free.
        static constexpr size_t MaxSpells = 256;
        std::array<ESM::RefId, MaxSpells> mSpells{};
        // Base Luck already includes this applied passive contribution. Its
        // source is the single race ability in mSpells, never the shirt.
        float mAbilityMagnitude = 0;
        std::array<std::array<float, 3>, ESM::Attribute::Length> mAttributes{};
        std::array<std::array<float, 3>, 3> mDynamic{};
        void validate(const ESMStore& content) const;
        bool operator==(const EquipmentNpcStatsValues&) const = default;
    };

    // One explicit actor's stock stats. This bounded context initializes NPDT
    // stats and initial known spells; it does not install custom data, AI or inventory.
    // No public mutable stats access: the equipment writer replaces a prepared
    // context only after durability. Content and actor must outlive validation.
    class EquipmentNpcStats
    {
        friend class PreparedPlainEquipment;
        friend class Testing::PlainEquipmentFixture;
        friend class TES3MP::Native::EquipmentRuntime;
        const Ptr mActor;
        const ESM::RefNum mIdentity;
        const ESMStore& mContent;
        const ESM::NPC* mBase;
        const int mNpdtType;
        float mMagickaMultiplier;
        std::array<ESM::RefId, EquipmentNpcStatsValues::MaxSpells> mInitialSpells{};
        MWMechanics::NpcStats mStats;
        void restore(const EquipmentNpcStatsValues& values, const InventoryStore& inventory);

    public:
        EquipmentNpcStats(const Ptr& actor, const ESMStore& content);
        EquipmentNpcStats(const EquipmentNpcStats&) = delete;
        void validate(const Ptr& actor, const ESMStore& content) const;
        EquipmentNpcStatsValues values() const;
        const MWMechanics::NpcStats& stats() const { return mStats; }
    };

    struct PlainEquipmentContext
    {
        const ESMStore& mStore;
        const WorldModel& mWorldModel;
        const LocalScripts& mLocalScripts;
        Ptr mActor;
        Ptr mPlayer;
        std::shared_ptr<const EquipmentNpcStats> mNpcStats;
        std::shared_ptr<const EquipmentScriptLocals> mScriptLocals;
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
            ItemAdded,
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
        std::optional<std::array<float, 3>> mLuck;
        std::vector<Effect> mEffects;
        bool mSkipped = false;
        ESM::RefNum mTransferred; // Transfer source/destination identity, otherwise unset.
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
        // NPC saves include initialized spells and the applied ability witness.
        // Skills, level, disposition and reputation stay at NPDT values; other
        // NPC state is outside this context, not part of this save.
        std::optional<EquipmentNpcStatsValues> mNpcStats;

        void validate(const ESMStore& content, ESM::RefNum expectedActor,
            const EquipmentScriptLocals* scripts = nullptr) const;
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
        friend class TES3MP::Native::EquipmentRuntime;
        struct State;
        std::unique_ptr<State> mState;
        explicit RestoredPlainEquipment(std::unique_ptr<State> state);
        // Only fresh runtime restart composition may relocate this storage.
        // Content must still be the exact store used to construct the nodes.
        InventoryStore& installationCandidate(
            const ESMStore& content, ESM::RefNum actor, ESM::RefNum counter) const;

    public:
        static RestoredPlainEquipment restore(
            const PlainEquipmentValues& input, const ESMStore& content, ESM::RefNum expectedActor,
            std::shared_ptr<const EquipmentScriptLocals> scripts = {});
        RestoredPlainEquipment(RestoredPlainEquipment&&) noexcept;
        RestoredPlainEquipment& operator=(RestoredPlainEquipment&&) noexcept;
        ~RestoredPlainEquipment();
        void exportValues(PlainEquipmentValues& output) const;
    };

    // Bounded engine preparation, NOT a command/installation/persistence API.
    // Only resolved, <=64-node shirt inventories and one slot are supported.
    // Actor/player must match; callers still provide authentication/serialization.
    // Plain by default; bound NPC stats opt into one fixed constant effect.
    // An explicit declaration binding permits one scripted constant shirt with
    // isolated initialized locals (single items, no executing registrations).
    // Reject other enchantments, Lua/custom state and other slots/types.
    // Content/WorldModel must outlive validation; store/reference/service lifetimes
    // are witnessed. Revalidation checks current state, not mutation history.
    // Failure leaves live storage, services, effects and prior caller output intact.
    class PreparedPlainEquipment
    {
        friend class Testing::PlainEquipmentFixture;
        friend class TES3MP::Native::EquipmentRuntime;
        struct State;
        std::unique_ptr<State> mState;
        explicit PreparedPlainEquipment(std::unique_ptr<State> state);
        // Only the runtime owner may stage installation. No public mutation
        // or persistence API: revalidate and require the exact receiving store.
        InventoryStore& installationCandidate(const PlainEquipmentContext& context, const InventoryStore& target);
        std::shared_ptr<EquipmentNpcStats>& installationNpcStats();

    public:
        static constexpr size_t MaxItems = 64;
        static PreparedPlainEquipment prepare(const ContainerStoreResolution& inventory, const ConstPtr& item,
            ESM::RefNum expectedIdentity, size_t expectedRegistryRevision, bool equip,
            const PlainEquipmentContext& context);
        // Plain, unequipped shirt transfer between these same protected stock
        // inventories. No registration/script scope expansion. Both candidates
        // share one generation counter and must be installed as a pair.
        static std::array<PreparedPlainEquipment, 2> prepareTransfer(
            const std::array<ContainerStoreResolution, 2>& inventories, const ConstPtr& item,
            ESM::RefNum expectedIdentity, size_t expectedRevision, int count,
            const std::array<PlainEquipmentContext, 2>& contexts);
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
