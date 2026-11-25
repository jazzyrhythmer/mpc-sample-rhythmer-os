# Fizzik corresponding port source

Original source/notices are in port/original. generated/ contains the exact wrapper and parameter header used by this payload. common/ contains the native loader/state boundary and original Schwung API notices. wrapper/ and Open-MPC-MIT-LICENSE.txt retain the wrapper attribution. No proprietary SDK, sound bank or ROM is needed.

On Linux ARM64 with GCC: `sh build-linux.sh /absolute/empty/output`. Build on the target architecture; the output is a wrapper beside module/dsp.so. MPC firmware, sequencer and installers are not part of this port.
