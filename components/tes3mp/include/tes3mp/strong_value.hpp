#ifndef TES3MP_STRONG_VALUE_HPP
#define TES3MP_STRONG_VALUE_HPP

#include <array>
#include <charconv>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <system_error>

namespace TES3MP::Detail
{
    enum class StrongValuePolicy
    {
        Identity,
        CounterFromZero,
        CounterFromOne,
    };

    template <class Tag, StrongValuePolicy Policy>
    class StrongValue
    {
    public:
        using Value = std::uint64_t;

        static constexpr std::optional<StrongValue> fromValue(Value value) noexcept
        {
            if constexpr (Policy != StrongValuePolicy::CounterFromZero)
            {
                if (value == 0)
                    return std::nullopt;
            }
            return StrongValue(value, ConstructionToken{});
        }

        static constexpr StrongValue initial() noexcept
            requires(Policy != StrongValuePolicy::Identity)
        {
            if constexpr (Policy == StrongValuePolicy::CounterFromZero)
                return StrongValue(0, ConstructionToken{});
            else
                return StrongValue(1, ConstructionToken{});
        }

        constexpr Value value() const noexcept { return mValue; }

        constexpr std::optional<StrongValue> next() const noexcept
            requires(Policy != StrongValuePolicy::Identity)
        {
            if (mValue == std::numeric_limits<Value>::max())
                return std::nullopt;
            return StrongValue(mValue + 1, ConstructionToken{});
        }

        std::string toString() const
        {
            std::array<char, std::numeric_limits<Value>::digits10 + 1> digits{};
            const auto [end, error] = std::to_chars(digits.data(), digits.data() + digits.size(), mValue);
            if (error != std::errc{})
                return std::string(Tag::name) + "{?}";

            std::string result(Tag::name);
            result.push_back('{');
            result.append(digits.data(), static_cast<std::size_t>(end - digits.data()));
            result.push_back('}');
            return result;
        }

        friend constexpr bool operator==(StrongValue, StrongValue) noexcept = default;
        friend constexpr auto operator<=>(StrongValue, StrongValue) noexcept = default;

        friend std::ostream& operator<<(std::ostream& stream, StrongValue value)
        {
            return stream << value.toString();
        }

    private:
        struct ConstructionToken
        {
        };

        constexpr explicit StrongValue(Value value, ConstructionToken) noexcept
            : mValue(value)
        {
        }

        Value mValue;
    };
}

namespace std
{
    template <class Tag, TES3MP::Detail::StrongValuePolicy Policy>
    struct hash<TES3MP::Detail::StrongValue<Tag, Policy>>
    {
        std::size_t operator()(TES3MP::Detail::StrongValue<Tag, Policy> value) const noexcept
        {
            return std::hash<std::uint64_t>{}(value.value());
        }
    };
}

#endif
