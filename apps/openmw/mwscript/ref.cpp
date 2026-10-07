#include "ref.hpp"

#include <components/interpreter/runtime.hpp>

#include "interpretercontext.hpp"

MWWorld::Ptr MWScript::ExplicitRef::operator()(Interpreter::Runtime& runtime, bool required, bool activeOnly) const
{
    const auto name = runtime.getStringLiteral(runtime[0].mInteger);
    runtime.pop();

    auto& context = static_cast<MWScript::InterpreterContext&>(runtime.getContext());
    return context.resolveReferenceName(name, required, activeOnly);
}

MWWorld::Ptr MWScript::ImplicitRef::operator()(Interpreter::Runtime& runtime, bool required, bool activeOnly) const
{
    MWScript::InterpreterContext& context = static_cast<MWScript::InterpreterContext&>(runtime.getContext());

    return context.resolveReference({}, required, activeOnly);
}
