#!/bin/bash
# You need to run this to build slang and newt.
# Most of the time you can just run tup.
cd deps_tui/slang/ && ./configure && make static && cd -
cd deps_tui/newt/ && ./autogen.sh && ./configure && make && cd -
tup
