#ifndef GAME_MWWORLD_LIVECELLREF_H
#define GAME_MWWORLD_LIVECELLREF_H

#include "cellref.hpp"

#include "refdata.hpp"

#include <memory>
#include <stdexcept>

namespace ESM
{
    struct ObjectState;
}

namespace MWWorld
{
    class Ptr;
    class ESMStore;
    class Class;
    class WorldModel;
    struct LiveCellRefBase;

    // Reference ownership stays with the engine. Ptr copies carry weak witnesses,
    // captured only when constructed from a current reference. A retained witness
    // cannot keep an object alive or validate a replacement at the same address.
    class ReferenceLifetime
    {
        struct State
        {
            const LiveCellRefBase* mReference;
        };
        mutable std::shared_ptr<State> mState;

    public:
        class Witness
        {
            friend class ReferenceLifetime;
            std::weak_ptr<const State> mState;

        public:
            bool isLive(const LiveCellRefBase* reference) const
            {
                const auto state = mState.lock();
                return reference && state && state->mReference == reference;
            }
            bool operator==(const Witness& other) const
            {
                return !mState.owner_before(other.mState) && !other.mState.owner_before(mState);
            }
        };
        ReferenceLifetime() = default;
        ReferenceLifetime(const ReferenceLifetime&) noexcept {}
        ReferenceLifetime(ReferenceLifetime&&) noexcept {}
        ReferenceLifetime& operator=(const ReferenceLifetime&) noexcept { return *this; }
        ReferenceLifetime& operator=(ReferenceLifetime&&) noexcept { return *this; }
        ~ReferenceLifetime() { invalidate(); }

    private:
        friend struct LiveCellRefBase;
        template <template <class> class>
        friend class PtrBase;
        void invalidate() noexcept
        {
            if (mState)
                mState->mReference = nullptr;
        }
        Witness witness(const LiveCellRefBase* reference) const
        {
            if (!mState)
                mState = std::make_shared<State>(State{ reference });
            Witness result;
            result.mState = mState;
            return result;
        }
    };

    template <typename X>
    struct LiveCellRef;

    // Opt-in lifetime witness for prepared stock nodes. Live nodes allocate no
    // token. Copy/move construction starts a new lifetime; assigning values to an
    // existing node preserves its lifetime. A witness prevents address reuse from
    // making a destroyed node's saved iterator appear valid again.
    class PreparedNodeIdentity
    {
        std::shared_ptr<const void> mIdentity;

    public:
        PreparedNodeIdentity() = default;
        PreparedNodeIdentity(const PreparedNodeIdentity&) noexcept {}
        PreparedNodeIdentity(PreparedNodeIdentity&&) noexcept {}
        PreparedNodeIdentity& operator=(const PreparedNodeIdentity&) noexcept { return *this; }
        PreparedNodeIdentity& operator=(PreparedNodeIdentity&&) noexcept { return *this; }
        std::shared_ptr<const void> bind()
        {
            if (!mIdentity)
                mIdentity = std::make_shared<const char>();
            return mIdentity;
        }
        bool matches(const std::shared_ptr<const void>& identity) const { return identity && mIdentity == identity; }
    };

    /// Used to create pointers to hold any type of LiveCellRef<> object.
    struct LiveCellRefBase
    {
    private:
        friend class ContainerStore;
        template <template <class> class>
        friend class PtrBase;
        PreparedNodeIdentity mPreparedIdentity;
        ReferenceLifetime mReferenceLifetime;

    public:
        const Class* mClass;

        /** Information about this instance, such as 3D location and rotation
         * and individual type-dependent data.
         */
        CellRef mRef;

        /** runtime-data */
        RefData mData;

        WorldModel* mWorldModel = nullptr;

        LiveCellRefBase(unsigned int type, const ESM::CellRef& cref);
        LiveCellRefBase(unsigned int type, const ESM4::Reference& cref);
        LiveCellRefBase(unsigned int type, const ESM4::ActorCharacter& cref);

        LiveCellRefBase(const LiveCellRefBase& other) = default;

        LiveCellRefBase(LiveCellRefBase&& other) noexcept;

        /* Need this for the class to be recognized as polymorphic */
        virtual ~LiveCellRefBase();

        LiveCellRefBase& operator=(const LiveCellRefBase& other) = default;

        LiveCellRefBase& operator=(LiveCellRefBase&& other) noexcept;

        virtual void load(const ESM::ObjectState& state) = 0;
        ///< Load state into a LiveCellRef, that has already been initialised with base and class.
        ///
        /// \attention Must not be called with an invalid \a state.

        virtual void save(ESM::ObjectState& state) const = 0;
        ///< Save LiveCellRef state into \a state.

        virtual std::string_view getTypeDescription() const = 0;

        unsigned int getType() const;
        ///< @see MWWorld::Class::getType

        template <class T>
        static const LiveCellRef<T>* dynamicCast(const LiveCellRefBase* value);

        template <class T>
        static LiveCellRef<T>* dynamicCast(LiveCellRefBase* value);

        /// Returns true if the object was either deleted by the content file or by gameplay.
        bool isDeleted() const;

    protected:
        void loadImp(const ESM::ObjectState& state);
        ///< Load state into a LiveCellRef, that has already been initialised with base and
        /// class.
        ///
        /// \attention Must not be called with an invalid \a state.

        void saveImp(ESM::ObjectState& state) const;
        ///< Save LiveCellRef state into \a state.

        static bool checkStateImp(const ESM::ObjectState& state);
        ///< Check if state is valid and report errors.
        ///
        /// \return Valid?
        ///
        /// \note Does not check if the RefId exists.
    };

    inline bool operator==(const LiveCellRefBase& cellRef, const ESM::RefNum refNum)
    {
        return cellRef.mRef.getRefNum() == refNum;
    }

    std::string makeDynamicCastErrorMessage(const LiveCellRefBase* value, std::string_view recordType);

    template <class T>
    const LiveCellRef<T>* LiveCellRefBase::dynamicCast(const LiveCellRefBase* value)
    {
        if (const LiveCellRef<T>* ref = dynamic_cast<const LiveCellRef<T>*>(value))
            return ref;
        throw std::runtime_error(
            makeDynamicCastErrorMessage(value, ESM::getRecNameString(T::sRecordId).toStringView()));
    }

    template <class T>
    LiveCellRef<T>* LiveCellRefBase::dynamicCast(LiveCellRefBase* value)
    {
        if (LiveCellRef<T>* ref = dynamic_cast<LiveCellRef<T>*>(value))
            return ref;
        throw std::runtime_error(
            makeDynamicCastErrorMessage(value, ESM::getRecNameString(T::sRecordId).toStringView()));
    }

    /// A reference to one object (of any type) in a cell.
    ///
    /// Constructing this with a CellRef instance in the constructor means that
    /// in practice (where D is RefData) the possibly mutable data is copied
    /// across to mData. If later adding data (such as position) to CellRef
    /// this would have to be manually copied across.
    template <typename X>
    struct LiveCellRef : public LiveCellRefBase
    {
        LiveCellRef(const ESM::CellRef& cref, const X* b = nullptr)
            : LiveCellRefBase(X::sRecordId, cref)
            , mBase(b)
        {
        }

        LiveCellRef(const ESM4::Reference& cref, const X* b = nullptr)
            : LiveCellRefBase(X::sRecordId, cref)
            , mBase(b)
        {
        }

        LiveCellRef(const ESM4::ActorCharacter& cref, const X* b = nullptr)
            : LiveCellRefBase(X::sRecordId, cref)
            , mBase(b)
        {
        }

        // The object that this instance is based on.
        const X* mBase;

        void load(const ESM::ObjectState& state) override;
        ///< Load state into a LiveCellRef, that has already been initialised with base and class.
        ///
        /// \attention Must not be called with an invalid \a state.

        void save(ESM::ObjectState& state) const override;
        ///< Save LiveCellRef state into \a state.

        std::string_view getTypeDescription() const override
        {
            if constexpr (ESM::isESM4Rec(X::sRecordId))
            {
                static constexpr ESM::FixedString<6> name = ESM::getRecNameString(X::sRecordId);
                return name.toStringView();
            }
            else
                return X::getRecordType();
        }

        static bool checkState(const ESM::ObjectState& state);
        ///< Check if state is valid and report errors.
        ///
        /// \return Valid?
        ///
        /// \note Does not check if the RefId exists.
    };

    template <typename X>
    void LiveCellRef<X>::load(const ESM::ObjectState& state)
    {
        loadImp(state);
    }

    template <typename X>
    void LiveCellRef<X>::save(ESM::ObjectState& state) const
    {
        saveImp(state);
    }

    template <typename X>
    bool LiveCellRef<X>::checkState(const ESM::ObjectState& state)
    {
        return checkStateImp(state);
    }
}

#endif
