#include "block_range_field_impl.h"

#include <util/string/builder.h>

namespace NYdb::NBS::NBlockStore {

////////////////////////////////////////////////////////////////////////////////

TNodeBasedBlockRangeFieldBase::ERealization
TNodeBasedBlockRangeFieldBase::GetRealization() const
{
    return ERealization::NodeBased;
}

TString TNodeBasedBlockRangeFieldBase::Print() const
{
    if (GetRealization() != ERealization::NodeBased) {
        return "not implemented";
    }

    TStringBuilder sb;
    Enumerate(
        [&](const TRange& r)
        {
            sb << r.Print();
            return EEnumerateContinuation::Continue;
        });
    return sb;
}

void TNodeBasedBlockRangeFieldBase::Serialize(TString* out) const
{
    Y_UNUSED(out);
}

////////////////////////////////////////////////////////////////////////////////

}   // namespace NYdb::NBS::NBlockStore
