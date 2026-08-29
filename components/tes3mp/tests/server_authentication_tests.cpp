#include <tes3mp/server_authentication.hpp>

#include <algorithm>
#include <array>
#include <barrier>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <thread>
#include <type_traits>
#include <variant>
#include <vector>

namespace
{
    using namespace TES3MP;

    static_assert(!std::is_copy_constructible_v<ResumeAdmissionGrant>);
    static_assert(!std::is_copy_constructible_v<AuthenticatedAdmission>);
    static_assert(std::variant_size_v<AuthenticationResult> == 2);

    class FakeCrypto final : public CredentialCrypto
    {
    public:
        bool randomBytes(std::span<std::byte> destination) noexcept override
        {
            if (failRandom)
                return false;
            std::fill(destination.begin(), destination.end(), std::byte{ seed });
            ++seed;
            return true;
        }

        bool sha256(std::span<const std::byte> source, CredentialDigest& destination) noexcept override
        {
            if (failDigest)
                return false;
            destination.bytes.fill(std::byte{ 0 });
            for (std::size_t index = 0; index < source.size(); ++index)
                destination.bytes[index % destination.bytes.size()] ^= source[index];
            return true;
        }

        bool constantTimeEqual(std::span<const std::byte> left, std::span<const std::byte> right) noexcept override
        {
            if (left.size() != right.size())
                return false;
            std::byte difference{ 0 };
            for (std::size_t index = 0; index < left.size(); ++index)
                difference |= left[index] ^ right[index];
            return difference == std::byte{ 0 };
        }

        std::uint8_t seed = 1;
        bool failRandom = false;
        bool failDigest = false;
    };

    PrincipalId principal(std::uint64_t value = 1)
    {
        return *PrincipalId::fromValue(value);
    }
    SessionId session(std::uint64_t value = 1)
    {
        return *SessionId::fromValue(value);
    }
    SessionGeneration generation(std::uint64_t value = 1)
    {
        return *SessionGeneration::fromValue(value);
    }

    ResumeTokenContext context(std::uint8_t protocol = 1, std::uint8_t content = 2)
    {
        ResumeTokenContext result;
        result.protocol.bytes.fill(std::byte{ protocol });
        result.content.bytes.fill(std::byte{ content });
        return result;
    }

    std::unique_ptr<ResumeTokenStore> store(
        FakeCrypto& crypto, std::uint64_t lifetime = MinimumResumeTokenLifetimeMilliseconds)
    {
        return ResumeTokenStore::create(crypto, lifetime);
    }

    bool policy_bounds_are_closed()
    {
        FakeCrypto crypto;
        return !ResumeTokenStore::create(crypto, MinimumResumeTokenLifetimeMilliseconds - 1)
            && ResumeTokenStore::create(crypto, MinimumResumeTokenLifetimeMilliseconds)
            && ResumeTokenStore::create(crypto, MaximumResumeTokenLifetimeMilliseconds)
            && !ResumeTokenStore::create(crypto, MaximumResumeTokenLifetimeMilliseconds + 1);
    }

    bool issue_resume_rotate_and_replay_are_atomic()
    {
        FakeCrypto crypto;
        auto tokens = store(crypto);
        auto issued
            = tokens->issue(principal(7), session(9), generation(3), context(), MonotonicInstant::fromNanoseconds(10));
        auto* accepted = std::get_if<AuthenticationAcceptedMessage>(&issued);
        if (!accepted || tokens->size() != 1)
            return false;
        auto original = accepted->takeToken();

        auto wrongContext = tokens->consume(original, context(3, 4), MonotonicInstant::fromNanoseconds(11));
        if (std::get<ResumeTokenStoreError>(wrongContext) != ResumeTokenStoreError::Denied || tokens->size() != 1)
            return false;

        auto resumed = tokens->consume(original, context(), MonotonicInstant::fromNanoseconds(11));
        auto* admission = std::get_if<AuthenticatedAdmission>(&resumed);
        if (!admission || admission->principal() != principal(7) || !admission->isResume() || tokens->size() != 1)
            return false;
        auto grant = admission->takeResumeGrant();
        if (!grant || grant->sessionId() != session(9) || grant->priorGeneration() != generation(3)
            || grant->nextGeneration() != generation(4) || admission->isResume() || admission->takeResumeGrant())
            return false;
        auto response = grant->takeResponse();
        if (!response || grant->takeResponse())
            return false;
        auto replacement = response->takeToken();

        return std::get<ResumeTokenStoreError>(
                   tokens->consume(original, context(), MonotonicInstant::fromNanoseconds(12)))
            == ResumeTokenStoreError::Denied
            && std::holds_alternative<AuthenticatedAdmission>(
                tokens->consume(replacement, context(), MonotonicInstant::fromNanoseconds(12)));
    }

    bool precommit_failures_preserve_the_old_token()
    {
        FakeCrypto crypto;
        auto tokens = store(crypto);
        auto issued
            = tokens->issue(principal(), session(), generation(), context(), MonotonicInstant::fromNanoseconds(0));
        auto token = std::get<AuthenticationAcceptedMessage>(std::move(issued)).takeToken();

        crypto.seed = 1;
        if (std::get<ResumeTokenStoreError>(tokens->consume(token, context(), MonotonicInstant::fromNanoseconds(1)))
            != ResumeTokenStoreError::DigestCollision)
            return false;
        crypto.seed = 2;
        return std::holds_alternative<AuthenticatedAdmission>(
                   tokens->consume(token, context(), MonotonicInstant::fromNanoseconds(2)))
            && tokens->size() == 1;
    }

    bool concurrent_replay_has_exactly_one_winner()
    {
        FakeCrypto crypto;
        auto tokens = store(crypto);
        auto issued
            = tokens->issue(principal(), session(), generation(), context(), MonotonicInstant::fromNanoseconds(0));
        auto token = std::get<AuthenticationAcceptedMessage>(std::move(issued)).takeToken();

        constexpr std::size_t Threads = 8;
        std::barrier start(static_cast<std::ptrdiff_t>(Threads + 1));
        std::array<std::optional<ResumeTokenConsumeResult>, Threads> results;
        std::vector<std::thread> workers;
        workers.reserve(Threads);
        for (std::size_t index = 0; index < Threads; ++index)
        {
            workers.emplace_back([&, index] {
                start.arrive_and_wait();
                results[index] = tokens->consume(token, context(), MonotonicInstant::fromNanoseconds(1));
            });
        }
        start.arrive_and_wait();
        for (auto& worker : workers)
            worker.join();

        std::size_t accepted = 0;
        std::size_t denied = 0;
        for (auto& result : results)
        {
            if (result && std::holds_alternative<AuthenticatedAdmission>(*result))
                ++accepted;
            else if (result && std::get<ResumeTokenStoreError>(*result) == ResumeTokenStoreError::Denied)
                ++denied;
        }
        return accepted == 1 && denied == Threads - 1 && tokens->size() == 1;
    }

    bool expiry_overflow_and_crypto_failure_fail_closed()
    {
        FakeCrypto crypto;
        auto tokens = store(crypto);
        auto issued
            = tokens->issue(principal(), session(), generation(), context(), MonotonicInstant::fromNanoseconds(0));
        auto token = std::get<AuthenticationAcceptedMessage>(std::move(issued)).takeToken();
        if (std::get<ResumeTokenStoreError>(
                tokens->consume(token, context(), MonotonicInstant::fromNanoseconds(1'000'000'000)))
                != ResumeTokenStoreError::Denied
            || tokens->size() != 0)
            return false;

        FakeCrypto overflowCrypto;
        auto generationOverflow = store(overflowCrypto);
        auto maximumGeneration = generation(std::numeric_limits<std::uint64_t>::max());
        auto maximumIssued = generationOverflow->issue(
            principal(), session(), maximumGeneration, context(), MonotonicInstant::fromNanoseconds(0));
        auto maximumToken = std::get<AuthenticationAcceptedMessage>(std::move(maximumIssued)).takeToken();
        if (std::get<ResumeTokenStoreError>(
                generationOverflow->consume(maximumToken, context(), MonotonicInstant::fromNanoseconds(1)))
                != ResumeTokenStoreError::GenerationOverflow
            || generationOverflow->size() != 1)
            return false;

        FakeCrypto failing;
        auto unavailable = store(failing);
        failing.failRandom = true;
        if (std::get<ResumeTokenStoreError>(unavailable->issue(
                principal(), session(), generation(), context(), MonotonicInstant::fromNanoseconds(0)))
            != ResumeTokenStoreError::RandomUnavailable)
            return false;
        failing.failRandom = false;
        failing.failDigest = true;
        if (std::get<ResumeTokenStoreError>(unavailable->issue(
                principal(), session(), generation(), context(), MonotonicInstant::fromNanoseconds(0)))
            != ResumeTokenStoreError::DigestUnavailable)
            return false;

        auto overflow = store(failing);
        failing.failDigest = false;
        return std::get<ResumeTokenStoreError>(overflow->issue(principal(), session(), generation(), context(),
                   MonotonicInstant::fromNanoseconds(std::numeric_limits<std::uint64_t>::max())))
            == ResumeTokenStoreError::DeadlineOverflow;
    }

    bool fixed_capacity_and_restart_invalidation_are_enforced()
    {
        FakeCrypto crypto;
        auto tokens = store(crypto);
        std::optional<ResumeToken> first;
        for (std::size_t index = 0; index < MaximumResumeTokenRecords; ++index)
        {
            auto issued
                = tokens->issue(principal(), session(), generation(), context(), MonotonicInstant::fromNanoseconds(0));
            auto* accepted = std::get_if<AuthenticationAcceptedMessage>(&issued);
            if (!accepted)
                return false;
            if (index == 0)
                first = accepted->takeToken();
        }
        if (tokens->size() != MaximumResumeTokenRecords
            || std::get<ResumeTokenStoreError>(
                   tokens->issue(principal(), session(), generation(), context(), MonotonicInstant::fromNanoseconds(0)))
                != ResumeTokenStoreError::Full)
            return false;

        FakeCrypto restartedCrypto;
        auto restarted = store(restartedCrypto);
        return first
            && std::get<ResumeTokenStoreError>(
                   restarted->consume(*first, context(), MonotonicInstant::fromNanoseconds(1)))
            == ResumeTokenStoreError::Denied;
    }
}

int main()
{
    struct Test
    {
        std::string_view name;
        bool (*run)();
    };
    constexpr std::array tests{
        Test{ "policy_bounds_are_closed", policy_bounds_are_closed },
        Test{ "issue_resume_rotate_and_replay_are_atomic", issue_resume_rotate_and_replay_are_atomic },
        Test{ "precommit_failures_preserve_the_old_token", precommit_failures_preserve_the_old_token },
        Test{ "concurrent_replay_has_exactly_one_winner", concurrent_replay_has_exactly_one_winner },
        Test{ "expiry_overflow_and_crypto_failure_fail_closed", expiry_overflow_and_crypto_failure_fail_closed },
        Test{ "fixed_capacity_and_restart_invalidation_are_enforced",
            fixed_capacity_and_restart_invalidation_are_enforced },
    };
    for (const auto& test : tests)
    {
        if (!test.run())
        {
            std::cerr << "FAILED: " << test.name << '\n';
            return 1;
        }
    }
    return 0;
}
