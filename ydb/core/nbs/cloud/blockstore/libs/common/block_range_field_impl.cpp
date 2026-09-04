#include "block_range_field_impl.h"

#include <util/string/builder.h>

namespace NYdb::NBS::NBlockStore {

////////////////////////////////////////////////////////////////////////////////

TString IBlockRangeFieldImpl::Print() const
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

void IBlockRangeFieldImpl::Serialize(TString* out) const
{
    Y_UNUSED(out);
}

////////////////////////////////////////////////////////////////////////////////

}   // namespace NYdb::NBS::NBlockStore
