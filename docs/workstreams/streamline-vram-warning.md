# Streamline evaluate warning handling

Base: fork main, 0.2.45. Fix not implemented.

Streamline 2.14.1 can return eWarnOutOfVRAM after successful evaluation. Preserve output copies, completion bookkeeping and the warning result. Real errors must remain fatal, including mixed-eye error/warning cases.

Test the right-eye copy, publication, aggregation and fences; reproduce repeated map transitions. This does not eliminate VRAM pressure.

Reference: https://github.com/NVIDIAGameWorks/Streamline/blob/v2.14.1/source/plugins/sl.common/commonInterface.cpp
