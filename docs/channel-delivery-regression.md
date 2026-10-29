# Repeated CW delivery regression

The x64 build enables `THREAD_DCW`. In that path, startup enabled DCW STRICT
and SuperSmartFilter level 5 unconditionally. DCW STRICT treated any repeated
16-byte CW for the same CAID/provider/SID within five seconds as stale and
rejected it; SuperSmartFilter also rejected some duplicate and cross-channel
matches. Repeated CWs are not, on their own, evidence that a response is bad.

`tests/cccam-repeated-cw.py` exercises a real isolated CCcam client and
synthetic Newcamd upstream. The peer supplies the same checksum-valid CW for
two distinct ECM requests for one service. Before this change the first
response reached the client, the second timed out, and the server reported
`DCW-STRICT-REJECT ... stale/recycled`. With this change both requests are
answered and the fixture confirms two upstream replies. This reproduces one
real delivery defect; it does not establish the cause of a particular live
server's outage or prove that a receiver descrambles video.

Until their rules can be validated and exposed safely, the two heuristic
rejection engines are initialized but disabled by default. Their code remains;
this does not disable the existing `acceptDCW_profile_ca_sid` checks, normal
DCW filters, cache policies or operator configuration. This is a deliberate
trade-off: heuristic fake/stale rejection and its associated penalties are
inactive, so some invalid words that only these heuristics would detect may
pass. They must not be represented as active protection. Other paths and
policies can still reject or delay words and require separate investigation.

Checks: x64 forced build with no warnings, repeated-CW end-to-end regression,
isolated CCcam success/failure and trace tests, crypto known-answer ASan/UBSan,
trace core, log-viewer regression and the `make-x64` unit suite. No real
provider or production deployment was tested.
