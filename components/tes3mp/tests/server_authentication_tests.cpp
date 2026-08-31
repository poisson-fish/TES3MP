#include <tes3mp/server_authentication.hpp>

#include <algorithm>
#include <array>
#include <barrier>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <ostream>
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

    template <class T>
    concept Streamable = requires(std::ostream& stream, const T& value) { stream << value; };

    template <class T>
    concept HasBytesAccessor = requires(const T& value) { value.bytes(); };

    static_assert(!Streamable<AdmissionScopeId>);
    static_assert(!HasBytesAccessor<AdmissionScopeId>);

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
            ++comparisonCalls;
            lastComparisonBytes = left.size();
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
        std::size_t comparisonCalls = 0;
        std::size_t lastComparisonBytes = 0;
    };

    class FixedClock final : public MonotonicClock
    {
    public:
        MonotonicInstant now() const noexcept override { return value; }
        MonotonicInstant value = MonotonicInstant::fromNanoseconds(0);
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

    AuthenticationAttempt attempt(std::uint64_t id = 1, std::uint64_t generationValue = 1)
    {
        return AuthenticationAttempt{ *AuthenticationAttemptId::fromValue(id), generation(generationValue) };
    }

    AuthenticationMaterial material(std::span<const std::byte> bytes = {})
    {
        return *AuthenticationMaterial::create(bytes);
    }

    AdmissionScopeId scope(std::uint64_t value)
    {
        std::array<std::byte, AdmissionScopeIdBytes> bytes{};
        for (std::size_t index = 0; index < sizeof(value); ++index)
            bytes[index] = static_cast<std::byte>((value >> (index * 8u)) & 0xffu);
        return *AdmissionScopeId::create(bytes);
    }

    MonotonicInstant milliseconds(std::uint64_t value)
    {
        return MonotonicInstant::fromNanoseconds(value * 1'000'000);
    }

    AuthenticationRateLimitPolicy ratePolicy(std::size_t sourceBurst = 2, std::size_t globalBurst = 4,
        std::uint64_t sourceRefill = MinimumAuthenticationRefillMilliseconds,
        std::uint64_t globalRefill = MinimumAuthenticationRefillMilliseconds)
    {
        return *AuthenticationRateLimitPolicy::create(sourceBurst, globalBurst, sourceRefill, globalRefill);
    }

    std::optional<AuthenticatedAdmission> accepted(AuthenticationPollResult result)
    {
        auto* completion = std::get_if<AuthenticationCompletion>(&result);
        if (!completion)
            return std::nullopt;
        auto* admission = std::get_if<AuthenticatedAdmission>(&completion->result);
        if (!admission)
            return std::nullopt;
        return std::move(*admission);
    }

    bool join_password_provider_handles_open_protected_and_denied_inputs()
    {
        FakeCrypto crypto;
        auto open = JoinPasswordAuthenticationProvider::create(crypto, material());
        auto openOperation = open->begin(attempt(), material());
        auto openAdmission = accepted(openOperation->poll());
        if (!openAdmission || openAdmission->isResume())
            return false;

        const std::array expected{ std::byte{ 0x10 }, std::byte{ 0x20 }, std::byte{ 0x30 } };
        auto protectedProvider = JoinPasswordAuthenticationProvider::create(crypto, material(expected));
        auto exactOperation = protectedProvider->begin(attempt(2), material(expected));
        auto exactAdmission = accepted(exactOperation->poll());
        if (!exactAdmission || exactAdmission->principal() == openAdmission->principal())
            return false;

        const std::array wrong{ std::byte{ 0x10 }, std::byte{ 0x20 }, std::byte{ 0x31 } };
        auto wrongOperation = protectedProvider->begin(attempt(3), material(wrong));
        auto wrongResult = wrongOperation->poll();
        auto* wrongCompletion = std::get_if<AuthenticationCompletion>(&wrongResult);
        auto* wrongRejection
            = wrongCompletion ? std::get_if<AuthenticationRejected>(&wrongCompletion->result) : nullptr;
        auto missingOperation = protectedProvider->begin(attempt(4), material());
        auto missingResult = missingOperation->poll();
        auto* missingCompletion = std::get_if<AuthenticationCompletion>(&missingResult);
        auto* missingRejection
            = missingCompletion ? std::get_if<AuthenticationRejected>(&missingCompletion->result) : nullptr;

        std::array<std::byte, MaximumAuthenticationMaterialBytes> maximum{};
        maximum.fill(std::byte{ 0x42 });
        auto maximumProvider = JoinPasswordAuthenticationProvider::create(crypto, material(maximum));
        auto maximumOperation = maximumProvider->begin(attempt(5), material(maximum));
        auto maximumAdmission = accepted(maximumOperation->poll());

        return wrongRejection && wrongRejection->reason == AuthenticationRejectionReason::Denied && missingRejection
            && missingRejection->reason == AuthenticationRejectionReason::Denied && maximumAdmission
            && crypto.comparisonCalls == 5
            && crypto.lastComparisonBytes == MaximumAuthenticationMaterialBytes + sizeof(std::uint16_t);
    }

    bool join_password_operation_is_cancellable_and_one_shot()
    {
        FakeCrypto crypto;
        auto provider = JoinPasswordAuthenticationProvider::create(crypto, material());
        auto operation = provider->begin(attempt(8, 3), material());
        operation->cancel();
        operation->cancel();
        auto result = operation->poll();
        auto* completion = std::get_if<AuthenticationCompletion>(&result);
        auto* rejection = completion ? std::get_if<AuthenticationRejected>(&completion->result) : nullptr;
        return completion && completion->attempt == attempt(8, 3) && rejection
            && rejection->reason == AuthenticationRejectionReason::Cancelled
            && std::holds_alternative<AuthenticationPending>(operation->poll());
    }

    bool admission_scope_is_exact_and_opaque()
    {
        std::array<std::byte, AdmissionScopeIdBytes> exact{};
        std::array<std::byte, AdmissionScopeIdBytes - 1> shortValue{};
        std::array<std::byte, AdmissionScopeIdBytes + 1> longValue{};
        auto first = AdmissionScopeId::create(exact);
        auto same = AdmissionScopeId::create(exact);
        exact.back() = std::byte{ 1 };
        auto different = AdmissionScopeId::create(exact);
        return !AdmissionScopeId::create(shortValue) && first && same && different && *first == *same
            && *first != *different && !AdmissionScopeId::create(longValue);
    }

    bool rate_policy_bounds_are_closed()
    {
        return !AuthenticationRateLimitPolicy::create(0, 1, 100, 100)
            && AuthenticationRateLimitPolicy::create(1, 1, 100, 100)
            && AuthenticationRateLimitPolicy::create(16, 256, 120'000, 120'000)
            && !AuthenticationRateLimitPolicy::create(17, 256, 120'000, 120'000)
            && !AuthenticationRateLimitPolicy::create(16, 257, 120'000, 120'000)
            && !AuthenticationRateLimitPolicy::create(1, 1, 99, 100)
            && !AuthenticationRateLimitPolicy::create(1, 1, 100, 120'001);
    }

    bool source_and_global_buckets_refill_at_exact_edges()
    {
        auto sourceLimited = AuthenticationRateLimiter::create(ratePolicy(2, 16), milliseconds(0));
        if (sourceLimited->allow(scope(1), milliseconds(0)) != AuthenticationRateLimitResult::Allowed
            || sourceLimited->allow(scope(1), milliseconds(0)) != AuthenticationRateLimitResult::Allowed
            || sourceLimited->allow(scope(1), milliseconds(99)) != AuthenticationRateLimitResult::SourceExhausted
            || sourceLimited->allow(scope(1), milliseconds(100)) != AuthenticationRateLimitResult::Allowed)
            return false;

        auto globalLimited = AuthenticationRateLimiter::create(ratePolicy(4, 2), milliseconds(0));
        return globalLimited->allow(scope(1), milliseconds(0)) == AuthenticationRateLimitResult::Allowed
            && globalLimited->allow(scope(2), milliseconds(0)) == AuthenticationRateLimitResult::Allowed
            && globalLimited->allow(scope(3), milliseconds(99)) == AuthenticationRateLimitResult::GlobalExhausted
            && globalLimited->allow(scope(3), milliseconds(100)) == AuthenticationRateLimitResult::Allowed;
    }

    bool source_state_survives_repeated_calls_and_table_saturation()
    {
        auto limiter = AuthenticationRateLimiter::create(ratePolicy(1, 256), milliseconds(0));
        if (limiter->allow(scope(1), milliseconds(0)) != AuthenticationRateLimitResult::Allowed
            || limiter->allow(scope(1), milliseconds(0)) != AuthenticationRateLimitResult::SourceExhausted
            || limiter->trackedScopes() != 1)
            return false;
        for (std::size_t index = 2; index <= MaximumAuthenticationAdmissionScopes; ++index)
        {
            if (limiter->allow(scope(index), milliseconds(0)) != AuthenticationRateLimitResult::Allowed)
                return false;
        }
        return limiter->trackedScopes() == MaximumAuthenticationAdmissionScopes
            && limiter->allow(scope(MaximumAuthenticationAdmissionScopes + 1), milliseconds(100))
            == AuthenticationRateLimitResult::SourceTableFull
            && limiter->trackedScopes() == MaximumAuthenticationAdmissionScopes;
    }

    bool clock_regression_and_concurrent_attempts_fail_closed()
    {
        auto regressed = AuthenticationRateLimiter::create(ratePolicy(), milliseconds(10));
        if (regressed->allow(scope(1), milliseconds(9)) != AuthenticationRateLimitResult::ClockRegressed
            || regressed->trackedScopes() != 0)
            return false;

        auto concurrent = AuthenticationRateLimiter::create(ratePolicy(1, 8), milliseconds(0));
        constexpr std::size_t Threads = 8;
        std::barrier start(static_cast<std::ptrdiff_t>(Threads + 1));
        std::array<AuthenticationRateLimitResult, Threads> results{};
        std::vector<std::thread> workers;
        for (std::size_t index = 0; index < Threads; ++index)
        {
            workers.emplace_back([&, index] {
                start.arrive_and_wait();
                results[index] = concurrent->allow(scope(2), milliseconds(0));
            });
        }
        start.arrive_and_wait();
        for (auto& worker : workers)
            worker.join();
        return std::count(results.begin(), results.end(), AuthenticationRateLimitResult::Allowed) == 1
            && std::count(results.begin(), results.end(), AuthenticationRateLimitResult::SourceExhausted)
            == Threads - 1;
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

    bool shared_service_gates_routes_and_defers_resume_consumption()
    {
        FakeCrypto crypto;
        FixedClock clock;
        auto limiter = AuthenticationRateLimiter::create(ratePolicy(1, 4), clock.now());
        auto join = JoinPasswordAuthenticationProvider::create(crypto, material());
        auto tokens = store(crypto);
        SharedServerAuthenticationService service(*limiter, *join, *tokens, clock);

        auto first = service.begin(attempt(), ServerAuthenticationSubmission(
            AuthenticationRequest::join(material()), scope(1), context()));
        if (!first || !accepted(first->poll()) || crypto.comparisonCalls != 1)
            return false;

        auto limited = service.begin(attempt(2), ServerAuthenticationSubmission(
            AuthenticationRequest::join(material()), scope(1), context()));
        auto limitedPoll = limited->poll();
        auto* limitedCompletion = std::get_if<AuthenticationCompletion>(&limitedPoll);
        auto* limitedRejection
            = limitedCompletion ? std::get_if<AuthenticationRejected>(&limitedCompletion->result) : nullptr;
        if (!limitedRejection || limitedRejection->reason != AuthenticationRejectionReason::ProviderUnavailable
            || crypto.comparisonCalls != 1)
            return false;

        auto issued = tokens->issue(principal(), session(), generation(), context(), clock.now());
        auto token = std::get<AuthenticationAcceptedMessage>(std::move(issued)).takeToken();
        auto resume = service.begin(attempt(3), ServerAuthenticationSubmission(
            AuthenticationRequest::resume(std::move(token)), scope(2), context()));
        if (!resume || tokens->size() != 1)
            return false;
        resume->cancel();
        return tokens->size() == 1 && std::holds_alternative<AuthenticationPending>(resume->poll());
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
        Test{ "join_password_provider_handles_open_protected_and_denied_inputs",
            join_password_provider_handles_open_protected_and_denied_inputs },
        Test{ "join_password_operation_is_cancellable_and_one_shot",
            join_password_operation_is_cancellable_and_one_shot },
        Test{ "admission_scope_is_exact_and_opaque", admission_scope_is_exact_and_opaque },
        Test{ "rate_policy_bounds_are_closed", rate_policy_bounds_are_closed },
        Test{ "source_and_global_buckets_refill_at_exact_edges", source_and_global_buckets_refill_at_exact_edges },
        Test{ "source_state_survives_repeated_calls_and_table_saturation",
            source_state_survives_repeated_calls_and_table_saturation },
        Test{ "clock_regression_and_concurrent_attempts_fail_closed",
            clock_regression_and_concurrent_attempts_fail_closed },
        Test{ "policy_bounds_are_closed", policy_bounds_are_closed },
        Test{ "issue_resume_rotate_and_replay_are_atomic", issue_resume_rotate_and_replay_are_atomic },
        Test{ "precommit_failures_preserve_the_old_token", precommit_failures_preserve_the_old_token },
        Test{ "concurrent_replay_has_exactly_one_winner", concurrent_replay_has_exactly_one_winner },
        Test{ "expiry_overflow_and_crypto_failure_fail_closed", expiry_overflow_and_crypto_failure_fail_closed },
        Test{ "fixed_capacity_and_restart_invalidation_are_enforced",
            fixed_capacity_and_restart_invalidation_are_enforced },
        Test{ "shared_service_gates_routes_and_defers_resume_consumption",
            shared_service_gates_routes_and_defers_resume_consumption },
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
