/// Platforms with no folder chooser of their own to call.
///
/// Not an error and not a gap: the browser in folder_picker.cpp is what answers
/// here, and it needs nothing installed to work.

#include "folder_picker.hpp"

namespace wowee::assets {

bool haveNativePicker() { return false; }

bool pickNative(PickWhat, const std::string&, const std::string&, const std::string&,
                std::string*) {
    return false;
}

}  // namespace wowee::assets
