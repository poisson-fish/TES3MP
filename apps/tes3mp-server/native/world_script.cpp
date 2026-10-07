#include "world_script.hpp"
#include <apps/openmw/mwscript/executioncontext.hpp>

#include <components/esm3/esmreader.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/loadscpt.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace TES3MP::Native
{
    namespace
    {
        static_assert(std::endian::native == std::endian::little);
        void require(bool value)
        { if (!value) throw std::invalid_argument("Invalid or incompatible shared world script image"); }

        // Finish a structural, allocation-free pass before ESMReader constructs
        // strings/locals. All admitted subrecords contain bounded text or scalars.
        struct Cursor
        {
            std::span<const char> bytes;
            std::span<const char> take(std::size_t size)
            {
                require(size <= bytes.size());
                auto value = bytes.first(size); bytes = bytes.subspan(size); return value;
            }
            uint32_t number()
            {
                const auto value = take(4);
                uint32_t result = 0;
                for (std::size_t i = 0; i < 4; ++i)
                    result |= uint32_t(static_cast<unsigned char>(value[i])) << (8 * i);
                return result;
            }
        };
        void preflight(std::span<const char> image)
        {
            require(!image.empty() && image.size() <= SharedWorldScript::MaximumImageBytes);
            Cursor input{image};
            std::size_t records = 0, locals = 0;
            while (!input.bytes.empty())
            {
                const auto tag = input.number(), size = input.number();
                require(input.number() == 0 && input.number() == 0);
                require(++records <= 3 + MWScript::WorldScript::MaximumGlobals);
                require((records == 1 && tag == ESM::fourCC("TES3"))
                    || (records == 2 && tag == ESM::fourCC("WSCR"))
                    || (records == 3 && tag == ESM::fourCC("GSCR"))
                    || (records > 3 && tag == ESM::fourCC("GLOB")));
                Cursor record{input.take(size)};
                std::size_t fields = 0;
                char globalType = 0;
                while (!record.bytes.empty())
                {
                    require(++fields <= 2 * MWScript::WorldScript::MaximumLocals + 8);
                    const auto field = record.number(), length = record.number();
                    const auto bytes = record.take(length);
                    if (tag == ESM::fourCC("TES3"))
                        require((field == ESM::fourCC("HEDR") && length <= 300)
                            || (field == ESM::fourCC("FORM") && length == 4));
                    else if (tag == ESM::fourCC("WSCR"))
                        require((field == ESM::fourCC("IDEN") && length == 32)
                            || (field == ESM::fourCC("TICK") && length == 8));
                    else if (field == ESM::fourCC("NAME"))
                        require(length > 1 && length <= MWScript::WorldScript::MaximumNameBytes + 1
                            && bytes[0] == static_cast<char>(ESM::RefIdType::UnsizedString));
                    else if (tag == ESM::fourCC("GSCR"))
                    {
                        if (field == ESM::fourCC("LOCA"))
                            require(++locals <= MWScript::WorldScript::MaximumLocals && length > 0
                                && length <= MWScript::WorldScript::MaximumNameBytes);
                        else require((field == ESM::fourCC("RUN_") || field == ESM::fourCC("INTV")
                                || field == ESM::fourCC("FLTV")) && length == 4);
                    }
                    else if (field == ESM::fourCC("FNAM"))
                    {
                        require(length == 1 && (bytes[0] == 's' || bytes[0] == 'l' || bytes[0] == 'f'));
                        globalType = bytes[0];
                    }
                    else
                    {
                        require(field == ESM::fourCC("FLTV") && length == 4 && globalType);
                        float value = 0;
                        std::memcpy(&value, bytes.data(), 4);
                        require(std::isfinite(value) && (globalType == 'f'
                            || (double(value) >= std::numeric_limits<int32_t>::min()
                                && double(value) <= std::numeric_limits<int32_t>::max())));
                    }
                }
            }
            require(records >= 3);
        }
    }

    SharedWorldScript::SharedWorldScript(const ESM::Script& script, std::vector<ESM::Global> globals,
        std::string_view campaignContent, CredentialCrypto& crypto)
        : mRuntime(script, std::move(globals))
    {
        require(!campaignContent.empty() && campaignContent.size() <= 256);
        // Bind source, ownership/defaults, limits and the content/campaign identity.
        // Stock GSCR/GLOB records remain the state representation.
        auto binding = encode(initial());
        const auto append = [&](std::string_view text) {
            binding.insert(binding.end(), text.begin(), text.end()); binding.push_back('\0');
        };
        append("context-free-world-script-1;8192;2048;4096;64;16;128;8;256;32768");
        append(campaignContent); append(script.mScriptText);
        require(crypto.sha256(std::as_bytes(std::span(binding)), mIdentity));
    }

    SharedWorldScript::State SharedWorldScript::initial() const { return {0, mRuntime.initial()}; }

    std::vector<char> SharedWorldScript::encode(const State& state) const
    {
        mRuntime.validate(state.runtime);
        std::ostringstream output(std::ios::binary);
        ESM::ESMWriter writer;
        writer.setVersion(ESM::VER_130);
        writer.setType(0);
        writer.setFormatVersion(ESM::CurrentSaveGameFormatVersion);
        writer.setRecordCount(static_cast<int>(2 + state.runtime.globals.size()));
        writer.save(output);
        writer.startRecord("WSCR");
        writer.writeHNT("IDEN", mIdentity.bytes);
        writer.writeHNT("TICK", state.tick);
        writer.endRecord("WSCR");
        writer.startRecord("GSCR"); state.runtime.script.save(writer); writer.endRecord("GSCR");
        for (const auto& global : state.runtime.globals)
        { writer.startRecord("GLOB"); global.save(writer); writer.endRecord("GLOB"); }
        writer.close();
        const auto bytes = output.str();
        require(bytes.size() <= MaximumImageBytes);
        return {bytes.begin(), bytes.end()};
    }

    SharedWorldScript::State SharedWorldScript::restore(std::span<const char> image) const
    {
        preflight(image);
        auto stream = std::make_unique<std::istringstream>(std::string(image.data(), image.size()), std::ios::binary);
        ESM::ESMReader reader;
        reader.open(std::move(stream), {});
        require(reader.getFormatVersion() == ESM::CurrentSaveGameFormatVersion);
        require(reader.getRecName() == ESM::fourCC("WSCR")); reader.getRecHeader();
        CredentialDigest identity;
        reader.getHNT(identity.bytes, "IDEN");
        require(identity == mIdentity);
        State state;
        reader.getHNT(state.tick, "TICK"); require(!reader.hasMoreSubs());
        require(reader.getRecName() == ESM::fourCC("GSCR")); reader.getRecHeader();
        state.runtime.script.load(reader); require(!reader.hasMoreSubs());
        while (reader.hasMoreRecs())
        {
            require(state.runtime.globals.size() < MWScript::WorldScript::MaximumGlobals
                && reader.getRecName() == ESM::fourCC("GLOB")); reader.getRecHeader();
            bool deleted = false;
            auto& global = state.runtime.globals.emplace_back();
            global.load(reader, deleted); require(!deleted && !reader.hasMoreSubs());
        }
        mRuntime.validate(state.runtime);
        const auto canonical = encode(state);
        require(canonical.size() == image.size() && std::equal(canonical.begin(), canonical.end(), image.begin()));
        return state;
    }

    SharedWorldScript::Prepared SharedWorldScript::prepare(const State& committed, uint64_t tick) const
    { return prepareImpl(committed, tick, nullptr, nullptr, nullptr); }

    SharedWorldScript::Prepared SharedWorldScript::prepare(const State& committed,
        const MWScript::ExecutionContext& execution, const MWWorld::ESMStore& content,
        const MWScript::ReferenceResolver& resolver) const
    { return prepareImpl(committed, execution.invocation, &execution, &content, &resolver); }

    SharedWorldScript::Prepared SharedWorldScript::prepareImpl(const State& committed, uint64_t tick,
        const MWScript::ExecutionContext* execution, const MWWorld::ESMStore* content,
        const MWScript::ReferenceResolver* resolver) const
    {
        Prepared prepared;
        auto& result = prepared.result;
        result.script = mRuntime.id();
        result.invocationTick = tick;
        try
        {
            if (execution) execution->validate();
            mRuntime.validate(committed.runtime);
            if (tick && tick == committed.tick)
            { result.outcome = Outcome::Committed; return prepared; }
            require(tick && committed.tick != std::numeric_limits<uint64_t>::max() && tick == committed.tick + 1);
            auto invocation = execution ? mRuntime.execute(committed.runtime, *execution, *content, *resolver, &result.trace)
                : mRuntime.execute(committed.runtime, &result.trace);
            prepared.state = {tick, std::move(invocation.state)};
            prepared.image = encode(prepared.state);
            result.presentation = std::move(invocation.presentation);
            result.executed = true;
            // Prepared output belongs to the containing transaction until durable.
            result.outcome = Outcome::Committed;
        }
        catch (const MWScript::ScriptContextUnavailable& error)
        {
            result.outcome = Outcome::Deferred;
            result.diagnostic = error.what();
        }
        catch (const std::exception& error)
        {
            result.diagnostic = std::string_view(error.what()).substr(0, MaximumDiagnosticBytes);
        }
        return prepared;
    }

    SharedWorldScript::Result SharedWorldScript::transact(State& committed, uint64_t tick,
        const DurableSink& persist) const
    {
        auto prepared = prepare(committed, tick);
        auto& result = prepared.result;
        if (result.outcome != Outcome::Committed || !result.executed) return std::move(result);
        try
        {
            // Complete allocations/validation before durability; installation and
            // moving presentation output after this point cannot allocate.
            try
            {
                if (!persist(prepared.image))
                {
                    result.outcome = Outcome::Deferred;
                    result.executed = false; result.presentation.clear();
                    result.diagnostic = "Durable shared world commit unavailable";
                    return std::move(result);
                }
            }
            catch (const std::exception&)
            {
                result.outcome = Outcome::Deferred;
                result.executed = false; result.presentation.clear();
                result.diagnostic = "Durable shared world commit failed";
                return std::move(result);
            }
            using std::swap;
            static_assert(std::is_nothrow_swappable_v<State>);
            swap(committed, prepared.state);
        }
        catch (const std::exception& error)
        {
            result.outcome = Outcome::Rejected;
            result.executed = false; result.presentation.clear();
            result.diagnostic = std::string_view(error.what()).substr(0, MaximumDiagnosticBytes);
        }
        return std::move(result);
    }
}
