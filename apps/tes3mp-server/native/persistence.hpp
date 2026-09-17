#ifndef TES3MP_NATIVE_PERSISTENCE_H
#define TES3MP_NATIVE_PERSISTENCE_H
namespace TES3MP::Native
{
    enum class PersistenceResult { Rejected, Accepted, Uncertain };
}
namespace MWWorld::Testing
{
    using TestPersistenceResult = TES3MP::Native::PersistenceResult;
}
#endif
